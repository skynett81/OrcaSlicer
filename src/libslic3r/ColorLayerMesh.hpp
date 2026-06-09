#ifndef slic3r_ColorLayerMesh_hpp_
#define slic3r_ColorLayerMesh_hpp_

#include <vector>
#include <admesh/stl.h> // indexed_triangle_set

#include "ColorLayer.hpp"

namespace Slic3r {

// Build a watertight relief slab from a per-pixel top-height map, for the
// HueForge-style Colour Layer generator. The grid is W x H pixels spaced
// `pixel_mm` apart; heights_mm[iy*W + ix] is the top surface z (mm) of that
// pixel (>0). The result is a closed terrain slab: a smooth top surface over the
// pixel-centre grid, a flat z=0 bottom, and perimeter walls — ready to slice and
// print with colour changes at the planned layer heights.
//
// Returns an empty set on invalid input (W<2, H<2, size mismatch, pixel_mm<=0).
// A 2x2 map yields a simple box (8 vertices, 12 triangles).
indexed_triangle_set its_from_heightmap(const std::vector<float>& heights_mm,
                                        int W, int H, double pixel_mm);

// Generation parameters for the Colour Layer (HueForge-style) generator.
struct ColorLayerParams
{
    // Filament printed at each relief layer, bottom -> top. Its colours + their
    // changes define both the reproducible colour ramp and the colour-swap Zs.
    std::vector<ColorLayerFilament> layer_schedule;
    ColorLayerRGB base{0.0, 0.0, 0.0}; // base/first colour beneath the relief
    float layer_height_mm = 0.08f;
    float pixel_mm        = 0.4f;       // model XY size per source pixel
    int   base_layers     = 3;          // solid base beneath the relief
};

// Result of generating a Colour Layer model.
struct ColorLayerResult
{
    indexed_triangle_set mesh;
    // Global layer indices (0-based) at which the filament changes — feed these
    // (as Z = layer * layer_height) to the colour-change G-code (CustomGCode).
    std::vector<int>     color_change_layers;
    int                  max_relief_layers = 0;
    bool                 ok                = false;
};

// Turn a W x H grid of target colours (row-major) into a printable relief: map
// each pixel to the height whose stacked colour best matches it (via the
// schedule's colour ramp), build the mesh (base + relief), and report the layers
// where the filament changes. Empty/!ok on invalid input.
ColorLayerResult generate_color_layer(const std::vector<ColorLayerRGB>& targets,
                                      int W, int H, const ColorLayerParams& params);

} // namespace Slic3r

#endif
