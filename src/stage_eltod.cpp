#include "stage_eltod.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include "util/csv.h"
#include "util/gz_io.h"

namespace ap {

namespace {

// External-station calibration factors (2023 count-to-volume).
constexpr long long I95 = 11560, I75 = 11548, I10 = 11504;
constexpr double I95_SCALE = 0.8951645, I75_SCALE = 0.7947358, I10_SCALE = 1.089312;
constexpr long long MIN_HHID = 15000000, MIN_TRK_HHID = 13000000;

double ext_scale(long long o, long long d) {
    double f = 1.0;
    if (o == I95 || d == I95) f *= I95_SCALE;
    if (o == I75 || d == I75) f *= I75_SCALE;
    if (o == I10 || d == I10) f *= I10_SCALE;
    return f;
}

// 15-min segment (1..96) -> clock hour (1..24, 24 = midnight) as in R.
int hour_from_seg(int seg) { int h = (seg * 15) / 60; return h == 0 ? 24 : h; }

std::string strip_vot_suffix(const std::string& m) {
    for (const char* suf : {"_Med", "_Low", "_Hig"}) {
        size_t n = std::string(suf).size();
        if (m.size() >= n && m.compare(m.size() - n, n, suf) == 0)
            return m.substr(0, m.size() - n);
    }
    return m;
}

// Accumulates an OD trip table: (hour, O, D) -> segment -> summed vehTrips.
struct ODTable {
    std::map<std::tuple<int, long long, long long>, std::map<std::string, double>> cells;
    std::set<std::string> segments;
    void add(int hour, long long o, long long d, const std::string& seg, double w) {
        cells[{hour, o, d}][seg] += w;
        segments.insert(seg);
    }
    void calibrate_externals() {
        for (auto& kv : cells) {
            long long o = std::get<1>(kv.first), d = std::get<2>(kv.first);
            double f = ext_scale(o, d);
            if (f != 1.0) for (auto& sc : kv.second) sc.second *= f;
        }
    }
    void write_csv(const std::string& path, bool hourclock) const {
        std::ofstream out(path);
        out << (hourclock ? "STARTTIME" : "period") << ",O,D";
        for (const auto& seg : segments) out << "," << seg;
        out << "\n";
        for (const auto& kv : cells) {
            int hr = std::get<0>(kv.first);
            if (hourclock) { char b[8]; std::snprintf(b, sizeof b, "%02d:00", hr); out << b; }
            else out << hr;
            out << "," << std::get<1>(kv.first) << "," << std::get<2>(kv.first);
            for (const auto& seg : segments) {
                auto it = kv.second.find(seg);
                out << "," << (it == kv.second.end() ? 0.0 : it->second);
            }
            out << "\n";
        }
    }
};

double occupancy_for(int tripMode) {
    if (tripMode == 3 || tripMode == 4) return 2.0;
    if (tripMode == 5 || tripMode == 6) return 3.2;
    return 1.0;
}

std::string vot_class(double v, double lo, double hi, const char* base) {
    std::string b = base;
    if (v <= lo) return b + "_Low";
    if (v <= hi) return b + "_Med";
    return b + "_Hig";
}

// Read an SDT trip list (resident or visitor) and emit list rows + OD entries.
struct SdtCols { int oTaz, dTaz, period, vot, mode, exp, dPurp, oPurp, hh, per, tour, trip; };

} // namespace

void run_stage_eltod(const Settings& s, const Lookups& lk, Rng& rng,
                     const std::vector<LdtTrip>& ldt_trips) {
    if (s.LimitCounties)
        throw std::runtime_error("LimitCounties is not supported in the C++ port "
                                 "(needs hhID_by_county.csv); set LimitCounties=false");

    std::vector<ListRow> list;
    ODTable od;             // combined ELToD OD table (by hour)
    ODTable sdt_res_od;     // SDT resident purpose*VOT OD table (by hour)

    const std::string res_file = s.scen(s.sdt_res_trips, "trips_" + std::to_string(s.feedback_loop) + ".csv");
    const std::string vis_file = s.scen(s.sdt_vis_trips, "visitorTrips.csv");

    // ---------------- SDT visitors & residents ----------------
    auto purpose_res = [](int dp, int op) -> std::string {
        int p = dp >= 0 ? dp : op;
        switch (p) {
            case 0: return "Work"; case 1: return "University"; case 2: return "School";
            case 3: return "Escort"; case 4: return "Maintenance"; case 5: return "Discretionary";
            case 6: return "AtWork"; default: return "None";
        }
    };
    auto purpose_vis = [](int dp, int op) -> std::string {
        int p = dp >= 0 ? dp : op;
        switch (p) {
            case 0: return "WorkVis"; case 1: return "Recreate"; case 2: return "Shop";
            case 3: return "Eatout"; default: return "None";
        }
    };

    // Bring an SDT period onto the internal 15-min segment grid (1..96) per the
    // declared SDT native resolution. 30-min periods are split into a 15-min
    // sub-segment; 15-min periods pass through; unknown (0) is uniform over the day.
    auto recode_seg = [&](int period) -> int {
        if (s.sdt_input_resolution == 15)
            return (period >= 1 && period <= 96) ? period : rng.uniform_int(1, 96);
        if (period >= 1 && period <= 48)
            return rng.uniform_int(0, 1) ? 2 * period : 2 * period - 1;
        return rng.uniform_int(1, 96);  // period 0: uniform over the day
    };

    auto process_sdt = [&](const std::string& file, bool resident) {
        GzLineReader rd(file);
        if (!rd.good()) throw std::runtime_error("cannot open SDT trips: " + file);
        std::string_view line; rd.next_line(line);
        CsvHeader h; h.parse(line);
        int cO = h.require("originTaz", file), cD = h.require("destinationTaz", file);
        int cPeriod = h.require("period", file), cVot = h.require("valueOfTime", file);
        int cMode = h.require("tripMode", file), cExp = h.require("expansionFactor", file);
        int cDP = h.require("destinationPurpose", file), cOP = h.require("originPurpose", file);
        int cHh = h.col("hh_id"), cPer = h.col("person_id"), cTour = h.col("tour_id"), cTrip = h.col("trip_id");
        std::vector<std::string_view> f;
        long long vis_counter = 0;
        const double lo = s.vot_sdt_low, hi = s.vot_sdt_high;
        while (rd.next_line(line)) {
            if (line.empty()) continue;
            split_csv(line, f);
            auto G = [&](int c) -> std::string_view { return c >= 0 && c < (int)f.size() ? f[c] : std::string_view(); };
            int mode = (int)to_ll(G(cMode));
            if (mode > 6) continue;                 // auto modes only (tripMode <= 6)
            double vt = to_double(G(cVot));
            int dp = (int)to_ll(G(cDP)), op = (int)to_ll(G(cOP));
            std::string purpose = resident ? purpose_res(dp, op) : purpose_vis(dp, op);
            std::string seg = resident ? vot_class(vt, lo, hi, "SDT_Res")
                                       : vot_class(vt, lo, hi, "SDT_Vis");
            double occ = occupancy_for(mode);
            double veh = to_double(G(cExp)) / occ;
            int period = (int)to_ll(G(cPeriod));
            int s15 = recode_seg(period);
            int hr = hour_from_seg(s15);
            long long o = to_ll(G(cO)), d = to_ll(G(cD));

            od.add(hr, o, d, seg, veh);

            ListRow r;
            r.O = o; r.D = d; r.valueOfTime = (double)(long long)vt;
            r.purpose = purpose; r.marketVot = seg; r.market = strip_vot_suffix(seg);
            r.vehTrips = veh; r.occupancy = occ; r.period = s.period_out(s15);
            if (resident) {
                r.hh_id = to_ll(G(cHh)); r.person_id = to_ll(G(cPer));
                r.tour_id = to_ll(G(cTour)); r.trip_id = to_ll(G(cTrip));
                // SDT resident purpose*VOT OD (output_SDT_Res_hourly)
                std::string pv = vot_class(vt, lo, hi, purpose.c_str());
                sdt_res_od.add(hr, o, d, pv, veh);
            } else {
                r.hh_id = MIN_HHID + (++vis_counter);
                r.person_id = 1;
                r.tour_id = to_ll(G(cTour)) - 1000000;
                r.trip_id = to_ll(G(cTrip));
            }
            list.push_back(std::move(r));
        }
        std::printf("[eltod] processed SDT %s\n", resident ? "residents" : "visitors");
    };

    process_sdt(vis_file, false);
    process_sdt(res_file, true);

    // ---------------- LDT (from stage A, in memory) ----------------
    {
        bool need_skim = s.track_AirTours || s.track_sdt_grt50M;
        if (need_skim) const_cast<Lookups&>(lk).load_skim(s);
        auto purpose_ldt = [](int p) -> std::string {
            switch (p) {
                case 1: return "PersonalBusiness"; case 2: return "VistFriendFamily";
                case 3: return "LeisureVacation"; case 4: return "CrossBorderCommute";
                case 5: return "EmployerBusiness"; default: return "None";
            }
        };
        long long ext_counter = 0;
        for (const auto& t : ldt_trips) {
            int trPurpose = t.trPurpose;
            // crossborder (DMA 10 <-> internal) employer-business -> commute
            bool cb = (t.org_DMA == 10 && t.des_DMA < 10) || (t.org_DMA < 10 && t.des_DMA == 10);
            if (cb && trPurpose == 5) trPurpose = 4;
            std::string purpose = purpose_ldt(trPurpose);

            std::string seg = t.vot;  // 6-class from stage A
            if (s.track_AirTours && t.trMode == 4) {
                seg = (t.type == "EI") ? "LDT_Vis_Air" : "LDT_Res_Air";
                double dist = lk.dist(t.otaz, t.dtaz);
                if (dist > 25.0) seg = "LDT_Air_AccEgr25M";
            }
            int s15 = t.period;                    // 15-min seg from stage A
            int hr = hour_from_seg(s15);
            od.add(hr, t.otaz, t.dtaz, seg, 1.0);  // each tour = 1 vehicle trip

            ListRow r;
            r.O = t.otaz; r.D = t.dtaz; r.valueOfTime = (double)(long long)t.trVOT;
            r.purpose = purpose; r.marketVot = seg;
            r.market = (seg == "LDT_Air_AccEgr25M" || seg == "LDT_Vis_Air" || seg == "LDT_Res_Air")
                           ? "LDT_Air" : strip_vot_suffix(seg);
            r.vehTrips = 1.0; r.occupancy = t.trPartySize; r.period = s.period_out(s15);
            r.hh_id = (t.trOState != 12) ? (MIN_HHID + (++ext_counter)) : t.hhId;
            r.person_id = 1; r.tour_id = 1; r.trip_id = 1;
            r.hhIncome = t.hhIncome; r.has_income = true;
            list.push_back(std::move(r));
        }
        std::printf("[eltod] processed %zu LDT trips\n", ldt_trips.size());
    }

    // ---------------- Trucks ----------------
    {
        CsvTable tk = load_csv(s.truck_odme);
        int cO = tk.require("O", s.truck_odme), cD = tk.require("D", s.truck_odme);
        int cH = tk.require("heavy", s.truck_odme), cL = tk.require("light", s.truck_odme);
        int cM = tk.require("medium", s.truck_odme);
        double trk_scale = 1.0 + (double)(s.yy() - s.truck_base_year) / 100.0;
        if (trk_scale < 1.0) trk_scale = 1.0;  // R only scales up

        struct TruckClass { const char* name; int col; const std::vector<double>* prob; };
        std::vector<TruckClass> classes = {
            {"heavy", cH, &lk.tod.ax5p}, {"medium", cM, &lk.tod.ax4}, {"light", cL, &lk.tod.ax3}};
        // truck VOT distribution params (R): heavy, medium, light
        const std::map<std::string, std::array<double, 3>> tparam = {
            // {sd, cost_coef, time_coef}
            {"heavy", {0.650, 2.88, 0.9}}, {"medium", {0.692, 5.42, 0.9}}, {"light", {0.896, 7.5, 0.7}}};

        long long trk_counter = 0;
        for (auto& c : classes) {
            auto disc = rng.make_disc(*c.prob);
            // Build the VOT pool for this class once (R seq(0,1,len=n) -> qnorm).
            // n here is the number of nonzero O-D cells for the class.
            std::vector<size_t> idx;
            for (size_t r = 0; r < tk.size(); ++r) if (tk.num(r, c.col) > 0) idx.push_back(r);
            size_t n = idx.size();
            const auto& tp = tparam.at(c.name);
            std::vector<double> vot_pool;
            vot_pool.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                double x = (n <= 1) ? 0.5 : (double)i / (double)(n - 1);
                double y = qnorm(x, tp[2], tp[0]);  // mean=time_coef, sd
                if (y > 0 && std::isfinite(y))
                    vot_pool.push_back(y * 60.0 * s.trk_axle_scale / tp[1]);
            }
            std::uniform_int_distribution<size_t> votpick(0, vot_pool.empty() ? 0 : vot_pool.size() - 1);

            for (size_t r : idx) {
                double veh = tk.num(r, c.col) * trk_scale;
                long long o = tk.ll(r, cO), d = tk.ll(r, cD);
                int s15 = lk.tod.seg96[rng.draw(disc)];
                int hr = hour_from_seg(s15);
                od.add(hr, o, d, c.name, veh);

                ListRow row;
                row.O = o; row.D = d; row.purpose = c.name;
                row.marketVot = c.name; row.market = "Truck";
                row.vehTrips = veh; row.occupancy = veh;  // R: occupancy := vehTrips for trucks
                row.period = s.period_out(s15);
                row.valueOfTime = vot_pool.empty() ? 0.0
                                 : (double)(long long)vot_pool[votpick(rng.engine())];
                row.hh_id = MIN_TRK_HHID + (++trk_counter);
                row.person_id = 1; row.tour_id = 1; row.trip_id = 1;
                list.push_back(std::move(row));
            }
        }
        std::printf("[eltod] processed trucks (scale=%.4f)\n", trk_scale);
    }

