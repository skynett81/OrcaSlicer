#ifndef slic3r_ForgeFilamentManager_hpp_
#define slic3r_ForgeFilamentManager_hpp_

#include <string>
#include <vector>

#include "ForgeSpool.hpp"

namespace Slic3r {

// Stock status of a physical spool, derived from its remaining vs. initial
// weight. This is what makes the 3DPrintForge "Filament Manager" richer than a
// plain preset library: we track the real, owned spools (weight, cost, location)
// — not just filament definitions.
enum class StockStatus { Unknown, OutOfStock, Low, OK };

// Percent of the spool remaining (0..100), or -1 when it cannot be derived.
double spool_percent_remaining(const ForgeSpool& s);

// Stock status. low_threshold_frac is the fraction-of-initial below which a
// spool counts as Low (e.g. 0.10 = 10%). Unknown when remaining is unknown.
StockStatus spool_stock_status(const ForgeSpool& s, double low_threshold_frac);

// One display row for the Filament Manager view, with everything the GUI needs
// already resolved (no further computation in the widget).
struct FilamentRow
{
    int         spool_id     = -1;
    std::string material;
    std::string vendor;
    std::string color_name;
    std::string color_hex;        // "RRGGBB", NO leading '#'
    double      remaining_g   = -1;
    double      initial_g     = -1;
    double      percent       = -1; // 0..100, -1 unknown
    double      cost_per_gram = -1; // -1 unknown
    std::string location;
    StockStatus status        = StockStatus::Unknown;
};

// Build display rows from a raw spool list. Archived spools are dropped unless
// include_archived is true. Rows are sorted by material, then vendor (both
// case-insensitive), then by remaining grams descending (unknown grams last).
std::vector<FilamentRow> build_filament_rows(const std::vector<ForgeSpool>& spools,
                                             double                         low_threshold_frac,
                                             bool                           include_archived);

// Resolve the filament preset to use for a spool: prefer the spool's explicit
// profile_name when it is present among preset_names; otherwise fall back to
// material+vendor token matching (match_spool_to_filament_preset). Returns ""
// when nothing matches.
std::string resolve_spool_preset(const ForgeSpool&               s,
                                 const std::vector<std::string>& preset_names);

} // namespace Slic3r

#endif
