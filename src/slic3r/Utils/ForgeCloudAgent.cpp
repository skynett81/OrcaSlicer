#include "ForgeCloudAgent.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>

#include <regex>

namespace Slic3r {

using json = nlohmann::json;

namespace {

// Parse a server URL like "http://192.168.1.10:3000" into host + port +
// scheme. cpp-httplib needs scheme stripped off when using Client.
struct ParsedUrl {
    std::string host;
    int         port = 0;
    bool        ssl  = false;
};

ParsedUrl parse_url(const std::string& url) {
    ParsedUrl p;
    std::regex re(R"(^(https?)://([^:/]+)(?::(\d+))?)");
    std::smatch m;
    if (!std::regex_search(url, m, re)) {
        p.host = url;
        p.port = 3000;
        return p;
    }
    p.ssl  = (m[1].str() == "https");
    p.host = m[2].str();
    p.port = m[3].matched ? std::stoi(m[3].str()) : (p.ssl ? 443 : 80);
    return p;
}

// Wraps cpp-httplib Client setup so the rest of the code is symmetric
// across http and https.
std::unique_ptr<httplib::Client> make_client(const std::string& server_url) {
    auto parsed = parse_url(server_url);
    auto cli = std::make_unique<httplib::Client>(parsed.host, parsed.port);
    cli->set_connection_timeout(3, 0);
    cli->set_read_timeout(8, 0);
    cli->set_write_timeout(8, 0);
    return cli;
}

httplib::Headers auth_headers(const std::string& token) {
    if (token.empty()) return {};
    return { { "Authorization", "Bearer " + token },
             { "Cookie",       "forge_session=" + token } };
}

} // namespace

ForgeCloudAgent::ForgeCloudAgent()
    : m_server_url("http://127.0.0.1:3000")
{}

ForgeCloudAgent::~ForgeCloudAgent() = default;

void ForgeCloudAgent::set_server_url(const std::string& url)
{
    m_server_url = url;
    m_auth.signed_in = false;
    m_auth.session_token.clear();
}

bool ForgeCloudAgent::login(const std::string& username, const std::string& password)
{
    auto cli = make_client(m_server_url);
    json body = { { "username", username }, { "password", password } };
    auto res = cli->Post("/api/auth/login", body.dump(), "application/json");
    if (!res) {
        m_auth.last_error = "Cannot reach 3DPrintForge Server at " + m_server_url;
        return false;
    }
    if (res->status != 200) {
        m_auth.last_error = "Login rejected (HTTP " + std::to_string(res->status) + ")";
        return false;
    }

    try {
        auto j = json::parse(res->body);
        if (j.contains("token"))     m_auth.session_token = j["token"].get<std::string>();
        if (j.contains("username"))  m_auth.username      = j["username"].get<std::string>();
        // Cookie-based session is also acceptable.
        if (m_auth.session_token.empty()) {
            auto it = res->headers.find("Set-Cookie");
            if (it != res->headers.end()) {
                std::regex re(R"(forge_session=([^;]+))");
                std::smatch m;
                std::string h = it->second;
                if (std::regex_search(h, m, re)) m_auth.session_token = m[1].str();
            }
        }
    } catch (...) {
        m_auth.last_error = "Server returned malformed JSON";
        return false;
    }

    m_auth.signed_in = true;
    m_auth.server_url = m_server_url;
    m_last_synced = std::chrono::steady_clock::now();
    BOOST_LOG_TRIVIAL(info) << "ForgeCloudAgent: signed in as " << m_auth.username;
    return true;
}

void ForgeCloudAgent::logout()
{
    if (!m_auth.signed_in) return;
    auto cli = make_client(m_server_url);
    cli->Post("/api/auth/logout", auth_headers(m_auth.session_token), "", "application/json");
    m_auth = ForgeAuthState{};
}

bool ForgeCloudAgent::refresh_session()
{
    auto cli = make_client(m_server_url);
    auto res = cli->Get("/api/auth/status", auth_headers(m_auth.session_token));
    if (!res || res->status != 200) {
        m_auth.signed_in = false;
        return false;
    }
    m_last_synced = std::chrono::steady_clock::now();
    return true;
}

std::vector<ForgePrinter> ForgeCloudAgent::list_printers()
{
    std::vector<ForgePrinter> out;
    auto cli = make_client(m_server_url);
    auto res = cli->Get("/api/printers", auth_headers(m_auth.session_token));
    if (!res) {
        m_auth.last_error = "Cannot reach server (network)";
        return out;
    }
    if (res->status != 200) {
        m_auth.last_error = "Server returned HTTP " + std::to_string(res->status);
        return out;
    }

    try {
        auto j = json::parse(res->body);
        // Server may return either a bare array or { "printers": [...] }.
        const json& arr = j.is_array() ? j : (j.contains("printers") ? j["printers"] : json::array());
        for (const auto& p : arr) {
            ForgePrinter fp;
            if (p.contains("id"))             fp.id            = p["id"].get<std::string>();
            else if (p.contains("name"))      fp.id            = p["name"].get<std::string>();
            if (p.contains("name"))           fp.name          = p["name"].get<std::string>();
            if (p.contains("vendor"))         fp.vendor        = p["vendor"].get<std::string>();
            if (p.contains("model"))          fp.model         = p["model"].get<std::string>();
            if (p.contains("ip"))             fp.ip            = p["ip"].get<std::string>();
            else if (p.contains("host"))      fp.ip            = p["host"].get<std::string>();
            if (p.contains("status"))         fp.status        = p["status"].is_string() ? p["status"].get<std::string>() : p["status"].dump();
            if (p.contains("state"))          fp.state         = p["state"].is_string() ? p["state"].get<std::string>() : p["state"].dump();
            if (p.contains("current_job"))    fp.current_job   = p["current_job"].is_string() ? p["current_job"].get<std::string>() : "";
            if (p.contains("progress"))       fp.progress_pct  = p["progress"].is_number() ? (int)p["progress"].get<double>() : 0;
            if (p.contains("error"))          fp.error_message = p["error"].is_string() ? p["error"].get<std::string>() : "";
            out.push_back(std::move(fp));
        }
        m_last_synced = std::chrono::steady_clock::now();
    } catch (std::exception& e) {
        m_auth.last_error = std::string("Parse error: ") + e.what();
    }
    return out;
}

std::vector<ForgeSpool> ForgeCloudAgent::list_spools(bool include_archived)
{
    auto cli = make_client(m_server_url);
    const std::string path = include_archived ? "/api/inventory/spools"
                                              : "/api/inventory/spools?archived=0";
    auto res = cli->Get(path.c_str(), auth_headers(m_auth.session_token));
    if (!res) {
        m_auth.last_error = "Cannot reach server (network)";
        return {};
    }
    if (res->status != 200) {
        m_auth.last_error = "Server returned HTTP " + std::to_string(res->status);
        return {};
    }
    // Parsing lives in libslic3r (pure + unit-tested); this just feeds it the body.
    std::vector<ForgeSpool> out = parse_forge_spools(res->body);
    m_last_synced = std::chrono::steady_clock::now();
    return out;
}

ForgeCurrency ForgeCloudAgent::get_active_currency()
{
    auto cli = make_client(m_server_url);
    auto res = cli->Get("/api/currency", auth_headers(m_auth.session_token));
    if (!res || res->status != 200)
        return {};
    // Parsing lives in libslic3r (pure + unit-tested).
    return parse_forge_currency(res->body);
}

std::optional<std::string> ForgeCloudAgent::add_printer(const ForgePrinterConfig& cfg)
{
    auto cli = make_client(m_server_url);
    json body = { { "name", cfg.name } };
    if (!cfg.id.empty())          body["id"]          = cfg.id;
    if (!cfg.ip.empty())          body["ip"]          = cfg.ip;
    if (!cfg.type.empty())        body["type"]        = cfg.type;
    if (!cfg.serial.empty())      body["serial"]      = cfg.serial;
    if (!cfg.access_code.empty()) body["access_code"] = cfg.access_code;
    if (!cfg.model.empty())       body["model"]       = cfg.model;

    auto res = cli->Post("/api/printers", auth_headers(m_auth.session_token),
                         body.dump(), "application/json");
    if (!res || res->status >= 400) {
        m_auth.last_error = res ? ("HTTP " + std::to_string(res->status) + ": " + res->body)
                                : "Cannot reach server";
        return std::nullopt;
    }
    try {
        auto j = json::parse(res->body);
        if (j.contains("id")) return j["id"].get<std::string>();
    } catch (...) {}
    return cfg.id.empty() ? std::string{} : cfg.id;
}

bool ForgeCloudAgent::delete_printer(const std::string& printer_id)
{
    auto cli = make_client(m_server_url);
    auto res = cli->Delete(("/api/printers/" + printer_id).c_str(),
                           auth_headers(m_auth.session_token));
    if (!res || res->status >= 400) {
        m_auth.last_error = res ? ("HTTP " + std::to_string(res->status)) : "Cannot reach server";
        return false;
    }
    return true;
}

std::optional<std::string> ForgeCloudAgent::start_print(const std::string& printer_id,
                                                        const std::string& gcode_path)
{
    auto cli = make_client(m_server_url);
    json body = { { "printer_id", printer_id }, { "gcode_path", gcode_path } };
    auto res = cli->Post("/api/printers/" + printer_id + "/print",
                          auth_headers(m_auth.session_token),
                          body.dump(), "application/json");
    if (!res || res->status >= 400) {
        m_auth.last_error = res ? ("HTTP " + std::to_string(res->status)) : "Cannot reach server";
        return std::nullopt;
    }
    try {
        auto j = json::parse(res->body);
        if (j.contains("job_id")) return j["job_id"].get<std::string>();
        return std::string{"started"};
    } catch (...) {
        return std::string{"started"};
    }
}

std::string ForgeCloudAgent::get_camera_frame(const std::string& printer_id)
{
    auto cli = make_client(m_server_url);
    auto res = cli->Get("/api/printers/" + printer_id + "/frame.jpeg",
                        auth_headers(m_auth.session_token));
    if (!res || res->status != 200)
        return std::string();
    return res->body; // raw JPEG bytes
}

bool ForgeCloudAgent::control_printer(const std::string& printer_id, const std::string& action)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", action } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (!res || res->status >= 400) {
        m_auth.last_error = res ? ("HTTP " + std::to_string(res->status)) : "Cannot reach server";
        return false;
    }
    return true;
}

