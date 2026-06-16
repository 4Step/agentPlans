#pragma once
// Stage A — port of 2_Create_LDT_TripTable_GA_AL_template.R
// Builds the long-distance (LDT) trip list with DMAs, external stations,
// airport access/egress, cruise trips, VOT classes, reverse direction and
// time-of-day. Returns the both-directions trip set (dt_trips_both) for stage B.
#include <vector>

#include "lookups.h"
#include "settings.h"
#include "types.h"
#include "util/rng.h"

namespace ap {

std::vector<LdtTrip> run_stage_ldt(const Settings& s, const Lookups& lk, Rng& rng);

} // namespace ap
