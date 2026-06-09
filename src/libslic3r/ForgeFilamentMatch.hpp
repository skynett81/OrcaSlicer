#ifndef slic3r_ForgeFilamentMatch_hpp_
#define slic3r_ForgeFilamentMatch_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// Map a 3DPrintForge spool (material + optional vendor) onto an installed
// filament preset, for syncing the slicer's filament setup to what's actually
// loaded. Requires the material to match (e.g. "PLA", "PETG"); prefers vendor
// overlap, then a plain/"basic" variant, then the shortest preset name. Returns
// the best-matching preset name, or "" when no preset matches the material.
//
// (Material is the reliable key; the exact branded variant is a best guess — the
// spool's COLOUR is applied separately and exactly.)
std::string match_spool_to_filament_preset(const std::string&              material,
                                           const std::string&              vendor,
                                           const std::vector<std::string>& preset_names);

} // namespace Slic3r

#endif
