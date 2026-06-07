// test_mixed_filament.cpp — Full Spectrum / Mixed Color engine verification.
// Deterministic checks of MixedFilamentManager: virtual-filament identity and
// the LayerCycle cadence that resolve() applies during slicing (the core logic
// the apply_mm_segmentation resolver depends on). No GUI / painting needed.

#include <catch2/catch_all.hpp>

#include "libslic3r/MixedFilament.hpp"

#include <vector>
#include <string>

using namespace Slic3r;

static std::vector<std::string> two_colors() { return { "#FF0000", "#0000FF" }; }

TEST_CASE("MixedFilamentManager virtual identity", "[MixedFilament]")
{
    MixedFilamentManager mgr;
    const size_t num_physical = 2;
    mgr.add_custom_filament(1, 2, 50, two_colors());

    SECTION("one enabled mixed filament -> total = physical + 1") {
        REQUIRE(mgr.enabled_count() == 1);
        REQUIRE(mgr.total_filaments(num_physical) == 3);
    }
    SECTION("the virtual id sits above the physical ids") {
        REQUIRE(mgr.is_mixed(3, num_physical));        // virtual
        REQUIRE_FALSE(mgr.is_mixed(1, num_physical));  // physical
        REQUIRE_FALSE(mgr.is_mixed(2, num_physical));  // physical
        REQUIRE(mgr.mixed_index_from_filament_id(3, num_physical) == 0);
    }
    SECTION("physical ids resolve to themselves unchanged") {
        REQUIRE(mgr.resolve(1, num_physical, 0) == 1);
        REQUIRE(mgr.resolve(2, num_physical, 7) == 2);
    }
}

TEST_CASE("MixedFilament LayerCycle cadence", "[MixedFilament]")
{
    const size_t num_physical = 2;

    SECTION("1:1 ratio alternates A,B,A,B per layer") {
        MixedFilamentManager mgr;
        mgr.add_custom_filament(1, 2, 50, two_colors());
        const unsigned vid = 3;
        const unsigned expect[6] = { 1, 2, 1, 2, 1, 2 };
        for (int layer = 0; layer < 6; ++layer)
            REQUIRE(mgr.resolve(vid, num_physical, layer) == expect[layer]);
    }

    SECTION("2:1 ratio gives A,A,B repeating") {
        MixedFilamentManager mgr;
        mgr.add_custom_filament(1, 2, 33, two_colors());
        // Set the cadence on the row directly.
        REQUIRE_FALSE(mgr.mixed_filaments().empty());
        mgr.mixed_filaments().back().ratio_a = 2;
        mgr.mixed_filaments().back().ratio_b = 1;
        const unsigned vid = 3;
        const unsigned expect[6] = { 1, 1, 2, 1, 1, 2 };
        for (int layer = 0; layer < 6; ++layer)
            REQUIRE(mgr.resolve(vid, num_physical, layer) == expect[layer]);
    }

    SECTION("negative layer index is handled (safe modulo)") {
        MixedFilamentManager mgr;
        mgr.add_custom_filament(1, 2, 50, two_colors());
        // -1 mod 2 must map cleanly, not crash or go out of range.
        const unsigned r = mgr.resolve(3, num_physical, -1);
        REQUIRE(r >= 1);
        REQUIRE(r <= 2);
    }
}

TEST_CASE("MixedFilament serialize/load round-trip", "[MixedFilament]")
{
    // The serialized form stores the row's identity + mix_b_percent (the cadence
    // ratio is derived from it), component ids, distribution mode, etc. — so we
    // round-trip via the values add_custom_filament sets, not hand-poked ratios.
    MixedFilamentManager a;
    a.add_custom_filament(1, 2, 50, two_colors());
    const std::string blob = a.serialize_custom_entries();
    REQUIRE_FALSE(blob.empty());

    MixedFilamentManager b;
    b.load_custom_entries(blob, two_colors());
    REQUIRE(b.enabled_count() == a.enabled_count());
    // The reloaded row must reproduce the same per-layer resolution as the original.
    for (int layer = 0; layer < 6; ++layer)
        REQUIRE(b.resolve(3, 2, layer) == a.resolve(3, 2, layer));
}
