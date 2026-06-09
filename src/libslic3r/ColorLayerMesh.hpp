#ifndef slic3r_ColorLayerMesh_hpp_
#define slic3r_ColorLayerMesh_hpp_

#include <vector>
#include <admesh/stl.h> // indexed_triangle_set

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

} // namespace Slic3r

#endif
