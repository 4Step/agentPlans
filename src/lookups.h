#pragma once
// Loads the converted-from-xlsx lookup tables and the already-CSV reference
// tables into in-memory structures used by both pipeline stages.
#include <string>
#include <unordered_map>
#include <vector>

#include "settings.h"

namespace ap {

// Parallel values/weights for R-style sample(values, n, prob, replace=TRUE).
struct WeightedChoices {
    std::vector<int> values;
    std::vector<double> probs;
    bool empty() const { return values.empty(); }
};

struct ToD {
    std::vector<int> seg96;       // TimeSeg_96 (1..96)
    std::vector<double> res_long, vis_long;
    std::vector<double> ax3, ax4, ax5p;  // 3_Axles, 4_Axles, 5p_Axles
};

struct Lookups {
    // TAZ -> DMA (Florida zones + GA/AL crossborder destinations tagged DMA 10).
    std::unordered_map<long long, int> taz_dma;

    // External auto shares grouped by CBM code (0,1,2,3).
    std::unordered_map<int, WeightedChoices> ext_auto_by_cbm;

    // Airport shares by DMA (1..9; DMA 9 duplicated from DMA 5; guard row added).
    std::unordered_map<int, WeightedChoices> airport_by_dma;

    // Cape Canaveral cruise hotel-room origins.
    WeightedChoices cruise;

    // CBM external lookup: internal TAZ -> external-station TAZ.
    std::unordered_map<long long, long long> cbm_ext;

    ToD tod;

    // Round-trip distance skim: (O,D) -> miles. Loaded only when needed.
    std::unordered_map<long long, double> skim;  // key = O * SKIM_MUL + D
    bool skim_loaded = false;

    int dma_for(long long taz) const {
        auto it = taz_dma.find(taz);
        return it == taz_dma.end() ? 0 : it->second;
    }

    static constexpr long long SKIM_MUL = 100000;
    double dist(long long o, long long d) const {
        auto it = skim.find(o * SKIM_MUL + d);
        return it == skim.end() ? -1.0 : it->second;
    }

    // Loaders.
    void load_taz_dma(const Settings& s);
    void load_external_auto(const Settings& s);
    void load_airport(const Settings& s);
    void load_cruise(const Settings& s);
    void load_cbm_ext(const Settings& s);
    void load_tod(const Settings& s);
    void load_skim(const Settings& s);
};

} // namespace ap
