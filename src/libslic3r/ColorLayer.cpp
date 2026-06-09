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

std::vector<ColorLayerRGB> height_palette(const std::vector<ColorLayerFilament>& layer_schedule,
                                          const ColorLayerRGB&                    base,
                                          double                                  layer_height_mm)
{
    std::vector<ColorLayerRGB> palette;
    palette.reserve(layer_schedule.size() + 1);
    palette.push_back(base);
    // Incremental "over" compositing: each printed layer laid over the stack so far.
    ColorLayerRGB cur = base;
    for (const ColorLayerFilament& f : layer_schedule) {
        const double a = layer_alpha(layer_height_mm, f.td_mm);
        cur.r = a * (double)f.r + (1.0 - a) * cur.r;
        cur.g = a * (double)f.g + (1.0 - a) * cur.g;
        cur.b = a * (double)f.b + (1.0 - a) * cur.b;
        palette.push_back(cur);
    }
    return palette;
}

std::vector<ColorLayerRGB> downsample_to_color_grid(const uint8_t* rgba,
                                                    int src_w, int src_h, int max_dim,
                                                    int& out_w, int& out_h)
{
    out_w = out_h = 0;
    if (rgba == nullptr || src_w < 1 || src_h < 1 || max_dim < 1)
        return {};

    // Scale the longest side to max_dim (no upscaling), preserve aspect, >= 1.
    double scale = 1.0;
    const int longest = std::max(src_w, src_h);
    if (longest > max_dim)
        scale = (double)max_dim / (double)longest;
    out_w = std::max(1, (int)std::lround(src_w * scale));
    out_h = std::max(1, (int)std::lround(src_h * scale));

    std::vector<ColorLayerRGB> grid((size_t)out_w * out_h);
    for (int oy = 0; oy < out_h; ++oy) {
        const int y0 = (int)((double)oy * src_h / out_h);
        const int y1 = std::max(y0 + 1, (int)((double)(oy + 1) * src_h / out_h));
        for (int ox = 0; ox < out_w; ++ox) {
            const int x0 = (int)((double)ox * src_w / out_w);
            const int x1 = std::max(x0 + 1, (int)((double)(ox + 1) * src_w / out_w));
            double r = 0, g = 0, b = 0;
            long   n = 0;
            for (int y = y0; y < y1 && y < src_h; ++y)
                for (int x = x0; x < x1 && x < src_w; ++x) {
                    const uint8_t* p = rgba + ((size_t)y * src_w + x) * 4;
                    r += p[0]; g += p[1]; b += p[2];
                    ++n;
                }
            ColorLayerRGB& c = grid[(size_t)oy * out_w + ox];
            if (n > 0) { c.r = r / n; c.g = g / n; c.b = b / n; }
        }
    }
    return grid;
}

int pick_height(const ColorLayerRGB& target, const std::vector<ColorLayerRGB>& palette)
{
    int    best_h    = 0;
    double best_dist = -1.0;
    for (int h = 0; h < (int)palette.size(); ++h) {
        const double dr = palette[h].r - target.r;
        const double dg = palette[h].g - target.g;
        const double db = palette[h].b - target.b;
        const double dist = dr * dr + dg * dg + db * db;
        if (best_dist < 0.0 || dist < best_dist) { // strict < keeps fewest layers on ties
            best_dist = dist;
            best_h    = h;
        }
    }
    return best_h;
}

} // namespace Slic3r
