#include "ForgePrinterMatch.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace Slic3r {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Split into lowercase alphanumeric tokens ("Bambu Lab P2S 0.4" -> bambu lab p2s 0 4).
std::vector<std::string> tokenize(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (std::isalnum(c)) {
            cur.push_back((char)std::tolower(c));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

} // namespace

std::string match_fleet_printer_preset(const std::string&              vendor,
                                       const std::string&              model,
                                       const std::vector<std::string>& preset_names)
{
    const std::vector<std::string> model_tokens  = tokenize(model);
    const std::vector<std::string> vendor_tokens = tokenize(vendor);
    if (model_tokens.empty())
        return std::string();

    std::string best;
    int    best_vendor_bonus = -1;
    bool   best_has_04       = false;
    size_t best_len          = 0;

    for (const std::string& name : preset_names) {
        const std::vector<std::string> nt_vec = tokenize(name);
        const std::set<std::string>    nt(nt_vec.begin(), nt_vec.end());

        // Require every model token to appear in the preset name.
        bool model_ok = true;
        for (const std::string& t : model_tokens)
            if (nt.find(t) == nt.end()) { model_ok = false; break; }
        if (!model_ok)
            continue;

        int vendor_bonus = 0;
        for (const std::string& t : vendor_tokens)
            if (nt.find(t) != nt.end()) ++vendor_bonus;

        const bool   has_04 = lower(name).find("0.4") != std::string::npos;
        const size_t len    = name.size();

        // Prefer: more vendor overlap, then a 0.4 nozzle, then the shortest name.
        bool better = best.empty();
        if (!better) {
            if (vendor_bonus != best_vendor_bonus)      better = vendor_bonus > best_vendor_bonus;
            else if (has_04 != best_has_04)             better = has_04 && !best_has_04;
            else                                        better = len < best_len;
        }
        if (better) {
            best              = name;
            best_vendor_bonus = vendor_bonus;
            best_has_04       = has_04;
            best_len          = len;
        }
    }
    return best;
}

} // namespace Slic3r
