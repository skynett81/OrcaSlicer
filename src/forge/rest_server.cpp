// rest_server.cpp — embedded HTTP server for skynett81/OrcaSlicer fork
//
// Phase 1 reference implementation. Wires cpp-httplib + nlohmann/json
// onto the existing OrcaSlicer profile manager / slicing pipeline.
// Drop into src/forge/ in your fork, link cpp-httplib (header-only),
// and call rest_server::start(port) from your main entry point when
// the --rest-port CLI flag is set.
//
// What's implemented here:
//   GET  /api/health          - liveness + version
//   GET  /api/version         - lightweight version-only probe
//   GET  /api/profiles        - list profiles, ?kind=printer|filament|process|all
//   GET  /api/profiles/{id}   - single profile with full settings
//   GET  /api/printers        - configured printer bindings
//   POST /api/slice           - placeholder, returns a 501 until
//                               you wire it onto Slic3r::Print
//   POST /api/preview         - placeholder (501)
//   GET  /api/jobs/{id}/gcode - placeholder (404)
//
// Once Phase 1 health/version/profiles works, reuse the pattern for
// /api/slice etc. by hooking into your existing PrintObject /
// GCode/CWriter pipelines.

#include "rest_server.hpp"

#include "httplib.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

namespace forge_slicer {

static std::string SERVICE_VERSION = "1.10.2-skynett.1";
static std::string UPSTREAM_VERSION = "OrcaSlicer 2.3.1";

static std::atomic<bool> g_started{false};
static std::string g_started_at;

// Bundle injection. Set by OrcaSlicer.cpp (GUI: wxGetApp().preset_bundle;
// headless: a PresetBundle constructed from data_dir). nullptr means
// profile endpoints return empty until the bundle is wired in.
static std::shared_mutex g_bundle_mutex;
static Slic3r::PresetBundle* g_bundle = nullptr;

void set_preset_bundle(Slic3r::PresetBundle* bundle) {
    std::unique_lock lock(g_bundle_mutex);
    g_bundle = bundle;
}

// Convert one Preset (printer/filament/process) into a JSON document.
// Iterates the underlying DynamicPrintConfig via keys()/opt_serialize()
// since Preset::config has no first-class JSON serializer.
static nlohmann::json preset_to_json(const Slic3r::Preset& preset, const char* kind) {
    using nlohmann::json;
    json p;
    p["id"]         = preset.name;
    p["kind"]       = kind;
    p["name"]       = preset.name;
    p["vendor"]     = preset.vendor ? preset.vendor->name : "";
    p["is_default"] = preset.is_default;

    json settings = json::object();
    for (const auto& key : preset.config.keys()) {
        settings[key] = preset.config.opt_serialize(key);
    }
    p["settings"] = std::move(settings);
    return p;
}

inline std::string iso_now() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return buf;
}

static nlohmann::json list_profiles(const std::string& kind, const std::string& vendor_filter) {
    using nlohmann::json;
    json arr = json::array();

    std::shared_lock lock(g_bundle_mutex);
    if (!g_bundle) return arr;

    auto enumerate = [&](const Slic3r::PresetCollection& coll, const char* k) {
        if (kind != "all" && kind != k) return;
        for (auto it = coll.cbegin(); it != coll.cend(); ++it) {
            const Slic3r::Preset& preset = *it;
            if (!vendor_filter.empty()) {
                const std::string vname = preset.vendor ? preset.vendor->name : "";
                if (vname != vendor_filter) continue;
            }
            arr.push_back(preset_to_json(preset, k));
        }
    };
    enumerate(g_bundle->printers,  "printer");
    enumerate(g_bundle->filaments, "filament");
    enumerate(g_bundle->prints,    "process");
    return arr;
}

static nlohmann::json find_profile(const std::string& id) {
    using nlohmann::json;
    std::shared_lock lock(g_bundle_mutex);
    if (!g_bundle) return nullptr;

    struct Pair { const Slic3r::PresetCollection* coll; const char* kind; };
    const Pair scopes[] = {
        { &g_bundle->printers,  "printer"  },
        { &g_bundle->filaments, "filament" },
        { &g_bundle->prints,    "process"  },
    };
    for (const auto& s : scopes) {
        for (auto it = s.coll->cbegin(); it != s.coll->cend(); ++it) {
            const Slic3r::Preset& preset = *it;
            if (preset.name == id) {
                return preset_to_json(preset, s.kind);
            }
        }
    }
    return nullptr;
}

void start(int port, const std::string& bind, const std::string& token) {
    using nlohmann::json;
    auto* svr = new httplib::Server();

    // CORS: localhost-only by default; add OPTIONS pre-flight if you
    // ever expose this beyond loopback.
    svr->set_default_headers({{"X-Service", "forge-slicer"}});

    // Auth — checks the Bearer token if configured. Localhost-only
    // deployments can run without one.
    auto require_auth = [token](const httplib::Request& req, httplib::Response& res) -> bool {
        if (token.empty()) return true;
        auto auth = req.get_header_value("Authorization");
        if (auth == "Bearer " + token) return true;
        json err;
        err["error"] = "token required";
        err["code"] = "ERR_UNAUTHORIZED";
        res.status = 401;
        res.set_content(err.dump(), "application/json");
        return false;
    };

    g_started_at = iso_now();
    g_started.store(true);

    svr->Get("/api/health", [](const httplib::Request&, httplib::Response& res) {
        json j;
        j["ok"] = true;
        j["service"] = "forge-slicer";
        j["version"] = SERVICE_VERSION;
        j["upstream"] = UPSTREAM_VERSION;
        j["started_at"] = g_started_at;
        // j["config_dir"] = Slic3r::data_dir();
        res.set_content(j.dump(), "application/json");
    });

    svr->Get("/api/version", [](const httplib::Request&, httplib::Response& res) {
        json j;
        j["version"] = SERVICE_VERSION;
        j["api"] = 1;
        res.set_content(j.dump(), "application/json");
    });

    svr->Get("/api/profiles", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string kind = req.has_param("kind") ? req.get_param_value("kind") : "all";
        std::string vendor = req.has_param("vendor") ? req.get_param_value("vendor") : "";
        json j;
        j["profiles"] = list_profiles(kind, vendor);
        res.set_content(j.dump(), "application/json");
    });

    svr->Get(R"(/api/profiles/(.+))", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string id = req.matches[1];
        auto p = find_profile(id);
        if (p.is_null()) {
            json err;
            err["error"] = "profile not found";
            err["code"] = "ERR_PROFILE_NOT_FOUND";
            res.status = 404;
            res.set_content(err.dump(), "application/json");
            return;
        }
        res.set_content(p.dump(), "application/json");
    });

    svr->Get("/api/printers", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        json j;
        // Mirror the printer-only subset of /api/profiles.
        j["printers"] = list_profiles("printer", "");
        res.set_content(j.dump(), "application/json");
    });

    // ── Phase 3 placeholder — implement when you're ready ─────────────
    svr->Post("/api/slice", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        json err;
        err["error"] = "slicing not implemented yet — phase 3";
        err["code"] = "ERR_NOT_IMPLEMENTED";
        res.status = 501;
        res.set_content(err.dump(), "application/json");
    });

    svr->Post("/api/preview", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        res.status = 501;
        res.set_content("{\"error\":\"preview not implemented\",\"code\":\"ERR_NOT_IMPLEMENTED\"}", "application/json");
    });

    std::cout << "[forge-slicer] REST service listening on " << bind << ":" << port << std::endl;
    svr->listen(bind.c_str(), port);
}

} // namespace forge_slicer
