// rest_server.cpp — embedded HTTP server for skynett81/3DPrintForge Slicer fork
//
// Phase 1 reference implementation. Wires cpp-httplib + nlohmann/json
// onto the existing 3DPrintForge Slicer profile manager / slicing pipeline.
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
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r_version.h"

namespace forge_slicer {

static std::string SERVICE_VERSION = "1.10.2-skynett.1";
static std::string UPSTREAM_VERSION = SLIC3R_APP_NAME " " FORGE_VERSION;

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
    if (bundle) {
        std::cout << "[forge-slicer] PresetBundle injected: "
                  << bundle->printers.size() << " printers, "
                  << bundle->filaments.size() << " filaments, "
                  << bundle->prints.size() << " processes" << std::endl;
    } else {
        std::cout << "[forge-slicer] PresetBundle detached (null)" << std::endl;
    }
}

// Job tracking for POST /api/slice → GET /api/jobs/:id/gcode.
struct SliceJob {
    std::string gcode_path;
    std::size_t gcode_size = 0;
    double      estimated_time_s = 0.0;
    std::vector<double> filament_used_g;
    std::string created_at;
};
static std::mutex g_jobs_mutex;
static std::map<std::string, SliceJob> g_jobs;

// Channel used by /api/slice/stream — slicing thread pushes progress
// events, the chunked content provider drains them and writes SSE.
struct ChannelEvent {
    std::string kind;     // "progress", "done", "error"
    std::string payload;  // JSON string (without surrounding "data: ")
};
class StatusChannel {
public:
    void push(ChannelEvent ev) {
        std::lock_guard<std::mutex> lk(mtx_);
        q_.push_back(std::move(ev));
        cv_.notify_one();
    }
    // Returns false if the channel was closed before an event arrived.
    bool pop(ChannelEvent& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [this]{ return !q_.empty() || closed_; }))
            return false;
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        return true;
    }
    void close() {
        std::lock_guard<std::mutex> lk(mtx_);
        closed_ = true;
        cv_.notify_all();
    }
    bool is_closed_and_drained() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return closed_ && q_.empty();
    }
private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<ChannelEvent> q_;
    bool closed_ = false;
};

// Result of a slice job — used by both the buffered and SSE handlers.
struct SliceOutcome {
    std::string job_id;
    std::string gcode_path;
    std::size_t gcode_size = 0;
    double      estimated_time_s = 0.0;
    std::vector<double> filament_used_g;
};

using ProgressFn = std::function<void(int pct, const std::string& stage)>;

// Forward decls for helpers defined later in the file.
static std::string make_job_id();
static std::filesystem::path forge_tmp_root();
inline std::string iso_now();

