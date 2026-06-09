#include <catch2/catch_all.hpp>

#include "libslic3r/ForgePrinterMatch.hpp"

using namespace Slic3r;

TEST_CASE("match P2S fleet printer to its preset (vendor=connector type)", "[ForgePrinterMatch]")
{
    std::vector<std::string> presets = {
        "Bambu Lab P2S 0.2 nozzle",
        "Bambu Lab P2S 0.4 nozzle",
        "Bambu Lab P2S 0.6 nozzle",
        "Bambu Lab X1C 0.4 nozzle",
        "Snapmaker U1 (0.4 nozzle)",
    };
    // Dashboard sends vendor="bambu" (connector), model="P2S".
    REQUIRE(match_fleet_printer_preset("bambu", "P2S", presets) == "Bambu Lab P2S 0.4 nozzle");
}

TEST_CASE("match Snapmaker U1 (vendor=moonraker, not the brand)", "[ForgePrinterMatch]")
{
    std::vector<std::string> presets = {
        "Snapmaker U1 (0.2 nozzle)",
        "Snapmaker U1 (0.4 nozzle)",
        "Snapmaker U1 (0.4+0.6 nozzle)",
        "Bambu Lab P2S 0.4 nozzle",
    };
    // vendor "moonraker" doesn't appear in any preset -> model carries the match;
    // prefer the 0.4 single-nozzle, shortest over "0.4+0.6".
    REQUIRE(match_fleet_printer_preset("moonraker", "Snapmaker U1", presets) ==
            "Snapmaker U1 (0.4 nozzle)");
}

TEST_CASE("no preset contains the model -> empty", "[ForgePrinterMatch]")
{
    std::vector<std::string> presets = { "Bambu Lab X1C 0.4 nozzle", "Prusa MK4 0.4 nozzle" };
    REQUIRE(match_fleet_printer_preset("bambu", "P2S", presets).empty());
    REQUIRE(match_fleet_printer_preset("bambu", "", presets).empty()); // empty model
}

TEST_CASE("vendor overlap breaks ties toward the right brand", "[ForgePrinterMatch]")
{
    // Two brands share a model token "mini"; vendor disambiguates.
    std::vector<std::string> presets = {
        "Prusa MINI 0.4 nozzle",
        "Other MINI 0.4 nozzle",
    };
    REQUIRE(match_fleet_printer_preset("Prusa", "MINI", presets) == "Prusa MINI 0.4 nozzle");
}

TEST_CASE("falls back to a non-0.4 preset when no 0.4 exists", "[ForgePrinterMatch]")
{
    std::vector<std::string> presets = { "Bambu Lab P2S 0.6 nozzle", "Bambu Lab P2S 0.8 nozzle" };
    // No 0.4 -> shortest among model matches.
    REQUIRE(match_fleet_printer_preset("bambu", "P2S", presets) == "Bambu Lab P2S 0.6 nozzle");
}
