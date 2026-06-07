#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeSpool.hpp"

using namespace Slic3r;

// A bare array shaped like the real GET /api/inventory/spools response, with a
// fully-populated spool and one carrying null fields (no profile joined).
static const char* SAMPLE = R"([
  {"id":1,"material":"PLA","color_name":"A00-B8","color_hex":"0086D6",
   "remaining_weight_g":255.09,"initial_weight_g":1000,"cost":299,"density":1.24,
   "vendor_name":"Bambu Lab","profile_name":"PLA Basic","location":"AMS2 PRO",
   "printer_id":"3dsky","ams_unit":0,"ams_tray":0,"archived":0},
  {"id":2,"material":"PETG","color_name":null,"color_hex":"#FF0000",
   "remaining_weight_g":1000,"initial_weight_g":1000,"cost":null,"density":null,
   "vendor_name":null,"profile_name":null,"location":null,"printer_id":null,
   "ams_unit":null,"ams_tray":null,"archived":1}
])";

TEST_CASE("parse_forge_spools reads a populated spool", "[ForgeSpool]")
{
    auto spools = parse_forge_spools(SAMPLE);
    REQUIRE(spools.size() == 2);

    const ForgeSpool& a = spools[0];
    REQUIRE(a.id == 1);
    REQUIRE(a.material == "PLA");
    REQUIRE(a.color_name == "A00-B8");
    REQUIRE(a.color_hex == "0086D6");        // no leading '#'
    REQUIRE_THAT(a.remaining_g, Catch::Matchers::WithinAbs(255.09, 0.001));
    REQUIRE_THAT(a.initial_g, Catch::Matchers::WithinAbs(1000.0, 0.001));
    REQUIRE_THAT(a.cost, Catch::Matchers::WithinAbs(299.0, 0.001));
    REQUIRE_THAT(a.density, Catch::Matchers::WithinAbs(1.24, 0.001));
    REQUIRE(a.vendor == "Bambu Lab");
    REQUIRE(a.location == "AMS2 PRO");
    REQUIRE(a.ams_unit == 0);
    REQUIRE(a.ams_tray == 0);
    REQUIRE_FALSE(a.archived);
}

TEST_CASE("parse_forge_spools tolerates null fields and strips '#'", "[ForgeSpool]")
{
    auto spools = parse_forge_spools(SAMPLE);
    REQUIRE(spools.size() == 2);

    const ForgeSpool& b = spools[1];
    REQUIRE(b.id == 2);
    REQUIRE(b.material == "PETG");
    REQUIRE(b.color_name.empty());           // null -> empty
    REQUIRE(b.color_hex == "FF0000");        // leading '#' stripped
    REQUIRE(b.cost == -1.0);                  // null -> unknown sentinel
    REQUIRE(b.density == -1.0);
    REQUIRE(b.vendor.empty());
    REQUIRE(b.location.empty());
    REQUIRE(b.ams_unit == -1);
    REQUIRE(b.archived);
}

TEST_CASE("cost_per_gram derives from cost and initial weight", "[ForgeSpool]")
{
    auto spools = parse_forge_spools(SAMPLE);
    // 299 / 1000 g
    REQUIRE_THAT(spools[0].cost_per_gram(), Catch::Matchers::WithinAbs(0.299, 1e-6));
    // null cost -> -1
    REQUIRE(spools[1].cost_per_gram() == -1.0);
}

TEST_CASE("parse_forge_spools accepts an object wrapping the array", "[ForgeSpool]")
{
    auto rows   = parse_forge_spools(R"({"rows":[{"id":7,"material":"ABS"}]})");
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == 7);
    REQUIRE(rows[0].material == "ABS");

    auto spools = parse_forge_spools(R"({"spools":[{"id":8}]})");
    REQUIRE(spools.size() == 1);
    REQUIRE(spools[0].id == 8);
}

TEST_CASE("parse_forge_spools never throws on malformed input", "[ForgeSpool]")
{
    REQUIRE(parse_forge_spools("").empty());
    REQUIRE(parse_forge_spools("not json").empty());
    REQUIRE(parse_forge_spools("{}").empty());
    REQUIRE(parse_forge_spools("42").empty());
    // Non-object array entries are skipped, valid ones kept.
    auto mixed = parse_forge_spools(R"([1, "x", {"id":9}])");
    REQUIRE(mixed.size() == 1);
    REQUIRE(mixed[0].id == 9);
}