    // ---------------- External calibration ----------------
    od.calibrate_externals();
    for (auto& r : list) {
        double f = ext_scale(r.O, r.D);
        if (f != 1.0) r.vehTrips *= f;
    }

    // ---------------- Merge SDT household income onto resident rows ----------------
    if (!s.sdt_syn_hh.empty()) {
        CsvTable hh = load_csv(s.sdt_syn_hh);
        int cId = hh.require("household_id", s.sdt_syn_hh);
        int cInc = hh.require("HHINCADJ", s.sdt_syn_hh);
        std::unordered_map<long long, double> inc;
        for (size_t r = 0; r < hh.size(); ++r) inc[hh.ll(r, cId)] = hh.num(r, cInc);
        for (auto& r : list) {
            // residents are the rows whose market begins with "SDT_Res"
            if (r.market.rfind("SDT_Res", 0) == 0) {
                auto it = inc.find(r.hh_id);
                if (it != inc.end()) { r.hhIncome = it->second; r.has_income = true; }
            }
        }
    }

    // ---------------- Outputs ----------------
    const std::string out_od = s.scen("", "ELTOD_tt_HourClock.csv");
    od.write_csv(out_od, s.write_HourClock_format);
    std::printf("[eltod] wrote OD table -> %s\n", out_od.c_str());

