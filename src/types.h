#pragma once
// Shared record types passed between the two pipeline stages.
#include <string>

namespace ap {

// One processed LDT trip (a row of stage-A `dt_trips_both`, both directions).
// Carried in memory into stage B (the R pipeline round-trips this through
// LD_tour_out_processed_ToD_Trips.csv, which stage A also writes for parity).
struct LdtTrip {
    long long otaz = 0, dtaz = 0;
    int period = 0;          // 15-min time segment 1..96 (stage A)
    int trMode = 1;          // 1 auto, 2 bus, 3 rail, 4 air
    std::string type;        // II / IE / EI / EE
    std::string vot;         // 6-class LDT segment from stage A step 6
    double trVOT = 0;
    int trPurpose = 0;
    long long hhId = 0;
    int trOState = 0;
    double hhIncome = 0;
    double trPartySize = 1;
    int org_DMA = 0, des_DMA = 0;
};

// One row of the unified Hydra trip list (14-column schema). Stage B fills this
// from SDT residents, SDT visitors, LDT, and trucks, then writes the gz.
struct ListRow {
    long long hh_id = 0;
    long long person_id = 1;
    long long tour_id = 1;
    long long trip_id = 1;
    double valueOfTime = 0;
    std::string purpose;
    int period = 0;          // 0-based 30-min period (0..47) — Hydra period_mode
    long long O = 0, D = 0;
    std::string marketVot;   // routing segment (purpose/market + _Low/_Med/_Hig)
    double vehTrips = 1;
    double occupancy = 1;
    double hhIncome = 0;
    bool has_income = false;  // R leaves hhIncome NA for non-resident markets
    std::string market;      // coarse market (marketVot with _Low/_Med/_Hig stripped)
};

} // namespace ap
