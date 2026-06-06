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

} // namespace Slic3r
