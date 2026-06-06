#include "ForgeCloud.hpp"

#include <cstdlib>
#include <cctype>
#include <string>
#include <vector>

#include <wx/string.h>
#include <wx/utils.h>   // wxExecute
#include <wx/arrstr.h>

#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

std::string forge_dashboard_url()
{
    if (AppConfig* cfg = wxGetApp().app_config) {
        std::string v = cfg->get("forge_dashboard_url");
        if (!v.empty()) return v;
    }
    if (const char* env = std::getenv("FORGE_DASHBOARD_URL"); env && *env)
        return env;
    if (AppConfig* cfg = wxGetApp().app_config) {
        std::string v = cfg->get("forge_server_url"); // fleet panel's key
        if (!v.empty()) return v;
    }
    return "https://localhost:3443";
}

void set_forge_dashboard_url(const std::string& url)
{
    if (AppConfig* cfg = wxGetApp().app_config) {
        cfg->set("forge_dashboard_url", url);
        cfg->set("forge_server_url", url); // keep the fleet agent in sync
    }
}

// Internal alias used by the provider below.
static std::string dashboard_url() { return forge_dashboard_url(); }

// Minimal URL-encoding for query values (filenames may contain spaces).
static std::string url_encode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back(static_cast<char>(c));
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// 3DPrintForge dashboard provider.
//
// HTTP is bridged through curl via wxExecute so we don't pull OpenSSL into
// the GUI just to call our own dashboard (same approach as ForgeLibraryDialog;
// Phase 3 will swap this for cpp-httplib + OpenSSL once that is wired in).
// ---------------------------------------------------------------------------
class ThreeDPrintForgeProvider : public CloudProvider
{
public:
    std::string id() const override { return "3dprintforge"; }
    std::string display_name() const override { return "3DPrintForge"; }
    // Always available — defaults to the local dashboard.
    bool        is_configured() const override { return true; }

    CloudJobResult send_job(const std::string& file_path,
                            const std::string& filename,
                            const std::string& printer_id,
                            bool               auto_queue) override
    {
        CloudJobResult r;

        std::string url = dashboard_url() + "/api/slicer/upload?filename=" + url_encode(filename);
        if (!printer_id.empty())
            url += "&printer_id=" + url_encode(printer_id);
        if (auto_queue)
            url += "&auto_queue=1";

        // -k: accept the dashboard's self-signed cert. --data-binary @file
        // streams the raw bytes the upload endpoint expects.
        wxString cmd = wxString::Format(
            "curl -ks -X POST \"%s\" --data-binary @\"%s\" --max-time 120",
            wxString::FromUTF8(url.c_str()),
            wxString::FromUTF8(file_path.c_str()));

        wxArrayString out, err;
        long exit_code = wxExecute(cmd, out, err, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE);
        if (exit_code != 0) {
            r.ok = false;
            r.message = "curl exit code " + std::to_string(exit_code) +
                        " (is 3DPrintForge running at " + dashboard_url() + "?)";
            return r;
        }

        // The endpoint replies 201 with a JSON body; we don't parse it here
        // (no JSON lib on the GUI side yet) — a zero exit means accepted.
        r.ok = true;
        if (!out.IsEmpty())
            r.message = out[0].ToStdString();
        return r;
    }
};

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------
std::vector<CloudProvider*> cloud_providers()
{
    static ThreeDPrintForgeProvider s_forge;
    static std::vector<CloudProvider*> s_providers = { &s_forge };
    return s_providers;
}

CloudProvider* cloud_provider(const std::string& id)
{
    for (CloudProvider* p : cloud_providers())
        if (p->id() == id)
            return p;
    return nullptr;
}

}} // namespace Slic3r::GUI
