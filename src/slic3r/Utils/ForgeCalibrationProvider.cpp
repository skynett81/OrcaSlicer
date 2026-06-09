#include "ForgeCalibrationProvider.hpp"
#include "InventoryProvider.hpp"

#include "libslic3r/Utils.hpp"

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <httplib.h>
#include <regex>
#include <sstream>

namespace Slic3r {

namespace {

// Dashboard REST contract (3DPrintForge provider):
//   GET  {url}/api/calibration            -> JSON array of calibration records
//   POST {url}/api/calibration            <- one record (JSON object)
// Both are optional: if the server doesn't implement them the local cache is the
// single source of truth.
const char* kCalibrationPath = "/api/calibration";

bool split_url(const std::string& url, std::string& host, int& port)
{
    host = url;
    port = 80;
    std::smatch m;
    std::regex  re(R"(^(https?)://([^:/]+)(?::(\d+))?)");
    if (!std::regex_search(url, m, re))
        return false;
    host = m[2].str();
    const bool ssl = (m[1].str() == "https");
    port = m[3].matched ? std::stoi(m[3].str()) : (ssl ? 443 : 80);
    return true;
}

std::vector<ForgeCalibrationRecord> pull_from_dashboard()
{
    const InventoryConfig cfg = inventory_config();
    if (!cfg.configured() || cfg.provider != "3dprintforge")
        return {};
    std::string host;
    int         port = 80;
    if (!split_url(cfg.url, host, port))
        return {};
    try {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(8, 0);
        httplib::Headers headers;
        if (!cfg.token.empty())
            headers.insert({ "Authorization", "Bearer " + cfg.token });
        auto res = cli.Get(kCalibrationPath, headers);
        if (!res || res->status != 200)
            return {};
        return parse_calibration_records(res->body);
    } catch (...) {
        return {};
    }
}

void push_to_dashboard(const ForgeCalibrationRecord& rec)
{
    const InventoryConfig cfg = inventory_config();
    if (!cfg.configured() || cfg.provider != "3dprintforge")
        return;
    std::string host;
    int         port = 80;
    if (!split_url(cfg.url, host, port))
        return;
    try {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(3, 0);
        cli.set_read_timeout(8, 0);
        httplib::Headers headers;
        if (!cfg.token.empty())
            headers.insert({ "Authorization", "Bearer " + cfg.token });
        // Body is a single-record JSON object (serialize a 1-element array, strip
        // the brackets) — keep it simple and contract-friendly.
        std::string arr = serialize_calibration_records({ rec });
        std::string body = (arr.size() >= 2) ? arr.substr(1, arr.size() - 2) : arr;
        cli.Post(kCalibrationPath, headers, body, "application/json");
    } catch (...) {
        // best-effort
    }
}

std::vector<ForgeCalibrationRecord> read_cache()
{
    try {
        boost::filesystem::path p(forge_calibration_cache_path());
        if (!boost::filesystem::exists(p))
            return {};
        boost::filesystem::ifstream f(p);
        std::stringstream ss;
        ss << f.rdbuf();
        return parse_calibration_records(ss.str());
    } catch (...) {
        return {};
    }
}

bool write_cache(const std::vector<ForgeCalibrationRecord>& records)
{
    try {
        boost::filesystem::path p(forge_calibration_cache_path());
        boost::filesystem::ofstream f(p, std::ios::trunc);
        f << serialize_calibration_records(records);
        return f.good();
    } catch (...) {
        return false;
    }
}

} // namespace

std::string forge_calibration_cache_path()
{
    return (boost::filesystem::path(data_dir()) / "forge_calibration.json").string();
}

std::vector<ForgeCalibrationRecord> load_cached_calibration_records()
{
    return read_cache();
}

std::vector<ForgeCalibrationRecord> load_calibration_records()
{
    std::vector<ForgeCalibrationRecord> records = read_cache();
    // Fold dashboard records over the local cache (dashboard wins on identity).
    for (const auto& r : pull_from_dashboard())
        records = upsert_calibration(std::move(records), r);
    return records;
}

bool save_calibration_record(const ForgeCalibrationRecord& rec)
{
    std::vector<ForgeCalibrationRecord> records = read_cache();
    records = upsert_calibration(std::move(records), rec);
    const bool ok = write_cache(records);
    push_to_dashboard(rec);
    return ok;
}

} // namespace Slic3r
