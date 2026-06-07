#include "ForgeSpoolMatch.hpp"

#include <algorithm>
#include <cctype>

namespace Slic3r {

namespace {

std::string normalise_token(std::string s)
{
    if (!s.empty() && s.front() == '#')
        s.erase(s.begin());
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

std::vector<SpoolMatch> match_filaments_to_spools(const std::vector<FilamentNeed>& needs,
                                                  const std::vector<ForgeSpool>&   spools)
{
    std::vector<SpoolMatch> out;
    out.reserve(needs.size());

    for (size_t i = 0; i < needs.size(); ++i) {
        const FilamentNeed& need = needs[i];
        SpoolMatch m;
        m.filament_index = static_cast<int>(i);
        m.needed_g       = need.needed_g;

        const std::string want_hex = normalise_token(need.color_hex);
        const std::string want_mat = normalise_token(need.material);

        double cost_accum_g = 0.0; // sum of remaining grams that carry a price
        double cost_accum   = 0.0; // sum of cost_per_gram * remaining for those
        for (const ForgeSpool& sp : spools) {
            if (sp.archived)
                continue;
            if (want_hex.empty() || normalise_token(sp.color_hex) != want_hex)
                continue;
            if (!want_mat.empty() && normalise_token(sp.material) != want_mat)
                continue;
            m.matched = true;
            m.spool_ids.push_back(sp.id);
            if (sp.remaining_g > 0.0)
                m.available_g += sp.remaining_g;
            const double cpg = sp.cost_per_gram();
            if (cpg >= 0.0 && sp.remaining_g > 0.0) {
                cost_accum   += cpg * sp.remaining_g;
                cost_accum_g += sp.remaining_g;
            }
        }

        if (m.matched) {
            m.deficit_g  = std::max(0.0, m.needed_g - m.available_g);
            m.sufficient = m.available_g >= m.needed_g;
            if (cost_accum_g > 0.0)
                m.cost_per_gram = cost_accum / cost_accum_g;
        }
        out.push_back(std::move(m));
    }

    return out;
}

} // namespace Slic3r
