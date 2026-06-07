#include <catch2/catch_all.hpp>

#include <algorithm>

#include "libslic3r/ColorOrderOptimizer.hpp"

using namespace Slic3r;

TEST_CASE("color_cycle_cost sums the closed loop", "[ColorOrder]")
{
    // 3 filaments, asymmetric flush.
    std::vector<std::vector<double>> m = {
        {0, 10, 100},
        {10, 0, 10},
        {100, 10, 0},
    };
    // cycle 0->1->2->0 = 10 + 10 + 100 = 120
    REQUIRE(color_cycle_cost({0, 1, 2}, m) == 120.0);
    // cycle 0->2->1->0 = 100 + 10 + 10 = 120 (same loop reversed)
    REQUIRE(color_cycle_cost({0, 2, 1}, m) == 120.0);
}

TEST_CASE("optimize_color_order is trivial for <2 filaments", "[ColorOrder]")
{
    auto empty = optimize_color_order({});
    REQUIRE(empty.order.empty());
    REQUIRE(empty.method == "trivial");
    REQUIRE(empty.saved == 0.0);

    auto one = optimize_color_order({{0.0}});
    REQUIRE(one.order == std::vector<size_t>{0});
    REQUIRE(one.method == "trivial");
}

TEST_CASE("optimize_color_order finds the cheap loop and reports savings", "[ColorOrder]")
{
    // Two clusters of similar colours: {0,1} cheap to swap, {2,3} cheap to swap,
    // but crossing clusters is expensive. Listing them interleaved (0,2,1,3) is
    // costly; grouping them (0,1,2,3) is cheap.
    const double C = 100.0; // cross-cluster cost
    const double N = 5.0;   // near (same cluster) cost
    std::vector<std::vector<double>> m = {
        {0, N, C, C},
        {N, 0, C, C},
        {C, C, 0, N},
        {C, C, N, 0},
    };

    auto r = optimize_color_order(m);
    REQUIRE(r.method == "brute-force");

    // Optimal cycle keeps clusters adjacent: 0-1-...-2-3-... -> 2 near + 2 cross.
    REQUIRE(r.optimized_cost == 2 * N + 2 * C);
    // Optimised must never be worse than the load-as-listed baseline.
    REQUIRE(r.optimized_cost <= r.baseline_cost);
    REQUIRE(r.saved == r.baseline_cost - r.optimized_cost);
    REQUIRE(r.saved >= 0.0);

    // No order can beat the brute-force optimum.
    std::vector<size_t> any = {0, 2, 1, 3};
    REQUIRE(r.optimized_cost <= color_cycle_cost(any, m));
}

TEST_CASE("optimize_color_order order is a valid permutation", "[ColorOrder]")
{
    std::vector<std::vector<double>> m = {
        {0, 1, 2, 3, 4},
        {1, 0, 1, 2, 3},
        {2, 1, 0, 1, 2},
        {3, 2, 1, 0, 1},
        {4, 3, 2, 1, 0},
    };
    auto r = optimize_color_order(m);
    REQUIRE(r.order.size() == 5);
    std::vector<size_t> seen = r.order;
    std::sort(seen.begin(), seen.end());
    REQUIRE(seen == std::vector<size_t>{0, 1, 2, 3, 4});
}

TEST_CASE("optimize_color_order falls back to nn+2opt above brute_limit", "[ColorOrder]")
{
    // Build a 10x10 ring-ish matrix; use a small brute_limit so the heuristic path runs.
    const size_t n = 10;
    std::vector<std::vector<double>> m(n, std::vector<double>(n, 50.0));
    for (size_t i = 0; i < n; ++i) {
        m[i][i]           = 0.0;
        m[i][(i + 1) % n] = 1.0; // cheap to go to the next index -> identity ring is optimal
    }

    auto r = optimize_color_order(m, /*brute_limit*/ 8);
    REQUIRE(r.method == "nn+2opt");
    REQUIRE(r.order.size() == n);
    // The cheap ring (cost n) should be found; never worse than the baseline.
    REQUIRE(r.optimized_cost <= r.baseline_cost);
    REQUIRE(r.optimized_cost == static_cast<double>(n));
}
