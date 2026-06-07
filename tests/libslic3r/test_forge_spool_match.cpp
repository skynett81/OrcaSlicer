#include <catch2/catch_all.hpp>

#include "libslic3r/ForgeSpoolMatch.hpp"

using namespace Slic3r;

static ForgeSpool spool(int id, const char* hex, const char* mat, double remaining, bool archived = false)
{
    ForgeSpool s;
    s.id          = id;
    s.color_hex   = hex;
    s.material    = mat;
    s.remaining_g = remaining;
    s.archived    = archived;
    return s;
}

TEST_CASE("match sums remaining across same-colour spools", "[ForgeSpoolMatch]")
{
    std::vector<ForgeSpool> inv = {
        spool(1, "0086D6", "PLA", 200.0),
        spool(2, "0086D6", "PLA", 150.0),
        spool(3, "FF0000", "PLA", 500.0),
    };
    std::vector<FilamentNeed> needs = { {"0086D6", "PLA", 300.0} };

    auto r = match_filaments_to_spools(needs, inv);
    REQUIRE(r.size() == 1);
    REQUIRE(r[0].matched);
    REQUIRE(r[0].available_g == 350.0);          // 200 + 150
    REQUIRE(r[0].sufficient);                     // 350 >= 300
    REQUIRE(r[0].deficit_g == 0.0);
    REQUIRE(r[0].spool_ids == std::vector<int>{1, 2});
}

TEST_CASE("match reports a deficit when stock is short", "[ForgeSpoolMatch]")
{
    std::vector<ForgeSpool> inv = { spool(1, "0086D6", "PLA", 100.0) };
    std::vector<FilamentNeed> needs = { {"#0086d6", "pla", 250.0} }; // case + '#' insensitive

    auto r = match_filaments_to_spools(needs, inv);
    REQUIRE(r[0].matched);
    REQUIRE(r[0].available_g == 100.0);
    REQUIRE_FALSE(r[0].sufficient);
    REQUIRE(r[0].deficit_g == 150.0);
}

TEST_CASE("material constrains the match when specified", "[ForgeSpoolMatch]")
{
    std::vector<ForgeSpool> inv = {
        spool(1, "0086D6", "PLA",  500.0),
        spool(2, "0086D6", "PETG", 500.0),
    };
    // Need PETG specifically -> only spool 2 counts.
    auto r = match_filaments_to_spools({ {"0086D6", "PETG", 100.0} }, inv);
    REQUIRE(r[0].matched);
    REQUIRE(r[0].spool_ids == std::vector<int>{2});

    // Empty material -> both colours count.
    auto r2 = match_filaments_to_spools({ {"0086D6", "", 100.0} }, inv);
    REQUIRE(r2[0].available_g == 1000.0);
}

TEST_CASE("archived spools are ignored", "[ForgeSpoolMatch]")
{
    std::vector<ForgeSpool> inv = {
        spool(1, "0086D6", "PLA", 1000.0, /*archived*/ true),
        spool(2, "0086D6", "PLA",  50.0),
    };
    auto r = match_filaments_to_spools({ {"0086D6", "PLA", 100.0} }, inv);
    REQUIRE(r[0].available_g == 50.0);            // archived 1000g excluded
    REQUIRE_FALSE(r[0].sufficient);
}

TEST_CASE("no match leaves sufficiency unknown (false), not a false 'ok'", "[ForgeSpoolMatch]")
{
    std::vector<ForgeSpool> inv = { spool(1, "00FF00", "PLA", 1000.0) };
    auto r = match_filaments_to_spools({ {"0086D6", "PLA", 10.0} }, inv);
    REQUIRE_FALSE(r[0].matched);
    REQUIRE_FALSE(r[0].sufficient);               // unmatched != sufficient
    REQUIRE(r[0].available_g == 0.0);
    REQUIRE(r[0].deficit_g == 0.0);               // unknown, not a deficit
    REQUIRE(r[0].spool_ids.empty());
}