ForgeLiveState ForgeCloudAgent::get_printer_state(const std::string& printer_id)
{
    ForgeLiveState ls;
    auto cli = make_client(m_server_url);
    auto res = cli->Get("/api/printers/" + printer_id + "/state",
                        auth_headers(m_auth.session_token));
    if (!res || res->status != 200) return ls;
    try {
        auto j = json::parse(res->body);
        // Moonraker-class state is often nested under "print".
        const json& root = (j.contains("print") && j["print"].is_object()) ? j["print"] : j;
        auto num = [](const json& o, std::initializer_list<const char*> keys) -> double {
            for (auto k : keys)
                if (o.contains(k) && o[k].is_number()) return o[k].get<double>();
            return -1;
        };
        double pct = num(root, {"mc_percent", "percent", "progress"});
        if (pct < 0) pct = num(j, {"mc_percent", "percent", "progress"});
        ls.progress_pct = (pct >= 0) ? static_cast<int>(pct) : -1;
        ls.nozzle_temp  = num(root, {"nozzle_temper", "nozzle_temperature"});
        ls.bed_temp     = num(root, {"bed_temper", "bed_temperature"});
        ls.chamber_temp = num(root, {"chamber_temper", "chamber_temperature"});
        for (const char* k : {"gcode_state", "state", "print_status"})
            if (root.contains(k) && root[k].is_string()) { ls.state = root[k].get<std::string>(); break; }

        // Print-job telemetry for the task card.
        ls.layer_cur        = static_cast<int>(num(root, {"layer_num"}));
        ls.layer_total      = static_cast<int>(num(root, {"total_layer_num"}));
        ls.time_elapsed     = static_cast<int>(num(root, {"print_duration_seconds"}));
        ls.time_total       = static_cast<int>(num(root, {"total_duration_seconds"}));
        ls.speed_pct        = static_cast<int>(num(root, {"spd_mag", "spd_lvl"}));
        ls.filament_used_mm = num(root, {"filament_used_mm"});
        for (const char* k : {"subtask_name", "gcode_file", "filename"})
            if (root.contains(k) && root[k].is_string()) { ls.job_name = root[k].get<std::string>(); break; }
        for (const char* k : {"_sm_state_label", "_sm_state_name", "stage"})
            if (root.contains(k) && root[k].is_string()) { ls.stage_label = root[k].get<std::string>(); break; }
        for (const char* k : {"print_error_msg", "error_message", "error"})
            if (root.contains(k) && root[k].is_string()) { ls.error_msg = root[k].get<std::string>(); break; }

        // Per-tool (Snapmaker U1-style): tool 0 = nozzle_temper, tools 1..N =
        // _extra_extruders[] (ordered T1,T2,T3); filament from _sm_filament[].
        if (root.contains("_extra_extruders") && root["_extra_extruders"].is_array()) {
            ForgeToolState t0;
            t0.temp = ls.nozzle_temp;
            if (root.contains("nozzle_target_temper") && root["nozzle_target_temper"].is_number())
                t0.target = root["nozzle_target_temper"].get<double>();
            ls.tools.push_back(t0);
            for (const auto& e : root["_extra_extruders"]) {
                ForgeToolState t;
                if (e.contains("temperature") && e["temperature"].is_number()) t.temp = e["temperature"].get<double>();
                if (e.contains("target") && e["target"].is_number())           t.target = e["target"].get<double>();
                ls.tools.push_back(t);
            }
            if (root.contains("_sm_filament") && root["_sm_filament"].is_array()) {
                const auto& fil = root["_sm_filament"];
                for (size_t i = 0; i < ls.tools.size() && i < fil.size(); ++i) {
                    const auto& f = fil[i];
                    if (f.contains("type") && f["type"].is_string()) {
                        std::string ty = f["type"].get<std::string>();
                        if (!ty.empty() && ty != "NONE") ls.tools[i].filament = ty;
                    }
                    if (f.contains("color") && f["color"].is_string())
                        ls.tools[i].color = f["color"].get<std::string>();
                }
            }
            if (root.contains("_active_extruder") && root["_active_extruder"].is_string()) {
                std::string ae = root["_active_extruder"].get<std::string>();
                if (ae == "extruder") ls.active_tool = 0;
                else { try { ls.active_tool = std::stoi(ae.substr(8)); } catch (...) {} }
            }
        }
        ls.ok = true;
    } catch (...) {}
    return ls;
}

