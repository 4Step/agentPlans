#pragma once
// Stage B — port of 3_get_ELTOD_TripTable_template.R
// Combines SDT residents + SDT visitors + LDT + trucks into the unified Hydra
// trip list (ELTOD_tt_List_hourly.csv.gz) and the ELToD OD trip table.
#include <vector>

#include "lookups.h"
#include "settings.h"
#include "types.h"
#include "util/rng.h"

namespace ap {

void run_stage_eltod(const Settings& s, const Lookups& lk, Rng& rng,
                     const std::vector<LdtTrip>& ldt_trips);

} // namespace ap
