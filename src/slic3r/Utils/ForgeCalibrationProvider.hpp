#ifndef slic3r_ForgeCalibrationProvider_hpp_
#define slic3r_ForgeCalibrationProvider_hpp_

#include <string>
#include <vector>

#include "libslic3r/ForgeCalibration.hpp"

namespace Slic3r {

// Persistence + sync for Forge calibration records. The slicer keeps a local
// cache (so calibration memory works fully standalone, per-machine) and — when
// an inventory/dashboard server is configured — best-effort pulls/pushes records
// so the whole 3DPrintForge fleet shares one memory. All functions are
// non-throwing and degrade gracefully when nothing is configured/reachable.

// Local cache file path: <data_dir>/forge_calibration.json.
std::string forge_calibration_cache_path();

// Load records from the local cache, merged with a best-effort dashboard pull
// (dashboard records win on identity collisions). Performs a blocking network
// request — do NOT call on the UI thread in a hot path. Never throws.
std::vector<ForgeCalibrationRecord> load_calibration_records();

// Load ONLY the local cache (no network). Instant and UI-thread safe — use this
// for refresh-on-show; reserve load_calibration_records() for explicit sync.
std::vector<ForgeCalibrationRecord> load_cached_calibration_records();

// Upsert one record into the local cache and persist it, then best-effort push
// it to the dashboard. Returns true when the local write succeeded.
bool save_calibration_record(const ForgeCalibrationRecord& rec);

} // namespace Slic3r

#endif