bool ForgeCloudAgent::control_home(const std::string& printer_id)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "home" } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_move(const std::string& printer_id, const std::string& axis, double dist_mm)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "move" }, { "axis", axis }, { "dist", dist_mm } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_extrude(const std::string& printer_id, double amount_mm)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "extrude" }, { "amount", amount_mm } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_tool(const std::string& printer_id, int tool_index)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "select_tool" }, { "tool", tool_index } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_filament(const std::string& printer_id, const std::string& action, int tool)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "filament" }, { "op", action } };
    if (tool >= 0) body["tool"] = tool;
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_fan(const std::string& printer_id, int percent)
{
    percent = std::max(0, std::min(100, percent));
    auto cli = make_client(m_server_url);
    json body = { { "action", "set_fan" }, { "percent", percent } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_light(const std::string& printer_id, bool on)
{
    auto cli = make_client(m_server_url);
    json body = { { "action", "set_light" }, { "on", on } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_speed(const std::string& printer_id, int percent)
{
    percent = std::max(10, std::min(300, percent));
    // Bambu accepts a preset level (1 silent, 2 standard, 3 sport, 4 ludicrous);
    // Moonraker uses the raw percentage (M220). Send both so either connector works.
    int level = percent <= 75 ? 1 : percent <= 110 ? 2 : percent <= 150 ? 3 : 4;
    auto cli = make_client(m_server_url);
    json body = { { "action", "set_speed" }, { "percent", percent }, { "level", level } };
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

bool ForgeCloudAgent::control_set_temp(const std::string& printer_id, const std::string& heater,
                                       int temp, int tool)
{
    temp = std::max(0, std::min(350, temp));
    auto cli = make_client(m_server_url);
    json body = { { "action", "set_temp" }, { "temp", temp } };
    if (heater == "bed") body["heater"] = "bed";
    if (tool >= 0)       body["tool"]   = tool;
    auto res = cli->Post("/api/printers/" + printer_id + "/control",
                         auth_headers(m_auth.session_token), body.dump(), "application/json");
    if (res && res->status >= 400) m_auth.last_error = "HTTP " + std::to_string(res->status);
    return res && res->status < 400;
}

} // namespace Slic3r
