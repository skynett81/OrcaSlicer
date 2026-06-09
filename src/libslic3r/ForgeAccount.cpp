#include "ForgeAccount.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

// Pull a string field from a parsed JSON object, tolerant of types/missing.
std::string json_str(const nlohmann::json& j, const char* key)
{
    if (j.is_object() && j.contains(key) && j[key].is_string())
        return j[key].get<std::string>();
    return std::string();
}

bool json_true(const nlohmann::json& j, const char* key)
{
    return j.is_object() && j.contains(key) && j[key].is_boolean() && j[key].get<bool>();
}

} // namespace

ForgeLoginOutcome interpret_login_response(int http_status, const std::string& body)
{
    ForgeLoginOutcome out;

    nlohmann::json j;
    bool parsed = false;
    try {
        j = nlohmann::json::parse(body);
        parsed = true;
    } catch (...) {
        parsed = false;
    }

    if (http_status == 200) {
        out.status = ForgeLoginStatus::Success;
        return out;
    }
    if (http_status == 401) {
        if (parsed && json_true(j, "totpRequired")) {
            out.status = ForgeLoginStatus::TotpRequired;
            out.error  = parsed ? json_str(j, "error") : std::string();
            return out;
        }
        out.status = ForgeLoginStatus::BadCredentials;
        out.error  = parsed ? json_str(j, "error") : std::string("Invalid credentials");
        return out;
    }
    if (http_status == 429) {
        out.status = ForgeLoginStatus::RateLimited;
        out.error  = parsed ? json_str(j, "error") : std::string("Too many attempts");
        return out;
    }
    if (http_status >= 500) {
        out.status = ForgeLoginStatus::ServerError;
        out.error  = parsed ? json_str(j, "error") : std::string("Server error");
        return out;
    }

    out.status = ForgeLoginStatus::Unknown;
    out.error  = parsed ? json_str(j, "error") : std::string();
    return out;
}

std::string extract_session_cookie(const std::string& set_cookie_header)
{
    static const std::string key = "bambu_session=";
    const std::size_t start = set_cookie_header.find(key);
    if (start == std::string::npos)
        return std::string();
    const std::size_t val_begin = start + key.size();
    std::size_t val_end = set_cookie_header.find(';', val_begin);
    if (val_end == std::string::npos)
        val_end = set_cookie_header.size();
    std::string token = set_cookie_header.substr(val_begin, val_end - val_begin);
    // Trim surrounding whitespace.
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    token.erase(token.begin(), std::find_if(token.begin(), token.end(), not_space));
    token.erase(std::find_if(token.rbegin(), token.rend(), not_space).base(), token.end());
    return token;
}

} // namespace Slic3r
