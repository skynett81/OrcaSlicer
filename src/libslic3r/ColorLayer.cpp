#include "ColorLayer.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

double layer_alpha(double thickness_mm, double td_mm)
{
    if (thickness_mm <= 0.0)
        return 0.0;
    if (td_mm <= 0.0)
        return 1.0; // zero TD => fully opaque
    const double a = 1.0 - std::exp(-thickness_mm / td_mm);
    return std::min(1.0, std::max(0.0, a));
}

ColorLayerRGB stack_color(const ColorLayerRGB&                                      background,
                          const std::vector<std::pair<ColorLayerFilament, double>>& layers)
{
    ColorLayerRGB out = background;
    // Composite bottom-to-top: each layer's colour is laid "over" the result so
    // far with its Beer-Lambert opacity.
    for (const auto& lt : layers) {
        const ColorLayerFilament& f = lt.first;
        const double a = layer_alpha(lt.second, f.td_mm);
        out.r = a * (double)f.r + (1.0 - a) * out.r;
        out.g = a * (double)f.g + (1.0 - a) * out.g;
        out.b = a * (double)f.b + (1.0 - a) * out.b;
    }
    return out;
}

int best_layer_count(const ColorLayerRGB&      target,
                     const ColorLayerRGB&      base,
                     const ColorLayerFilament& filament,
                     double                    layer_height_mm,
                     int                       max_layers)
{
    int    best_n    = 0;
    double best_dist = -1.0;

    std::vector<std::pair<ColorLayerFilament, double>> stack;
    for (int n = 0; n <= max_layers; ++n) {
        const ColorLayerRGB c = stack_color(base, stack);
        const double dr = c.r - target.r, dg = c.g - target.g, db = c.b - target.b;
        const double dist = dr * dr + dg * dg + db * db;
        // Strictly-less keeps the first (fewest-layers) winner on ties.
        if (best_dist < 0.0 || dist < best_dist) {
            best_dist = dist;
            best_n    = n;
        }
        stack.emplace_back(filament, layer_height_mm);
    }
    return best_n;
}

} // namespace Slic3r
