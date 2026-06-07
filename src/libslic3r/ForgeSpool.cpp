#include "ForgeSpool.hpp"

#include <nlohmann/json.hpp>

namespace Slic3r {

using json = nlohmann::json;

namespace {

std::string str_or(const json& j, const char* key, const std::string& dflt = "")
{
    auto it = j.find(key);
    if (it != j.end() && it->is_string())
        return it->get<std::string>();
    return dflt;
}

double num_or(const json& j, const char* key, double dflt = -1.0)
{
    auto it = j.find(key);
    if (it != j.end() && it->is_number())
        return it->get<double>();
    return dflt;
}

int int_or(const json& j, const char* key, int dflt = -1)
{
    auto it = j.find(key);
    if (it != j.end() && it->is_number())
        return static_cast<int>(it->get<double>());
    return dflt;
}

// 3DPrintForge stores hex without a leading '#'; normalise just in case an
// override carried one through.
std::string normalise_hex(std::string hex)
{
    if (!hex.empty() && hex.front() == '#')
        hex.erase(hex.begin());
    return hex;
}

} // namespace

std::vector<ForgeSpool> parse_forge_spools(const std::string& json_body)
{
    std::vector<ForgeSpool> out;
    try {
        json j = json::parse(json_body);
        const json* arr = nullptr;
        if (j.is_array())
            arr = &j;
        else if (j.is_object() && j.contains("rows") && j["rows"].is_array())
            arr = &j["rows"];
        else if (j.is_object() && j.contains("spools") && j["spools"].is_array())
            arr = &j["spools"];
        if (arr == nullptr)
            return out;

        out.reserve(arr->size());
        for (const auto& s : *arr) {
            if (!s.is_object())
                continue;
            ForgeSpool sp;
            sp.id           = int_or(s, "id");
            sp.material     = str_or(s, "material");
            sp.color_name   = str_or(s, "color_name");
            sp.color_hex    = normalise_hex(str_or(s, "color_hex"));
            sp.remaining_g  = num_or(s, "remaining_weight_g");
            sp.initial_g    = num_or(s, "initial_weight_g");
            sp.cost         = num_or(s, "cost");
            sp.density      = num_or(s, "density");
            sp.vendor       = str_or(s, "vendor_name");
            sp.profile_name = str_or(s, "profile_name");
            sp.location     = str_or(s, "location");
            sp.printer_id   = str_or(s, "printer_id");
            sp.ams_unit     = int_or(s, "ams_unit");
            sp.ams_tray     = int_or(s, "ams_tray");
            sp.archived     = int_or(s, "archived", 0) != 0;
            out.push_back(std::move(sp));
        }
    } catch (const std::exception&) {
        // Malformed body -> empty list, never throw.
        return {};
    }
    return out;
}

} // namespace Slic3r
