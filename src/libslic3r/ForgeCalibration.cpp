#include "ForgeCalibration.hpp"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace Slic3r {

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool ieq(const std::string& a, const std::string& b) { return lower(a) == lower(b); }

bool nozzle_matches(double rec_nozzle, double target_nozzle)
{
    if (rec_nozzle < 0.0 || target_nozzle < 0.0)
        return true; // unknown nozzle on either side is a wildcard
    return std::abs(rec_nozzle - target_nozzle) < 1e-6;
}

// Higher = more specific. 0 means "does not match at all".
int match_score(const ForgeCalibrationRecord& r,
                const std::string&            printer_id,
                int                           target_spool_id,
                const std::string&            material,
                const std::string&            vendor,
                double                        nozzle_mm)
{
    // 1. Exact spool match wins outright, and is PRINTER-INDEPENDENT: a physical
    //    spool's calibration travels with the spool to whatever printer it is
    //    mounted on (per-spool calibration). Material/printer are not required.
    if (target_spool_id >= 0 && r.spool_id == target_spool_id)
        return 100;

    // 2. Otherwise fall back to printer + material matching.
    if (!ieq(r.printer_id, printer_id))
        return 0;
    if (!ieq(r.material, material))
        return 0;

    if (!nozzle_matches(r.nozzle_mm, nozzle_mm))
        return 0;

    const bool vendor_ok = vendor.empty() || r.vendor.empty() || ieq(r.vendor, vendor);
    if (!vendor_ok)
        return 0;

    int score = 10;                                  // printer + material
    if (!vendor.empty() && ieq(r.vendor, vendor)) score += 5; // exact vendor
    if (r.nozzle_mm >= 0.0 && nozzle_mm >= 0.0)    score += 3; // explicit nozzle
    return score;
}

bool same_identity(const ForgeCalibrationRecord& a, const ForgeCalibrationRecord& b)
{
    return ieq(a.printer_id, b.printer_id) && a.spool_id == b.spool_id &&
           ieq(a.material, b.material) && ieq(a.vendor, b.vendor) &&
           a.nozzle_mm == b.nozzle_mm;
}

} // namespace

int find_best_calibration(const std::vector<ForgeCalibrationRecord>& records,
                          const std::string&                         printer_id,
                          int                                        target_spool_id,
                          const std::string&                         material,
                          const std::string&                         vendor,
                          double                                     nozzle_mm)
{
    int best_idx   = -1;
    int best_score = 0;
    for (int i = 0; i < static_cast<int>(records.size()); ++i) {
        int s = match_score(records[i], printer_id, target_spool_id, material, vendor, nozzle_mm);
        if (s > best_score) {
            best_score = s;
            best_idx   = i;
        }
    }
    return best_idx;
}

std::vector<ForgeCalibrationRecord> upsert_calibration(std::vector<ForgeCalibrationRecord> records,
                                                       const ForgeCalibrationRecord&       rec)
{
    for (auto& r : records) {
        if (same_identity(r, rec)) {
            r = rec;
            return records;
        }
    }
    records.push_back(rec);
    return records;
}

std::vector<ForgeCalibrationRecord> parse_calibration_records(const std::string& json_body)
{
    std::vector<ForgeCalibrationRecord> out;
    try {
        auto root = nlohmann::json::parse(json_body);
        const nlohmann::json* arr = nullptr;
        if (root.is_array())
            arr = &root;
        else if (root.is_object() && root.contains("records") && root["records"].is_array())
            arr = &root["records"];
        if (!arr)
            return out;

        for (const auto& e : *arr) {
            if (!e.is_object())
                continue;
            ForgeCalibrationRecord r;
            r.printer_id = e.value("printer_id", std::string());
            r.material   = e.value("material", std::string());
            if (r.printer_id.empty() || r.material.empty())
                continue;
            r.vendor     = e.value("vendor", std::string());
            r.spool_id   = e.value("spool_id", -1);
            r.nozzle_mm  = e.value("nozzle_mm", -1.0);
            r.flow_ratio           = e.value("flow_ratio", -1.0);
            r.pressure_advance     = e.value("pressure_advance", -1.0);
            r.max_volumetric_speed = e.value("max_volumetric_speed", -1.0);
            r.updated_at = e.value("updated_at", std::string());
            r.source     = e.value("source", std::string());
            out.push_back(std::move(r));
        }
    } catch (...) {
        out.clear();
    }
    return out;
}

std::string serialize_calibration_records(const std::vector<ForgeCalibrationRecord>& records)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& r : records) {
        nlohmann::json e;
        e["printer_id"] = r.printer_id;
        e["material"]   = r.material;
        if (!r.vendor.empty())     e["vendor"]    = r.vendor;
        if (r.spool_id >= 0)       e["spool_id"]  = r.spool_id;
        if (r.nozzle_mm >= 0.0)    e["nozzle_mm"] = r.nozzle_mm;
        if (r.has_flow())          e["flow_ratio"] = r.flow_ratio;
        if (r.has_pa())            e["pressure_advance"] = r.pressure_advance;
        if (r.has_mvs())           e["max_volumetric_speed"] = r.max_volumetric_speed;
        if (!r.updated_at.empty()) e["updated_at"] = r.updated_at;
        if (!r.source.empty())     e["source"]    = r.source;
        arr.push_back(std::move(e));
    }
    return arr.dump();
}

} // namespace Slic3r
