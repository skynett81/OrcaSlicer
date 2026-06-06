#ifndef slic3r_GUI_ForgeCloud_hpp_
#define slic3r_GUI_ForgeCloud_hpp_

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

// A printer exposed by a cloud/remote service.
struct CloudPrinter {
    std::string id;
    std::string name;
    std::string status;
};

// Result of submitting a job to a cloud/remote service.
struct CloudJobResult {
    bool        ok = false;
    std::string job_id;
    std::string message;
};

// Abstraction for a remote/cloud print service.
//
// Phase 1 ships the 3DPrintForge dashboard provider. Future providers
// (OctoEverywhere, Obico, SimplyPrint, Prusa Connect, Bambu Cloud) plug
// in behind this same interface without touching call sites.
class CloudProvider
{
public:
    virtual ~CloudProvider() = default;

    // Stable slug, e.g. "3dprintforge", "octoeverywhere".
    virtual std::string id() const = 0;
    // Human-readable name shown in the UI.
    virtual std::string display_name() const = 0;
    // True when the provider has the config it needs to be used.
    virtual bool        is_configured() const = 0;

    // Upload a sliced or project file to the service.
    // `printer_id` may be empty (let the service queue generically).
    // `auto_queue` asks the service to enqueue the job for printing.
    virtual CloudJobResult send_job(const std::string& file_path,
                                    const std::string& filename,
                                    const std::string& printer_id,
                                    bool               auto_queue) = 0;

    // Best-effort printer listing; default empty until a provider implements it.
    virtual std::vector<CloudPrinter> list_printers() { return {}; }
};

// Registry of available providers (3DPrintForge always present).
std::vector<CloudProvider*> cloud_providers();
CloudProvider*              cloud_provider(const std::string& id);

// Push the slicer's printer/filament/process profile catalog to the
// 3DPrintForge dashboard (POST /api/slicer/profiles/push). On-demand
// complement to the dashboard's periodic pull — works in GUI-only mode.
CloudJobResult sync_profiles_to_forge();

// Shared 3DPrintForge dashboard URL.
// Resolution order: AppConfig "forge_dashboard_url" -> env FORGE_DASHBOARD_URL
// -> AppConfig "forge_server_url" (fleet panel's key) -> https://localhost:3443.
std::string forge_dashboard_url();
// Persists the URL to AppConfig (both keys, so the fleet panel agrees).
void        set_forge_dashboard_url(const std::string& url);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_ForgeCloud_hpp_
