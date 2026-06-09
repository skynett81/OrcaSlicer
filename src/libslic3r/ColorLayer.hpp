#ifndef slic3r_ColorLayer_hpp_
#define slic3r_ColorLayer_hpp_

#include <cstdint>
#include <vector>
#include <utility>

namespace Slic3r {

// HueForge-style colour-layering core: predict the colour perceived when light
// passes through a stack of (partly translucent) filament layers, and pick how
// many layers reproduce a target shade. This is the algorithmic heart of the
// "Colour Layer" generator; the geometry/3MF export and GUI build on top of it.
//
// Model: each printed layer of thickness t of a filament attenuates what is
// behind it following Beer-Lambert. We express a filament's opacity by its
// Transmission Distance (TD) — the thickness at which it becomes ~opaque — via
// alpha = 1 - exp(-t / TD). Layers are composited back-to-front with the "over"
// operator, so a thick top layer hides what is below while thin layers let the
// colours beneath show through. (Re-implementation of the standard TD model used
// by HueForge/Kromacut; the physics is not copyrightable.)

struct ColorLayerRGB
{
    double r = 0.0; // 0..255
    double g = 0.0;
    double b = 0.0;
};

struct ColorLayerFilament
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    double  td_mm = 1.0; // transmission distance (mm); larger = more translucent
};

// Per-layer opacity from Beer-Lambert: 1 - exp(-thickness / TD). Clamped to [0,1].
double layer_alpha(double thickness_mm, double td_mm);

// Composite a stack over an opaque background. `layers` lists (filament,
// thickness_mm) from BOTTOM (first printed, nearest background) to TOP (last
// printed, nearest viewer). Returns the perceived colour.
ColorLayerRGB stack_color(const ColorLayerRGB&                                       background,
                          const std::vector<std::pair<ColorLayerFilament, double>>&  layers);

// For a single filament printed over a base colour, return the layer count
// (0..max_layers) whose stack best matches `target` (min squared RGB distance),
// at the given per-layer thickness. Useful for single-colour HueForge / depth
// shading. Ties resolve to the FEWER layers (less filament).
int best_layer_count(const ColorLayerRGB&     target,
                     const ColorLayerRGB&     base,
                     const ColorLayerFilament& filament,
                     double                   layer_height_mm,
                     int                      max_layers);

// Precompute the perceived colour after each printed height, given a per-layer
// filament schedule (layer_schedule[i] = the filament printed at layer i, bottom
// to top) and a uniform layer height. The returned palette has
// layer_schedule.size()+1 entries: palette[0] = base, palette[h] = the stack of
// the first h layers. This is the colour "ramp" a HueForge print can reproduce.
std::vector<ColorLayerRGB> height_palette(const std::vector<ColorLayerFilament>& layer_schedule,
                                          const ColorLayerRGB&                    base,
                                          double                                  layer_height_mm);

// Index into `palette` (i.e. the layer height) whose colour best matches `target`
// (min squared RGB distance). Ties resolve to the FEWER layers (less filament,
// shorter print). Returns 0 for an empty palette.
int pick_height(const ColorLayerRGB& target, const std::vector<ColorLayerRGB>& palette);

} // namespace Slic3r

#endif
