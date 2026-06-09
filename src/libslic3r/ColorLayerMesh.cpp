#include "ColorLayerMesh.hpp"

namespace Slic3r {

indexed_triangle_set its_from_heightmap(const std::vector<float>& heights_mm,
                                        int W, int H, double pixel_mm)
{
    if (W < 2 || H < 2 || (int)heights_mm.size() != W * H || pixel_mm <= 0.0)
        return indexed_triangle_set{};

    const float p = static_cast<float>(pixel_mm);
    const int   N = W * H;

    auto z_at = [&](int ix, int iy) {
        const float z = heights_mm[iy * W + ix];
        return z > 0.001f ? z : 0.001f; // keep the slab solid (no zero-height holes)
    };

    std::vector<stl_vertex> verts;
    verts.reserve(2 * N);
    // Top grid [0..N): pixel centres at their heights. Bottom grid [N..2N): z = 0.
    for (int iy = 0; iy < H; ++iy)
        for (int ix = 0; ix < W; ++ix)
            verts.emplace_back((float)ix * p, (float)iy * p, z_at(ix, iy));
    for (int iy = 0; iy < H; ++iy)
        for (int ix = 0; ix < W; ++ix)
            verts.emplace_back((float)ix * p, (float)iy * p, 0.0f);

    auto T = [&](int ix, int iy) { return iy * W + ix; };       // top vertex index
    auto B = [&](int ix, int iy) { return N + iy * W + ix; };   // bottom vertex index

    std::vector<stl_triangle_vertex_indices> idx;
    idx.reserve(4 * (W - 1) * (H - 1) + 4 * (W - 1) + 4 * (H - 1));

    // Top surface (normals up).
    for (int iy = 0; iy < H - 1; ++iy)
        for (int ix = 0; ix < W - 1; ++ix) {
            const int a = T(ix, iy), b = T(ix + 1, iy), c = T(ix + 1, iy + 1), d = T(ix, iy + 1);
            idx.emplace_back(a, b, c);
            idx.emplace_back(a, c, d);
        }
    // Bottom surface (normals down — reversed winding).
    for (int iy = 0; iy < H - 1; ++iy)
        for (int ix = 0; ix < W - 1; ++ix) {
            const int a = B(ix, iy), b = B(ix + 1, iy), c = B(ix + 1, iy + 1), d = B(ix, iy + 1);
            idx.emplace_back(a, c, b);
            idx.emplace_back(a, d, c);
        }
    // Front (iy=0) and back (iy=H-1) walls.
    for (int ix = 0; ix < W - 1; ++ix) {
        const int t0 = T(ix, 0), t1 = T(ix + 1, 0), b0 = B(ix, 0), b1 = B(ix + 1, 0);
        idx.emplace_back(t0, b0, b1);
        idx.emplace_back(t0, b1, t1);
        const int u0 = T(ix, H - 1), u1 = T(ix + 1, H - 1), c0 = B(ix, H - 1), c1 = B(ix + 1, H - 1);
        idx.emplace_back(u0, c1, c0);
        idx.emplace_back(u0, u1, c1);
    }
    // Left (ix=0) and right (ix=W-1) walls.
    for (int iy = 0; iy < H - 1; ++iy) {
        const int t0 = T(0, iy), t1 = T(0, iy + 1), b0 = B(0, iy), b1 = B(0, iy + 1);
        idx.emplace_back(t0, b1, b0);
        idx.emplace_back(t0, t1, b1);
        const int u0 = T(W - 1, iy), u1 = T(W - 1, iy + 1), c0 = B(W - 1, iy), c1 = B(W - 1, iy + 1);
        idx.emplace_back(u0, c0, c1);
        idx.emplace_back(u0, c1, u1);
    }

    return indexed_triangle_set(idx, verts); // ctor sizes face properties
}

} // namespace Slic3r