// Shared slicing pipeline. Reads multipart fields from the request,
// drives Slic3r::Print through process()+export_gcode(), and reports
// progress via the optional callback. Throws std::runtime_error on
// validation/slicing failure — callers translate to JSON error.
static SliceOutcome run_slice(const httplib::Request& req, ProgressFn progress) {
    auto pick = [&](const char* name) -> std::string {
        auto it = req.files.find(name);
        return it != req.files.end() ? it->second.content : std::string();
    };

    std::string model_bytes      = pick("model");
    std::string printer_id       = pick("printer_id");
    std::string process_id       = pick("process_id");
    std::string filament_ids_raw = pick("filament_ids");
    if (model_bytes.empty()) throw std::runtime_error("missing 'model' file");
    if (printer_id.empty())  throw std::runtime_error("missing 'printer_id'");
    if (process_id.empty())  throw std::runtime_error("missing 'process_id'");

    std::vector<std::string> filament_ids;
    if (!filament_ids_raw.empty()) {
        auto arr = nlohmann::json::parse(filament_ids_raw);
        for (const auto& f : arr) filament_ids.push_back(f.get<std::string>());
    }
    if (filament_ids.empty())
        throw std::runtime_error("'filament_ids' must be a non-empty JSON array");

    if (progress) progress(2, "validating");

    SliceOutcome out;
    out.job_id = make_job_id();
    auto root  = forge_tmp_root();
    auto job_dir = root / out.job_id;
    std::filesystem::create_directories(job_dir);

    std::string upload_name = req.files.find("model")->second.filename;
    std::string ext = ".stl";
    if (auto dot = upload_name.rfind('.'); dot != std::string::npos)
        ext = upload_name.substr(dot);
    auto model_path = job_dir / ("input" + ext);
    {
        std::ofstream of(model_path, std::ios::binary);
        of.write(model_bytes.data(), static_cast<std::streamsize>(model_bytes.size()));
    }
    auto gcode_path = root / (out.job_id + ".gcode");

    if (progress) progress(5, "applying_presets");

    Slic3r::DynamicPrintConfig config;
    {
        std::unique_lock<std::shared_mutex> bundle_lock(g_bundle_mutex);
        if (!g_bundle) throw std::runtime_error("no PresetBundle injected");

        if (!g_bundle->printers.select_preset_by_name(printer_id, true))
            throw std::runtime_error("unknown printer_id: " + printer_id);
        if (!g_bundle->prints.select_preset_by_name(process_id, true))
            throw std::runtime_error("unknown process_id: " + process_id);
        for (const auto& fid : filament_ids) {
            if (!g_bundle->filaments.select_preset_by_name(fid, true))
                throw std::runtime_error("unknown filament_id: " + fid);
        }
        config = g_bundle->full_config(true);
    }

    if (progress) progress(10, "loading_model");
    Slic3r::Model model = Slic3r::Model::read_from_file(model_path.string());

    Slic3r::Print print;
    if (progress) {
        // Map Slic3r's 0..100 percent to 15..90 of the overall job — we
        // reserve 0..15 for setup and 90..100 for gcode export.
        print.set_status_callback([progress](const Slic3r::PrintBase::SlicingStatus& s) {
            int scaled = 15 + (s.percent > 0 ? (s.percent * 75 / 100) : 0);
            progress(scaled, s.text.empty() ? std::string("slicing") : s.text);
        });
    }
    print.apply(model, config);

    if (progress) progress(15, "slicing");
    print.process();

    if (progress) progress(92, "exporting_gcode");
    Slic3r::GCodeProcessorResult gcode_result;
    std::string out_path = print.export_gcode(gcode_path.string(), &gcode_result, nullptr);
    std::error_code ec;
    out.gcode_path       = out_path;
    out.gcode_size       = std::filesystem::file_size(out_path, ec);
    out.estimated_time_s = gcode_result.print_statistics.modes[0].time;

    for (std::size_t i = 0; i < gcode_result.filament_densities.size(); ++i) {
        double vol_mm3 = 0.0;
        auto it = gcode_result.print_statistics.total_volumes_per_extruder.find(i);
        if (it != gcode_result.print_statistics.total_volumes_per_extruder.end())
            vol_mm3 = it->second;
        out.filament_used_g.push_back((vol_mm3 / 1000.0) * gcode_result.filament_densities[i]);
    }

    {
        std::lock_guard<std::mutex> lk(g_jobs_mutex);
        g_jobs[out.job_id] = SliceJob{out.gcode_path, out.gcode_size,
                                      out.estimated_time_s, out.filament_used_g, iso_now()};
    }
    if (progress) progress(100, "done");
    return out;
}

static nlohmann::json outcome_to_json(const SliceOutcome& o) {
    nlohmann::json r;
    r["ok"] = true;
    r["job_id"] = o.job_id;
    r["gcode_path"] = o.gcode_path;
    r["gcode_size"] = o.gcode_size;
    r["estimated_time_s"] = o.estimated_time_s;
    r["filament_used_g"] = o.filament_used_g;
    return r;
}

static std::string make_job_id() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream ss;
    ss << std::hex << std::setw(16) << std::setfill('0') << dist(rng);
    return ss.str();
}

