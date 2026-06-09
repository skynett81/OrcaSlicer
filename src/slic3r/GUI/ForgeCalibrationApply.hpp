#ifndef slic3r_GUI_ForgeCalibrationApply_hpp_
#define slic3r_GUI_ForgeCalibrationApply_hpp_

#include <string>

#include "libslic3r/ForgeCalibration.hpp"

namespace Slic3r { namespace GUI {

// Shared helpers behind both the Fleet Calibration dialog and the Calibration-tab
// page. They read/write the active printer + filament so the calibration memory
// is wired to the real presets, brand-agnostically.
struct ForgeCaliContext
{
    std::string printer;   // printer preset name (the record key)
    std::string material;  // filament_type[0]
    std::string vendor;    // filament_vendor[0]
    double      nozzle = -1;
};

// Resolve the active printer/filament/nozzle from the preset bundle.
ForgeCaliContext forge_current_calibration_context();

// Build a calibration record from the current filament preset's flow ratio /
// pressure advance / max volumetric speed, stamped with today's date.
ForgeCalibrationRecord forge_capture_current_calibration(const ForgeCaliContext& ctx);

// Apply a record's values onto the current filament preset via the filament Tab.
// Returns false when the filament settings are unavailable.
bool forge_apply_calibration(const ForgeCalibrationRecord& rec);

// One-line "Flow 0.980   PA 0.0200   Max vol 12.0 mm3/s" summary; "(no values)"
// when nothing is set.
std::string forge_calibration_summary(const ForgeCalibrationRecord& rec);

}} // namespace Slic3r::GUI

#endif
