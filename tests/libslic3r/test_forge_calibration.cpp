#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeCalibration.hpp"

using namespace Slic3r;

static ForgeCalibrationRecord rec(const std::string& printer, const std::string& mat,
                                  const std::string& vendor, double nozzle, double flow,
                                  int spool = -1)
{
    ForgeCalibrationRecord r;
    r.printer_id = printer;
    r.material   = mat;
    r.vendor     = vendor;
    r.nozzle_mm  = nozzle;
    r.flow_ratio = flow;
    r.spool_id   = spool;
    return r;
}

TEST_CASE("find_best_calibration prefers more specific matches", "[ForgeCalibration]")
{
    std::vector<ForgeCalibrationRecord> records = {
        rec("u1", "PLA", "", 0.4, 0.95),                 // 0: printer+material
        rec("u1", "PLA", "Bambu", 0.4, 0.98),            // 1: + vendor + nozzle
        rec("u1", "PLA", "Bambu", 0.4, 0.99, /*spool=*/7), // 2: exact spool
        rec("p2s", "PLA", "Bambu", 0.4, 0.90),           // 3: other printer
    };

    // Exact spool wins.
    int i = find_best_calibration(records, "u1", /*spool=*/7, "PLA", "Bambu", 0.4);
    REQUIRE(i == 2);

    // No spool -> vendor+nozzle match beats material-only.
    i = find_best_calibration(records, "u1", -1, "PLA", "Bambu", 0.4);
    REQUIRE(i == 1);

    // Unknown vendor -> falls back to printer+material (vendor wildcard).
    i = find_best_calibration(records, "u1", -1, "PLA", "", 0.4);
    REQUIRE(i >= 0);
    REQUIRE(records[i].printer_id == "u1");

    // Different material -> no match.
    REQUIRE(find_best_calibration(records, "u1", -1, "PETG", "Bambu", 0.4) == -1);
    // Different printer not present -> no match.
    REQUIRE(find_best_calibration(records, "voron", -1, "PLA", "Bambu", 0.4) == -1);
}

TEST_CASE("per-spool calibration is printer-independent", "[ForgeCalibration]")
{
    std::vector<ForgeCalibrationRecord> records = {
        rec("u1", "PLA", "Bambu", 0.4, 0.95),                  // printer-level on u1
        rec("u1", "PLA", "Bambu", 0.4, 0.99, /*spool=*/7),     // spool 7, saved on u1
    };
    // Spool 7 mounted on a DIFFERENT printer (p2s) still matches its record.
    int i = find_best_calibration(records, "p2s", /*spool=*/7, "PLA", "Bambu", 0.6);
    REQUIRE(i == 1);
    REQUIRE(records[i].flow_ratio == Catch::Approx(0.99));

    // Even with mismatched material/vendor, the physical spool wins.
    i = find_best_calibration(records, "voron", /*spool=*/7, "PETG", "", -1);
    REQUIRE(i == 1);
}

TEST_CASE("nozzle is a wildcard when unknown on either side", "[ForgeCalibration]")
{
    std::vector<ForgeCalibrationRecord> records = {
        rec("u1", "PLA", "Bambu", -1, 0.97), // unknown nozzle on record
    };
    // Target has 0.4; record nozzle unknown -> still matches.
    REQUIRE(find_best_calibration(records, "u1", -1, "PLA", "Bambu", 0.4) == 0);
}

TEST_CASE("upsert replaces same identity, appends otherwise", "[ForgeCalibration]")
{
    std::vector<ForgeCalibrationRecord> records = { rec("u1", "PLA", "Bambu", 0.4, 0.95) };

    // Same identity (printer+material+vendor+nozzle, no spool) -> replace.
    auto r2 = upsert_calibration(records, rec("u1", "PLA", "Bambu", 0.4, 0.99));
    REQUIRE(r2.size() == 1);
    REQUIRE(r2[0].flow_ratio == Catch::Approx(0.99));

    // Different nozzle -> new identity -> append.
    auto r3 = upsert_calibration(r2, rec("u1", "PLA", "Bambu", 0.2, 0.93));
    REQUIRE(r3.size() == 2);

    // Per-spool record is a distinct identity -> append.
    auto r4 = upsert_calibration(r3, rec("u1", "PLA", "Bambu", 0.4, 1.01, /*spool=*/7));
    REQUIRE(r4.size() == 3);
}

TEST_CASE("JSON round-trip", "[ForgeCalibration]")
{
    std::vector<ForgeCalibrationRecord> records = {
        rec("u1", "PLA", "Bambu", 0.4, 0.98),
        rec("p2s", "PETG", "", 0.4, -1),
    };
    records[0].pressure_advance = 0.020;
    records[1].max_volumetric_speed = 12.5;

    std::string json = serialize_calibration_records(records);
    auto back = parse_calibration_records(json);
    REQUIRE(back.size() == 2);
    REQUIRE(back[0].printer_id == "u1");
    REQUIRE(back[0].flow_ratio == Catch::Approx(0.98));
    REQUIRE(back[0].pressure_advance == Catch::Approx(0.020));
    REQUIRE(back[1].material == "PETG");
    REQUIRE(back[1].max_volumetric_speed == Catch::Approx(12.5));
    REQUIRE_FALSE(back[1].has_flow()); // flow not set -> not emitted -> not parsed
}

TEST_CASE("parse tolerates garbage and missing required fields", "[ForgeCalibration]")
{
    REQUIRE(parse_calibration_records("not json").empty());
    REQUIRE(parse_calibration_records("{}").empty());
    // entry missing printer_id is skipped
    REQUIRE(parse_calibration_records(R"([{"material":"PLA"}])").empty());
    // wrapped under "records"
    auto r = parse_calibration_records(R"({"records":[{"printer_id":"u1","material":"PLA"}]})");
    REQUIRE(r.size() == 1);
}
