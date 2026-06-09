#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeFilamentManager.hpp"

using namespace Slic3r;

static ForgeSpool make_spool(int id, const std::string& mat, const std::string& vendor,
                             double remaining, double initial)
{
    ForgeSpool s;
    s.id          = id;
    s.material    = mat;
    s.vendor      = vendor;
    s.remaining_g = remaining;
    s.initial_g   = initial;
    return s;
}

TEST_CASE("percent remaining and stock status", "[ForgeFilamentManager]")
{
    REQUIRE(spool_percent_remaining(make_spool(1, "PLA", "Bambu", 500, 1000)) == Catch::Approx(50.0));
    REQUIRE(spool_percent_remaining(make_spool(2, "PLA", "Bambu", -1, 1000)) == Catch::Approx(-1.0));

    // 5% remaining with a 10% threshold -> Low.
    REQUIRE(spool_stock_status(make_spool(3, "PLA", "Bambu", 50, 1000), 0.10) == StockStatus::Low);
    // 50% remaining -> OK.
    REQUIRE(spool_stock_status(make_spool(4, "PLA", "Bambu", 500, 1000), 0.10) == StockStatus::OK);
    // Empty -> OutOfStock.
    REQUIRE(spool_stock_status(make_spool(5, "PLA", "Bambu", 0, 1000), 0.10) == StockStatus::OutOfStock);
    // Unknown remaining -> Unknown.
    REQUIRE(spool_stock_status(make_spool(6, "PLA", "Bambu", -1, 1000), 0.10) == StockStatus::Unknown);
}

TEST_CASE("build_filament_rows drops archived and sorts", "[ForgeFilamentManager]")
{
    std::vector<ForgeSpool> spools = {
        make_spool(1, "PETG", "Bambu", 800, 1000),
        make_spool(2, "PLA",  "Polymaker", 200, 1000),
        make_spool(3, "PLA",  "Bambu", 900, 1000),
        make_spool(4, "PLA",  "Bambu", 100, 1000),
    };
    spools[1].archived = true; // Polymaker PLA archived

    auto rows = build_filament_rows(spools, 0.10, /*include_archived=*/false);
    REQUIRE(rows.size() == 3); // archived dropped

    // Sorted by material (PETG < PLA alphabetically); within PLA/Bambu, 900 before 100.
    REQUIRE(rows[0].material == "PETG");
    REQUIRE(rows[1].material == "PLA");
    REQUIRE(rows[1].vendor == "Bambu");
    REQUIRE(rows[1].remaining_g == Catch::Approx(900));
    REQUIRE(rows[2].material == "PLA");
    REQUIRE(rows[2].remaining_g == Catch::Approx(100));

    // include_archived brings it back.
    REQUIRE(build_filament_rows(spools, 0.10, true).size() == 4);
}

TEST_CASE("resolve_spool_preset prefers explicit profile_name", "[ForgeFilamentManager]")
{
    std::vector<std::string> presets = {
        "Bambu PLA Basic @BBL P2S",
        "My Tuned PLA @BBL P2S",
        "Generic PLA @System",
    };
    ForgeSpool s = make_spool(1, "PLA", "Bambu", 500, 1000);
    s.profile_name = "My Tuned PLA @BBL P2S";
    // Explicit, valid profile_name wins over token matching.
    REQUIRE(resolve_spool_preset(s, presets) == "My Tuned PLA @BBL P2S");

    // profile_name not in the list -> fall back to material+vendor match.
    s.profile_name = "Deleted Preset";
    REQUIRE(resolve_spool_preset(s, presets) == "Bambu PLA Basic @BBL P2S");

    // No profile_name -> token match.
    s.profile_name.clear();
    REQUIRE(resolve_spool_preset(s, presets) == "Bambu PLA Basic @BBL P2S");
}
