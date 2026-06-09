#ifndef slic3r_ForgePrinterMatch_hpp_
#define slic3r_ForgePrinterMatch_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// Map a 3DPrintForge dashboard fleet printer onto an installed slicer printer
// preset, so connected printers can appear under "Printer".
//
// The dashboard's `vendor` field is the CONNECTOR type ("bambu", "moonraker",
// …) — NOT the brand — so matching keys on the MODEL string (e.g. "P2S",
// "Snapmaker U1"); vendor is only a soft bonus. Returns the best-matching preset
// name, or "" when no preset contains all the model's tokens. Among candidates,
// prefers more vendor-token overlap, then a 0.4 nozzle, then the shortest name
// (the base variant).
std::string match_fleet_printer_preset(const std::string&              vendor,
                                       const std::string&              model,
                                       const std::vector<std::string>& preset_names);

} // namespace Slic3r

#endif
