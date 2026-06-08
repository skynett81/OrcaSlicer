#ifndef slic3r_InventoryProvider_hpp_
#define slic3r_InventoryProvider_hpp_

#include <string>
#include <vector>

#include "libslic3r/ForgeSpool.hpp"

namespace Slic3r {

// A filament-inventory source the slicer can read spools/currency from. The
// slicer depends on this CONTRACT, not on any single product: 3DPrintForge is one
// provider, Spoolman is another, and any server matching the documented REST
// contract works. The provider is OFF by default — nothing is probed unless the
// user explicitly configures an inventory server (so the slicer stays fully
// standalone and never reaches out to localhost on its own).
struct InventoryConfig
{
    std::string provider; // "3dprintforge" | "spoolman" | "" (none/off)
    std::string url;      // base URL of the inventory server
    std::string token;    // optional bearer/api token

    bool configured() const { return !provider.empty() && !url.empty(); }
};

// Resolve the active inventory provider from AppConfig. Reads the generic
// inventory_provider/inventory_url/inventory_token keys, falling back to the
// legacy 3DPrintForge dashboard keys (forge_dashboard_url/forge_server_url) for
// backward compatibility. Returns an unconfigured config when nothing is set.
InventoryConfig inventory_config();

// Fetch the spool inventory from the configured provider. Returns an empty list
// when not configured or on any failure. Safe to call from a worker thread; never
// throws.
std::vector<ForgeSpool> fetch_inventory_spools(const InventoryConfig& cfg);

// Fetch the provider's active display currency (best-effort; empty code/symbol
// when unknown or not supported by the provider).
ForgeCurrency fetch_inventory_currency(const InventoryConfig& cfg);

} // namespace Slic3r

#endif