    const std::string out_sdt = s.scen("", "ELTOD_SDT_Res_hourly.csv");
    sdt_res_od.write_csv(out_sdt, false);
    std::printf("[eltod] wrote SDT resident OD -> %s\n", out_sdt.c_str());

    // Primary deliverable: Hydra-schema gzipped trip list.
    const std::string out_list = s.scen(s.trip_table_out, "ELTOD_tt_List_hourly.csv.gz");
    GzWriter gz(out_list);
    if (!gz.good()) throw std::runtime_error("cannot open output: " + out_list);
    // `depart_time` is an HH:MM:SS clock (start of the period bin) — Hydra's
    // tsm_trip_reader treats this as its preferred clock departure field.
    gz.line("hh_id,person_id,tour_id,trip_id,valueOfTime,purpose,depart_time,O,D,"
            "marketVot,vehTrips,occupancy,hhIncome,market");
    std::string buf;
    buf.reserve(160);
    char num[64];
    for (const auto& r : list) {
        buf.clear();
        buf += std::to_string(r.hh_id); buf += ',';
        buf += std::to_string(r.person_id); buf += ',';
        buf += std::to_string(r.tour_id); buf += ',';
        buf += std::to_string(r.trip_id); buf += ',';
        std::snprintf(num, sizeof num, "%g", r.valueOfTime); buf += num; buf += ',';
        buf += r.purpose; buf += ',';
        {   // period bin index -> HH:MM:SS clock at the start of the bin
            int mins = r.period * s.output_resolution;
            std::snprintf(num, sizeof num, "%02d:%02d:%02d", mins / 60, mins % 60, 0);
            buf += num;
        }
        buf += ',';
        buf += std::to_string(r.O); buf += ',';
        buf += std::to_string(r.D); buf += ',';
        buf += r.marketVot; buf += ',';
        std::snprintf(num, sizeof num, "%g", r.vehTrips); buf += num; buf += ',';
        std::snprintf(num, sizeof num, "%g", r.occupancy); buf += num; buf += ',';
        if (r.has_income) { std::snprintf(num, sizeof num, "%g", r.hhIncome); buf += num; }
        buf += ',';
        buf += r.market;
        gz.line(buf);
    }
    gz.finish();
    std::printf("[eltod] wrote trip list (%zu rows) -> %s\n", list.size(), out_list.c_str());
}

} // namespace ap
