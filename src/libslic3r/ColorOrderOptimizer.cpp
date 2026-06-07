#include "ColorOrderOptimizer.hpp"

#include <algorithm>
#include <limits>
#include <numeric>

namespace Slic3r {

double color_cycle_cost(const std::vector<size_t>&              order,
                        const std::vector<std::vector<double>>& m)
{
    const size_t n = order.size();
    if (n < 2)
        return 0.0;
    double total = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const size_t a = order[i];
        const size_t b = order[(i + 1) % n];
        if (a < m.size() && b < m[a].size())
            total += m[a][b];
    }
    return total;
}

namespace {

// Brute-force the optimal cycle. Index 0 is fixed to remove rotational duplicates.
std::vector<size_t> brute_force(const std::vector<std::vector<double>>& m)
{
    const size_t n = m.size();
    std::vector<size_t> rest(n - 1);
    std::iota(rest.begin(), rest.end(), size_t(1));

    std::vector<size_t> best;
    double best_cost = std::numeric_limits<double>::infinity();

    // std::next_permutation over `rest` enumerates every ordering of indices 1..n-1.
    std::sort(rest.begin(), rest.end());
    do {
        std::vector<size_t> order;
        order.reserve(n);
        order.push_back(0);
        order.insert(order.end(), rest.begin(), rest.end());
        const double c = color_cycle_cost(order, m);
        if (c < best_cost) {
            best_cost = c;
            best      = order;
        }
    } while (std::next_permutation(rest.begin(), rest.end()));

    return best;
}

std::vector<size_t> nearest_neighbour(const std::vector<std::vector<double>>& m)
{
    const size_t n = m.size();
    std::vector<bool>   visited(n, false);
    std::vector<size_t> order;
    order.reserve(n);
    order.push_back(0);
    visited[0] = true;
    for (size_t step = 1; step < n; ++step) {
        const size_t last     = order.back();
        size_t       next      = 0;
        double       best_dist = std::numeric_limits<double>::infinity();
        bool         found     = false;
        for (size_t j = 0; j < n; ++j) {
            if (!visited[j] && m[last][j] < best_dist) {
                best_dist = m[last][j];
                next      = j;
                found     = true;
            }
        }
        if (!found)
            break;
        order.push_back(next);
        visited[next] = true;
    }
    return order;
}

std::vector<size_t> two_opt(std::vector<size_t> order, const std::vector<std::vector<double>>& m)
{
    double best_cost = color_cycle_cost(order, m);
    bool   improved  = true;
    while (improved) {
        improved = false;
        for (size_t i = 1; i + 1 < order.size(); ++i) {
            for (size_t k = i + 1; k < order.size(); ++k) {
                std::vector<size_t> cand(order);
                std::reverse(cand.begin() + i, cand.begin() + k + 1);
                const double c = color_cycle_cost(cand, m);
                if (c < best_cost - 1e-9) {
                    order     = std::move(cand);
                    best_cost = c;
                    improved  = true;
                }
            }
        }
    }
    return order;
}

} // namespace

ColorOrderResult optimize_color_order(const std::vector<std::vector<double>>& m, size_t brute_limit)
{
    ColorOrderResult result;
    const size_t     n = m.size();

    if (n < 2) {
        if (n == 1)
            result.order = {0};
        result.method = "trivial";
        return result;
    }

    std::vector<size_t> identity(n);
    std::iota(identity.begin(), identity.end(), size_t(0));
    const double baseline = color_cycle_cost(identity, m);

    if (n <= brute_limit) {
        result.order  = brute_force(m);
        result.method = "brute-force";
    } else {
        result.order  = two_opt(nearest_neighbour(m), m);
        result.method = "nn+2opt";
    }

    const double opt = color_cycle_cost(result.order, m);
    result.baseline_cost  = baseline;
    result.optimized_cost = opt;
    result.saved          = std::max(0.0, baseline - opt);
    result.saved_pct      = baseline > 0.0 ? (result.saved / baseline) * 100.0 : 0.0;
    return result;
}

} // namespace Slic3r
