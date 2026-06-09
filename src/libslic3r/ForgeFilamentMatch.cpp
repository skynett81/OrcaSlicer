#include "ForgeFilamentMatch.hpp"

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

std::vector<std::string> tokenize(const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (std::isalnum(c)) cur.push_back((char)std::tolower(c));
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

} // namespace

std::string match_spool_to_filament_preset(const std::string&              material,
                                           const std::string&              vendor,
                                           const std::vector<std::string>& preset_names)
{
    const std::vector<std::string> mat_tokens    = tokenize(material);
    const std::vector<std::string> vendor_tokens = tokenize(vendor);
    if (mat_tokens.empty())
        return std::string();

    std::string best;
    int    best_vendor_bonus = -1;
    bool   best_basic        = false;
    size_t best_len          = 0;

    for (const std::string& name : preset_names) {
        const std::vector<std::string> nt_vec = tokenize(name);
        const std::set<std::string>    nt(nt_vec.begin(), nt_vec.end());

        bool mat_ok = true;
        for (const std::string& t : mat_tokens)
            if (nt.find(t) == nt.end()) { mat_ok = false; break; }
        if (!mat_ok)
            continue;

        int vendor_bonus = 0;
        for (const std::string& t : vendor_tokens)
            if (nt.find(t) != nt.end()) ++vendor_bonus;

        const std::string lname = lower(name);
        const bool   basic = lname.find("basic") != std::string::npos ||
                             lname.find("generic") != std::string::npos;
        const size_t len   = name.size();

        // Prefer: more vendor overlap, then a plain "basic"/generic variant, then
        // the shortest name (avoids CF/Silk/Matte/HF specialties when unsure).
        bool better = best.empty();
        if (!better) {
            if (vendor_bonus != best_vendor_bonus) better = vendor_bonus > best_vendor_bonus;
            else if (basic != best_basic)          better = basic && !best_basic;
            else                                   better = len < best_len;
        }
        if (better) {
            best              = name;
            best_vendor_bonus = vendor_bonus;
            best_basic        = basic;
            best_len          = len;
        }
    }
    return best;
}

} // namespace Slic3r
