#include "lookups.h"

#include <cstdio>

#include "util/csv.h"

namespace ap {

void Lookups::load_taz_dma(const Settings& s) {
    CsvTable t = load_csv(s.taz_dma);
    int cT = t.require("TAZ", s.taz_dma);
    int cD = t.require("DMA", s.taz_dma);
    for (size_t r = 0; r < t.size(); ++r) taz_dma[t.ll(r, cT)] = static_cast<int>(t.ll(r, cD));

    // GA/AL crossborder destinations -> DMA 10 (R: mutate(DMA = 10)).
    if (!s.ga_al_destinations.empty()) {
        CsvTable g = load_csv(s.ga_al_destinations);
        int gT = g.require("TAZ", s.ga_al_destinations);
        for (size_t r = 0; r < g.size(); ++r) taz_dma[g.ll(r, gT)] = 10;
    }
}

void Lookups::load_external_auto(const Settings& s) {
    CsvTable t = load_csv(s.external_auto_shares);
    int cC = t.require("CBM", s.external_auto_shares);
    int cTaz = t.require("NextGen_TAZ", s.external_auto_shares);
    int cSh = t.require("share", s.external_auto_shares);
    for (size_t r = 0; r < t.size(); ++r) {
        int cbm = static_cast<int>(t.ll(r, cC));
        auto& w = ext_auto_by_cbm[cbm];
        w.values.push_back(static_cast<int>(t.ll(r, cTaz)));
        w.probs.push_back(t.num(r, cSh));
    }
}

void Lookups::load_airport(const Settings& s) {
    CsvTable t = load_csv(s.airport_shares);
    int cD = t.require("DMA", s.airport_shares);
    int cTaz = t.require("NextGen_TAZ", s.airport_shares);
    int cSh = t.require("share", s.airport_shares);
    for (size_t r = 0; r < t.size(); ++r) {
        int dma = static_cast<int>(t.ll(r, cD));
        auto& w = airport_by_dma[dma];
        w.values.push_back(static_cast<int>(t.ll(r, cTaz)));
        w.probs.push_back(t.num(r, cSh));
    }
    // R guard row: rbindlist(err_row = list("Airport", 3, 7536, 0.01)).
    {
        auto& w = airport_by_dma[3];
        w.values.push_back(7536);
        w.probs.push_back(0.01);
    }
    // Disney DMA 9 == Central FL DMA 5 (R duplicates DMA 5 rows as DMA 9).
    airport_by_dma[9] = airport_by_dma[5];
}

void Lookups::load_cruise(const Settings& s) {
    CsvTable t = load_csv(s.canaveral_cruise);
    int cTaz = t.require("NextGen_TAZ", s.canaveral_cruise);
    int cSh = t.require("HotelRoom_Share", s.canaveral_cruise);
    for (size_t r = 0; r < t.size(); ++r) {
        cruise.values.push_back(static_cast<int>(t.ll(r, cTaz)));
        cruise.probs.push_back(t.num(r, cSh));
    }
}

void Lookups::load_cbm_ext(const Settings& s) {
    if (s.cbm_external_lookup_csv.empty()) return;
    CsvTable t = load_csv(s.cbm_external_lookup_csv);
    int cT = t.require("TAZ", s.cbm_external_lookup_csv);
    int cE = t.require("CBM_Ext", s.cbm_external_lookup_csv);
    for (size_t r = 0; r < t.size(); ++r) cbm_ext[t.ll(r, cT)] = t.ll(r, cE);
}

void Lookups::load_tod(const Settings& s) {
    CsvTable t = load_csv(s.tod_distributions);
    int cSeg = t.require("TimeSeg_96", s.tod_distributions);
    int cRl = t.col("Res_Long"), cVl = t.col("Vis_Long");
    int c3 = t.col("3_Axles"), c4 = t.col("4_Axles"), c5 = t.col("5p_Axles");
    for (size_t r = 0; r < t.size(); ++r) {
        tod.seg96.push_back(static_cast<int>(t.ll(r, cSeg)));
        tod.res_long.push_back(t.num(r, cRl));
        tod.vis_long.push_back(t.num(r, cVl));
        tod.ax3.push_back(t.num(r, c3));
        tod.ax4.push_back(t.num(r, c4));
        tod.ax5p.push_back(t.num(r, c5));
    }
}

void Lookups::load_skim(const Settings& s) {
    if (skim_loaded || s.distance_skim.empty()) return;
    CsvTable t = load_csv(s.distance_skim);
    // R reads the first three columns positionally: O, D, distance.
    if (t.header.names.size() < 3)
        throw std::runtime_error(s.distance_skim + ": expected >=3 columns");
    for (size_t r = 0; r < t.size(); ++r) {
        long long o = to_ll(t.at(r, 0));
        long long d = to_ll(t.at(r, 1));
        skim[o * SKIM_MUL + d] = to_double(t.at(r, 2));
    }
    skim_loaded = true;
    std::printf("[lookups] distance skim: %zu O-D pairs\n", skim.size());
}

} // namespace ap
