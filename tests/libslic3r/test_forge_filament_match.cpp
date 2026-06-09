#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeFilamentMatch.hpp"

using namespace Slic3r;

TEST_CASE("match PLA spool to a filament preset (vendor + basic preferred)", "[ForgeFilamentMatch]")
{
    std::vector<std::string> presets = {
        "Bambu PLA Basic @BBL P2S",
        "Bambu PLA Silk @BBL P2S",
        "Bambu PLA-CF @BBL P2S",
        "Generic PLA @System",
        "Bambu PETG HF @BBL P2S",
    };
    // material PLA, vendor "Bambu Lab" -> Bambu + PLA + basic.
    REQUIRE(match_spool_to_filament_preset("PLA", "Bambu Lab", presets) == "Bambu PLA Basic @BBL P2S");
}

TEST_CASE("material is required; no match -> empty", "[ForgeFilamentMatch]")
{
    std::vector<std::string> presets = { "Bambu PLA Basic @BBL P2S", "Generic PETG @System" };
    REQUIRE(match_spool_to_filament_preset("ABS", "Bambu", presets).empty());
    REQUIRE(match_spool_to_filament_preset("", "Bambu", presets).empty()); // empty material
}

TEST_CASE("falls back to material-only when vendor doesn't match", "[ForgeFilamentMatch]")
{
    std::vector<std::string> presets = {
        "Generic PETG @System",
        "Bambu PETG HF @BBL P2S",
    };
    // vendor "Polymaker" matches neither -> material PETG + prefer generic/basic + shortest.
    REQUIRE(match_spool_to_filament_preset("PETG", "Polymaker", presets) == "Generic PETG @System");
}

TEST_CASE("vendor overlap wins over generic", "[ForgeFilamentMatch]")
{
    std::vector<std::string> presets = {
        "Generic PLA @System",
        "Prusa PLA @MK4",
    };
    // vendor Prusa -> prefer the Prusa preset even though Generic is 'basic-ish'.
    REQUIRE(match_spool_to_filament_preset("PLA", "Prusa", presets) == "Prusa PLA @MK4");
}
