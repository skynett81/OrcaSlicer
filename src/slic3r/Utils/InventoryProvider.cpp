#include "InventoryProvider.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "ForgeCloudAgent.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r {

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
    // Other providers (e.g. "spoolman") are added behind this same dispatch.
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
