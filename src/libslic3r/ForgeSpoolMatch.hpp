#ifndef slic3r_ForgeSpoolMatch_hpp_
#define slic3r_ForgeSpoolMatch_hpp_

#include <string>
#include <vector>

#include "ForgeSpool.hpp"

namespace Slic3r {

// What a single plate filament needs from inventory.
struct FilamentNeed
{
    std::string color_hex; // "RRGGBB" (with or without '#'); matched case-insensitively
    std::string material;  // e.g. "PLA"; empty = don't constrain by material
    double      needed_g = 0.0;
};

// Result of matching one filament against the 3DPrintForge spool inventory.
struct SpoolMatch
{
    int                 filament_index = -1;
    double              needed_g       = 0.0;
    // Sum of remaining grams across all active spools matching colour (+material).
    double              available_g    = 0.0;
    bool                matched        = false; // at least one inventory spool matched
    // true only when matched AND available_g >= needed_g. When unmatched we cannot
    // prove sufficiency, so this is false and the UI should say "unknown", not "ok".
    bool                sufficient     = false;
    // max(0, needed_g - available_g) when matched, else 0.
    double              deficit_g      = 0.0;
    // Remaining-weighted average cost per gram across matched spools that report
    // a price, or -1 when no matched spool has a known cost. Lets callers price
    // this filament's usage (e.g. purge waste) from real inventory cost.
    double              cost_per_gram  = -1.0;
    std::vector<int>    spool_ids;             // ids of the matched spools
};

// Match each filament need against the spool inventory. A spool matches when its
// colour hex equals the need's (case-insensitive) and, if the need specifies a
// material, the materials match (case-insensitive). Archived spools are ignored.
// Remaining grams are summed across all matching spools (AMS lets you continue
// from another spool of the same colour), so sufficiency reflects total stock.
std::vector<SpoolMatch> match_filaments_to_spools(const std::vector<FilamentNeed>& needs,
                                                  const std::vector<ForgeSpool>&   spools);

} // namespace Slic3r

#endif
