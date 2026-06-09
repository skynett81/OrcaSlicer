#ifndef slic3r_ForgeCalibration_hpp_
#define slic3r_ForgeCalibration_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// A saved calibration result, tied to a fleet printer + filament (and optionally
// a specific physical spool). This is the "tailored to our system" part: it is
// brand-agnostic — keyed by printer_id/material/vendor — so the WHOLE 3DPrintForge
// fleet (Bambu, Snapmaker, Moonraker, Prusa, ...) shares one calibration memory,
// not just Bambu-cloud printers. Records persist in the dashboard (fleet memory)
// and can be bound to a physical spool (per-spool calibration).
//
// Numeric calibration fields are -1 when "not calibrated for this field".
struct ForgeCalibrationRecord
{
    std::string printer_id;            // fleet printer id (required)
    std::string material;              // e.g. "PLA" (required)
    std::string vendor;                // filament vendor (optional)
    int         spool_id  = -1;        // physical spool id, when per-spool (-1 = none)
    double      nozzle_mm = -1;        // nozzle diameter, e.g. 0.4 (-1 = unknown)

    double      flow_ratio           = -1; // e.g. 0.98
    double      pressure_advance     = -1; // e.g. 0.020 (0 is a valid calibrated value)
    double      max_volumetric_speed = -1; // mm^3/s

    std::string updated_at;            // ISO date string, supplied by the caller
    std::string source;                // "manual" | "wizard" | "import" | ...

    bool has_flow() const { return flow_ratio > 0.0; }
    bool has_pa()   const { return pressure_advance >= 0.0; }
    bool has_mvs()  const { return max_volumetric_speed > 0.0; }
    bool has_any()  const { return has_flow() || has_pa() || has_mvs(); }
};

// Find the best-matching calibration for a target, by specificity (highest
// first):
//   1. exact spool_id (with matching printer_id), when target_spool_id >= 0
//   2. printer_id + material + vendor + nozzle
//   3. printer_id + material + nozzle   (vendor ignored)
//   4. printer_id + material            (nozzle/vendor ignored)
// Material match is case-insensitive and required. A record with unknown nozzle
// (-1) is treated as a wildcard on nozzle. Returns the index into `records`, or
// -1 when nothing matches.
int find_best_calibration(const std::vector<ForgeCalibrationRecord>& records,
                          const std::string&                         printer_id,
                          int                                        target_spool_id,
                          const std::string&                         material,
                          const std::string&                         vendor,
                          double                                     nozzle_mm);

// Insert-or-replace by identity (printer_id, spool_id, material, vendor,
// nozzle_mm). A record with the same identity is replaced in place; otherwise
// the record is appended. Returns a new vector (does not mutate the input).
std::vector<ForgeCalibrationRecord> upsert_calibration(std::vector<ForgeCalibrationRecord> records,
                                                        const ForgeCalibrationRecord&       rec);

// Parse a JSON array of calibration records (the body of the dashboard's
// calibration endpoint, or a local cache file). Accepts a bare array or an
// object wrapping the array under "records". Malformed entries are skipped; a
// malformed body yields an empty vector. Never throws.
std::vector<ForgeCalibrationRecord> parse_calibration_records(const std::string& json_body);

// Serialize records to a compact JSON array (for POSTing to the dashboard or
// writing the local cache). Only fields that are set are emitted.
std::string serialize_calibration_records(const std::vector<ForgeCalibrationRecord>& records);

} // namespace Slic3r

#endif
