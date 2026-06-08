#include "InventoryProvider.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "ForgeCloudAgent.hpp"
#include "libslic3r/AppConfig.hpp"

#include <httplib.h>
#include <regex>

namespace Slic3r {

namespace {

// Plain HTTP client for a Spoolman base URL (Spoolman is typically self-hosted
// over http on a local port). Mirrors the ForgeCloudAgent client setup.
std::vector<ForgeSpool> fetch_spoolman(const InventoryConfig& cfg)
{
    std::string host = cfg.url;
    int         port = 80;
    std::smatch m;
    std::regex  re(R"(^(https?)://([^:/]+)(?::(\d+))?)");
    if (std::regex_search(cfg.url, m, re)) {
        host = m[2].str();
        const bool ssl = (m[1].str() == "https");
        port = m[3].matched ? std::stoi(m[3].str()) : (ssl ? 443 : 80);
    }
    httplib::Client cli(host, port);
    cli.set_connection_timeout(3, 0);
    cli.set_read_timeout(8, 0);
    httplib::Headers headers;
    if (!cfg.token.empty())
        headers.insert({ "Authorization", "Bearer " + cfg.token });
    auto res = cli.Get("/api/v1/spool", headers);
    if (!res || res->status != 200)
        return {};
    return parse_spoolman_spools(res->body);
}

} // namespace

InventoryConfig inventory_config()
{
    InventoryConfig c;
    AppConfig* cfg = GUI::wxGetApp().app_config;
    if (cfg == nullptr)
        return c;

    c.provider = cfg->get("inventory_provider");
    c.url      = cfg->get("inventory_url");
    c.token    = cfg->get("inventory_token");

    // Backward compatibility: an existing 3DPrintForge dashboard configuration
    // (set via the Forge connection dialog) implies the 3dprintforge provider.
    // Note: we read the RAW keys, not forge_dashboard_url(), so an unconfigured
    // slicer stays "off" rather than defaulting to a localhost probe.
    if (c.provider.empty()) {
        std::string fu = cfg->get("forge_dashboard_url");
        if (fu.empty())
            fu = cfg->get("forge_server_url");
        if (!fu.empty()) {
            c.provider = "3dprintforge";
            c.url      = fu;
            c.token    = cfg->get("forge_server_token");
        }
    }
    return c;
}

std::vector<ForgeSpool> fetch_inventory_spools(const InventoryConfig& cfg)
{
    if (!cfg.configured())
        return {};

    if (cfg.provider == "3dprintforge") {
        ForgeCloudAgent agent;
        agent.set_server_url(cfg.url);
        return agent.list_spools();
    }
    if (cfg.provider == "spoolman")
        return fetch_spoolman(cfg);
    return {};
}

ForgeCurrency fetch_inventory_currency(const InventoryConfig& cfg)
{
    if (!cfg.configured())
        return {};

    if (cfg.provider == "3dprintforge") {
        ForgeCloudAgent agent;
        agent.set_server_url(cfg.url);
        return agent.get_active_currency();
    }
    return {};
}

} // namespace Slic3r
