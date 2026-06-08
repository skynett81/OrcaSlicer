#ifndef slic3r_ForgeSpool_hpp_
#define slic3r_ForgeSpool_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// A single filament spool as reported by the 3DPrintForge Server inventory
// (GET /api/inventory/spools). Only the fields the slicer needs for
// spool-aware filament picking and "not enough material" warnings are kept.
// Numeric fields are -1 when unknown; strings are empty when unknown.
struct ForgeSpool
{
    int         id           = -1;
    std::string material;          // e.g. "PLA"
    std::string color_name;        // e.g. "A00-B8"
    std::string color_hex;         // "RRGGBB", NO leading '#'
    double      remaining_g  = -1; // grams left on the spool
    double      initial_g    = -1; // grams when full
    double      cost         = -1; // total spool cost (currency-agnostic)
    double      density      = -1; // g/cm^3, for gram<->volume conversions
    std::string vendor;            // vendor display name
    std::string profile_name;      // filament profile name
    std::string location;          // e.g. "AMS2 PRO"
    std::string printer_id;        // owning printer id, if assigned
    int         ams_unit     = -1; // AMS unit index, if known
    int         ams_tray     = -1; // AMS tray/slot index, if known
    bool        archived     = false;

    // Estimated cost per gram, or -1 when it cannot be derived.
    double cost_per_gram() const
    {
        if (cost > 0.0 && initial_g > 0.0)
            return cost / initial_g;
        return -1.0;
    }
};

// The 3DPrintForge display currency (GET /api/currency).
struct ForgeCurrency
{
    std::string code;   // ISO code, e.g. "NOK", "USD" (empty when unknown)
    std::string symbol; // display symbol, e.g. "kr", "$" (empty when unknown)
};

// Parse the JSON body of GET /api/currency: { "active": "NOK", "supported":
// [{ "code": "NOK", "symbol": "kr", ... }, ...] }. Returns the active currency
// with its symbol resolved from the supported list. Empty fields on failure;
// never throws.
ForgeCurrency parse_forge_currency(const std::string& json_body);

// Parse the JSON body of GET /api/inventory/spools into ForgeSpool entries.
// Accepts a bare JSON array, or an object wrapping the array under "rows" or
// "spools". Malformed entries are skipped; a malformed body yields an empty
// vector (never throws).
std::vector<ForgeSpool> parse_forge_spools(const std::string& json_body);

} // namespace Slic3r

#endif
