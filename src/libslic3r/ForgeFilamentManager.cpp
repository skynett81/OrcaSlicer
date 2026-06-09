#include "ForgeFilamentManager.hpp"
#include "ForgeFilamentMatch.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

double spool_percent_remaining(const ForgeSpool& s)
{
    if (s.remaining_g < 0.0 || s.initial_g <= 0.0)
        return -1.0;
    double pct = 100.0 * s.remaining_g / s.initial_g;
    if (pct < 0.0)   pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    return pct;
}

StockStatus spool_stock_status(const ForgeSpool& s, double low_threshold_frac)
{
    if (s.remaining_g < 0.0)
        return StockStatus::Unknown;
    if (s.remaining_g <= 0.0)
        return StockStatus::OutOfStock;
    if (s.initial_g > 0.0 && s.remaining_g / s.initial_g < low_threshold_frac)
        return StockStatus::Low;
    return StockStatus::OK;
}

std::vector<FilamentRow> build_filament_rows(const std::vector<ForgeSpool>& spools,
                                             double                         low_threshold_frac,
                                             bool                           include_archived)
{
    std::vector<FilamentRow> rows;
    rows.reserve(spools.size());
    for (const ForgeSpool& s : spools) {
        if (s.archived && !include_archived)
            continue;
        FilamentRow r;
        r.spool_id      = s.id;
        r.material      = s.material;
        r.vendor        = s.vendor;
        r.color_name    = s.color_name;
        r.color_hex     = s.color_hex;
        r.remaining_g   = s.remaining_g;
        r.initial_g     = s.initial_g;
        r.percent       = spool_percent_remaining(s);
        r.cost_per_gram = s.cost_per_gram();
        r.location      = s.location;
        r.status        = spool_stock_status(s, low_threshold_frac);
        rows.push_back(std::move(r));
    }

    std::stable_sort(rows.begin(), rows.end(), [](const FilamentRow& a, const FilamentRow& b) {
        const std::string am = lower(a.material), bm = lower(b.material);
        if (am != bm) return am < bm;
        const std::string av = lower(a.vendor), bv = lower(b.vendor);
        if (av != bv) return av < bv;
        // Remaining grams descending; unknown (-1) sorts last.
        const double ar = a.remaining_g < 0.0 ? -1.0 : a.remaining_g;
        const double br = b.remaining_g < 0.0 ? -1.0 : b.remaining_g;
        return ar > br;
    });
    return rows;
}

std::string resolve_spool_preset(const ForgeSpool&               s,
                                 const std::vector<std::string>& preset_names)
{
    if (!s.profile_name.empty()) {
        for (const std::string& name : preset_names)
            if (name == s.profile_name)
                return name;
    }
    return match_spool_to_filament_preset(s.material, s.vendor, preset_names);
}

} // namespace Slic3r
