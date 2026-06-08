#include <catch2/catch_all.hpp>

#include "libslic3r/ColorLayer.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

TEST_CASE("layer_alpha follows Beer-Lambert and clamps", "[ColorLayer]")
{
    REQUIRE(layer_alpha(0.0, 1.0) == 0.0);          // no thickness -> transparent
    REQUIRE(layer_alpha(1.0, 0.0) == 1.0);          // zero TD -> opaque
    // 1 - exp(-1) ~= 0.6321 at thickness == TD
    REQUIRE_THAT(layer_alpha(1.0, 1.0), WithinAbs(0.63212, 1e-4));
    // Thickness >> TD -> ~1
    REQUIRE(layer_alpha(100.0, 1.0) > 0.999);
}

TEST_CASE("stack_color: empty stack returns the background", "[ColorLayer]")
{
    ColorLayerRGB bg{10, 20, 30};
    ColorLayerRGB c = stack_color(bg, {});
    REQUIRE(c.r == 10.0);
    REQUIRE(c.g == 20.0);
    REQUIRE(c.b == 30.0);
}

TEST_CASE("stack_color: a thick opaque-ish layer hides the background", "[ColorLayer]")
{
    ColorLayerRGB bg{0, 0, 0};
    ColorLayerFilament white{255, 255, 255, 0.1}; // small TD -> opaque quickly
    auto c = stack_color(bg, { {white, 2.0} });    // 2mm >> TD
    REQUIRE(c.r > 254.0);
    REQUIRE(c.g > 254.0);
    REQUIRE(c.b > 254.0);
}

TEST_CASE("stack_color: a thin layer blends toward, not fully to, the filament", "[ColorLayer]")
{
    ColorLayerRGB bg{0, 0, 0};
    ColorLayerFilament white{255, 255, 255, 1.0};
    auto c = stack_color(bg, { {white, 0.2} });    // thin -> partial
    const double a = layer_alpha(0.2, 1.0);        // ~0.181
    REQUIRE_THAT(c.r, WithinAbs(a * 255.0, 1e-6));
    REQUIRE(c.r > 0.0);
    REQUIRE(c.r < 255.0);
}

TEST_CASE("stack_color: top layer dominates over a lower layer", "[ColorLayer]")
{
    ColorLayerRGB bg{0, 0, 0};
    ColorLayerFilament red{255, 0, 0, 0.1};
    ColorLayerFilament blue{0, 0, 255, 0.1};
    // red at bottom, blue (thick, opaque) on top -> looks blue
    auto c = stack_color(bg, { {red, 1.0}, {blue, 1.0} });
    REQUIRE(c.b > 254.0);
    REQUIRE(c.r < 1.0);
}

TEST_CASE("best_layer_count picks the closest stack and prefers fewer layers", "[ColorLayer]")
{
    ColorLayerRGB base{0, 0, 0};
    ColorLayerFilament white{255, 255, 255, 0.1};
    // Target near-white -> needs several opaque layers.
    int n_white = best_layer_count(ColorLayerRGB{250, 250, 250}, base, white, 0.2, 10);
    REQUIRE(n_white >= 1);

    // Target == base (black) -> zero layers is exact.
    int n_zero = best_layer_count(base, base, white, 0.2, 10);
    REQUIRE(n_zero == 0);

    // More layers never strays further from a white target than zero layers.
    auto c_best = stack_color(base, std::vector<std::pair<ColorLayerFilament,double>>(n_white, {white, 0.2}));
    REQUIRE(c_best.r >= stack_color(base, {}).r); // monotone toward white
}
