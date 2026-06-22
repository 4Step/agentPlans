# agentPlans

C++17 port of the two TSM "trip-list" R scripts from the QGIS `tsm_panel` plugin:

| R script (canonical source) | agentPlans stage |
|---|---|
| `Rscripts/2_Create_LDT_TripTable_GA_AL_template.R` | `ldt` |
| `Rscripts/3_get_ELTOD_TripTable_template.R` | `eltod` |

Canonical R source:
`C:/Users/<user>/AppData/Roaming/QGIS/QGIS3/profiles/default/python/plugins/tsm_panel/Rscripts/`

The two scripts build the long-distance (LDT) trip list and then combine it
with the SDT resident/visitor trip lists and the truck ODME table into the
**ELToD trip list** that Hydra (AgentFlow DTA) consumes. agentPlans runs the
**full pipeline from a single control file** and writes the trip list as a
gzipped CSV in the exact Hydra schema.

## Build

Needs MSVC (VS18 / VS 2026) + Ninja + CMake. No external dependencies — gzip
I/O uses vendored miniz (the same library Hydra uses).

```
scripts\build.bat
```
Produces `build\agentPlans.exe`.

## Run

```
build\agentPlans.exe config\agentplans_settings.txt
```

## Inputs

All inputs are **plain CSV**. The original Excel lookups must be exported to CSV
once — see [docs/CSV_INPUTS.md](docs/CSV_INPUTS.md) for the exact schema of every
file (column names are matched by header). Paths are set in the control file
(`config/agentplans_settings.txt`).

## Outputs (written under `scenario_dir`)

| file | meaning |
|---|---|
| **`ELTOD_tt_List_hourly.csv.gz`** | **Hydra trip list — primary deliverable** |
| `ELTOD_tt_HourClock.csv` | combined ELToD OD trip table (wide, by market) |
| `ELTOD_SDT_Res_hourly.csv` | SDT resident purpose×VOT OD table |

The trip-list gz uses the 14-column Hydra schema, in order:

```
hh_id,person_id,tour_id,trip_id,valueOfTime,purpose,depart_time,O,D,marketVot,vehTrips,occupancy,hhIncome,market
```

`depart_time` is an **HH:MM:SS** clock (start of the period bin) — Hydra's
`tsm_trip_reader` reads this as its preferred clock departure field. Bin width
follows `output_resolution` (30-min → :00/:30, 15-min → :00/:15/:30/:45).

## Fidelity notes (read before comparing to R output)

- **RNG:** R's `sample()`/`set.seed()` stream cannot be reproduced bit-for-bit
  in C++. Stochastic assignments (external stations, airport zones/modes,
  time-of-day, truck periods, truck VOT) are statistically equivalent and
  reproducible for a fixed `seed`, but individual rows differ. Aggregate trip
  tables match within sampling noise.
- **`period` column / temporal resolution:** every source is brought onto a
  common internal 15-min grid (LDT and trucks are *daily* and get timed via the
  15-min ToD share table; SDT 30-min periods are split to 15-min), then the
  output `period` is collapsed to `output_resolution`. The bins are declared in
  the control file:

  | setting | meaning | values |
  |---|---|---|
  | `output_resolution` | trip-list `depart_time` bin width | `15` or `30` (minutes) |
  | `sdt_input_resolution` | SDT trip-list native bin | `15` or `30` |
  | `ldt_input_resolution` | LDT native bin | `daily` |
  | `truck_input_resolution` | truck ODME native bin | `daily` |

  The trip list's `depart_time` is an **HH:MM:SS** clock at the start of the
  output bin, so Hydra's `tsm_trip_reader` reads it directly (its preferred
  clock field). The R script wrote a 1..96 15-minute integer index instead;
  agentPlans makes the resolution explicit and emits a real clock. Hourly OD
  tables always use the 15-min segment → clock-hour mapping as R.
- **LDT/truck rows:** the R script collapses LDT tours with `.N` (count) into
  `vehTrips`; agentPlans emits one list row per tour with `vehTrips = 1`
  (identical totals, more rows — natural for a trip list). Truck rows are one
  per O-D-class with `vehTrips` = the (scaled) matrix value.
- **Not ported:** `LimitCounties` (needs `hhID_by_county.csv`), `telework`
  (needs `person_Industry_type.csv`), and the TRANSIMS export are guarded off;
  `LimitCounties=true` raises an error. Pure diagnostic `dcast`/print summaries
  from the R scripts are omitted.

## Layout

```
src/util/   csv, gzip read/write (miniz), RNG + qnorm
src/        settings (control file), lookups, stage_ldt, stage_eltod, main
config/     control-file template
docs/       CSV input schemas
third_party/miniz   vendored gzip codec
```