static std::filesystem::path forge_tmp_root() {
    auto p = std::filesystem::temp_directory_path() / "forge-slicer";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
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

// Use lbegin() (not cbegin()) so we expose the default presets that
// ship with 3DPrintForge Slicer too — cbegin() skips the first
// m_num_default_presets entries, which means a fresh install with no
// vendor profiles installed would otherwise return an empty array.
static nlohmann::json list_profiles(const std::string& kind, const std::string& vendor_filter) {
    using nlohmann::json;
    json arr = json::array();

    std::shared_lock lock(g_bundle_mutex);
    if (!g_bundle) return arr;

    auto enumerate = [&](Slic3r::PresetCollection& coll, const char* k) {
        if (kind != "all" && kind != k) return;
        for (auto it = coll.lbegin(); it != coll.end(); ++it) {
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

    struct Pair { Slic3r::PresetCollection* coll; const char* kind; };
    const Pair scopes[] = {
        { &g_bundle->printers,  "printer"  },
        { &g_bundle->filaments, "filament" },
        { &g_bundle->prints,    "process"  },
    };
    for (const auto& s : scopes) {
        for (auto it = s.coll->lbegin(); it != s.coll->end(); ++it) {
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

    // Buffered slice — caller waits for the whole pipeline, then gets a
    // single JSON document. Serialized: only one slice runs at a time.
    static std::mutex g_slice_mutex;
    svr->Post("/api/slice", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::lock_guard<std::mutex> slice_lock(g_slice_mutex);
        try {
            if (!req.is_multipart_form_data())
                throw std::runtime_error("expected multipart/form-data");
            SliceOutcome o = run_slice(req, nullptr);
            res.set_content(outcome_to_json(o).dump(), "application/json");
        } catch (const std::exception& ex) {
            json err;
            err["error"] = ex.what();
            err["code"] = "ERR_SLICE_FAILED";
            res.status = 500;
            res.set_content(err.dump(), "application/json");
        }
    });

    // Streaming slice — Server-Sent Events. Slicer thread pushes progress
    // events into a channel; the chunked content provider drains them
    // and writes "event: kind\ndata: {...}\n\n" lines to the response.
    svr->Post("/api/slice/stream", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        if (!req.is_multipart_form_data()) {
            json err;
            err["error"] = "expected multipart/form-data";
            err["code"]  = "ERR_BAD_REQUEST";
            res.status = 400;
            res.set_content(err.dump(), "application/json");
            return;
        }

        auto channel = std::make_shared<StatusChannel>();

        // Copy the multipart payload onto the worker thread — req itself
        // is only valid for the duration of this lambda.
        auto req_copy = std::make_shared<httplib::Request>(req);

        std::thread([req_copy, channel]() {
            std::lock_guard<std::mutex> slice_lock(g_slice_mutex);
            try {
                SliceOutcome o = run_slice(*req_copy,
                    [channel](int pct, const std::string& stage) {
                        json p; p["pct"] = pct; p["stage"] = stage;
                        channel->push({"progress", p.dump()});
                    });
                channel->push({"done", outcome_to_json(o).dump()});
            } catch (const std::exception& ex) {
                json e;
                e["error"] = ex.what();
                e["code"]  = "ERR_SLICE_FAILED";
                channel->push({"error", e.dump()});
            }
            channel->close();
        }).detach();

        res.set_header("Cache-Control", "no-cache");
        res.set_chunked_content_provider("text/event-stream",
            [channel](size_t /*offset*/, httplib::DataSink& sink) {
                ChannelEvent ev;
                // Time-bounded so we can emit SSE keepalives.
                if (!channel->pop(ev, std::chrono::seconds(5))) {
                    if (channel->is_closed_and_drained()) {
                        sink.done();
                        return false;
                    }
                    static const std::string keepalive = ":keepalive\n\n";
                    sink.write(keepalive.data(), keepalive.size());
                    return true;
                }
                std::string line = "event: " + ev.kind + "\ndata: " + ev.payload + "\n\n";
                sink.write(line.data(), line.size());
                if (ev.kind == "done" || ev.kind == "error") {
                    sink.done();
                    return false;
                }
                return true;
            });
    });

    svr->Get(R"(/api/jobs/([^/]+)/gcode)", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        std::string job_id = req.matches[1];
        std::string path;
        {
            std::lock_guard<std::mutex> lock(g_jobs_mutex);
            auto it = g_jobs.find(job_id);
            if (it == g_jobs.end()) {
                json err; err["error"] = "job not found"; err["code"] = "ERR_JOB_NOT_FOUND";
                res.status = 404;
                res.set_content(err.dump(), "application/json");
                return;
            }
            path = it->second.gcode_path;
        }
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            json err; err["error"] = "gcode file missing on disk"; err["code"] = "ERR_GCODE_MISSING";
            res.status = 410;
            res.set_content(err.dump(), "application/json");
            return;
        }
        std::ostringstream buf;
        buf << f.rdbuf();
        res.set_content(buf.str(), "text/x-gcode");
    });

    svr->Get("/api/jobs", [require_auth](const httplib::Request& req, httplib::Response& res) {
        if (!require_auth(req, res)) return;
        json arr = json::array();
        {
            std::lock_guard<std::mutex> lock(g_jobs_mutex);
            for (const auto& [id, j] : g_jobs) {
                json o;
                o["job_id"]            = id;
                o["gcode_path"]        = j.gcode_path;
                o["gcode_size"]        = j.gcode_size;
                o["estimated_time_s"]  = j.estimated_time_s;
                o["filament_used_g"]   = j.filament_used_g;
                o["created_at"]        = j.created_at;
                arr.push_back(o);
            }
        }
        json resp; resp["jobs"] = arr;
        res.set_content(resp.dump(), "application/json");
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
