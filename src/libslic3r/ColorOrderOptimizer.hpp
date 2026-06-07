#ifndef slic3r_ColorOrderOptimizer_hpp_
#define slic3r_ColorOrderOptimizer_hpp_

#include <vector>
#include <string>
#include <cstddef>

namespace Slic3r {

// Result of optimising the filament colour order to minimise multi-colour purge.
struct ColorOrderResult
{
    // Permutation of the input indices forming the optimised cycle. order[k] is the
    // original filament index printed in cycle position k.
    std::vector<size_t> order;
    // Total flush of the identity (load-as-listed) cycle.
    double baseline_cost = 0.0;
    // Total flush of the optimised cycle.
    double optimized_cost = 0.0;
    // max(0, baseline_cost - optimized_cost).
    double saved = 0.0;
    // saved / baseline_cost * 100 (0 when baseline is 0).
    double saved_pct = 0.0;
    // "trivial" | "brute-force" | "nn+2opt".
    std::string method;
};

// Total flush around the closed cycle order[0]->order[1]->...->order[n-1]->order[0].
// flush_matrix[i][j] is the purge cost of changing from filament i to filament j.
double color_cycle_cost(const std::vector<size_t>&                order,
                        const std::vector<std::vector<double>>&   flush_matrix);

// Find the filament order that minimises total purge for a repeating multi-colour
// print. The flush between two colours is asymmetric (dark->light usually costs more),
// so this is an asymmetric TSP over the colour set. flush_matrix must be square
// (n x n); diagonal entries are ignored. For n <= brute_limit the optimum is
// brute-forced, otherwise nearest-neighbour + 2-opt is used. Port of the
// 3DPrintForge color-order.js model, fed by the slicer's own flush_volumes_matrix.
ColorOrderResult optimize_color_order(const std::vector<std::vector<double>>& flush_matrix,
                                      size_t                                  brute_limit = 8);

} // namespace Slic3r

#endif
