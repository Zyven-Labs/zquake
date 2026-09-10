#include "app/rt_scene.hpp"
#include <cstring>
#include <cmath>
#include <cstdio>

namespace zq::app {

namespace {
constexpr std::uint32_t kMaxAtlasWidth = 2048;
} // namespace


void RtSceneBuilder::Clear() {
    triangles_.clear();
    tile_infos_.clear();
    atlas_.rgba.clear();
    atlas_.width = atlas_.height = 0;
    cursor_x_ = cursor_y_ = row_h_ = 0;
}

std::uint32_t RtSceneBuilder::RegisterTexture(const uint8_t* rgba, uint32_t w, uint32_t h) {
    // NOTE: no pointer-keyed dedup here: callers pass a temporary RGBA buffer
    // whose heap address may be reused across meshes, which would collapse
    // distinct textures onto one tile. Register every mesh's texture as its
    // own tile (correctness over atlas size).
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    uint32_t tile = (uint32_t)tile_infos_.size();

    // Ensure the atlas is large enough to hold this tile (shelf packing: grow
    // only downward, never reshape/clear so earlier tiles stay put).
    if (atlas_.rgba.empty()) { atlas_.width = kMaxAtlasWidth; atlas_.height = 1024; atlas_.rgba.assign((size_t)atlas_.width * atlas_.height * 4, 255); }

    // If the tile doesn't fit on the current row, move to the next row.
    if (cursor_x_ + w > atlas_.width) {
        cursor_x_ = 0;
        cursor_y_ += row_h_;
        row_h_ = 0;
    }
    // Grow downward if needed.
    if (cursor_y_ + h > atlas_.height) {
        std::uint32_t nh = cursor_y_ + h;
        std::vector<uint8_t> grown((size_t)atlas_.width * nh * 4, 255);
        std::memcpy(grown.data(), atlas_.rgba.data(), atlas_.rgba.size());
        atlas_.rgba.swap(grown);
        atlas_.height = nh;
    }
    uint32_t u = cursor_x_, v = cursor_y_;
    // Copy the texture at its native size (no resampling).
    for (std::uint32_t y = 0; y < h; y++) {
        std::memcpy(&atlas_.rgba[((size_t)(v + y) * atlas_.width + u) * 4],
                    rgba + (size_t)y * w * 4, (size_t)w * 4);
    }
    cursor_x_ += w;
    row_h_ = std::max(row_h_, h);

    tile_infos_.push_back(zq::render::RtTileInfo{ (float)u, (float)v, (float)w, (float)h });
    return tile;
}

std::uint32_t RtSceneBuilder::AddMesh(const void* vertices, std::size_t vertexCount,
                             std::size_t vertexStride,
                             const std::uint32_t* indices, std::size_t indexCount,
                             const uint8_t* texRgba, uint32_t texW, uint32_t texH,
                             const float model[16]) {
    if (!vertices || !indices || indexCount < 3) return 0;
    uint32_t tile = RegisterTexture(texRgba, texW, texH);

    const uint8_t* v = static_cast<const uint8_t*>(vertices);
    auto vertPos = [&](std::size_t i, float out[3]) {
        const float* p = reinterpret_cast<const float*>(v + i * vertexStride);
        float x = p[0], y = p[1], z = p[2];
        if (model) {
            float w = model[3]*x + model[7]*y + model[11]*z + model[15];
            if (w == 0.0f) w = 1.0f;
            out[0] = (model[0]*x + model[4]*y + model[8]*z + model[12]) / w;
            out[1] = (model[1]*x + model[5]*y + model[9]*z + model[13]) / w;
            out[2] = (model[2]*x + model[6]*y + model[10]*z + model[14]) / w;
        } else {
            out[0] = x; out[1] = y; out[2] = z;
        }
    };
    auto vertUV = [&](std::size_t i, float out[2]) {
        const float* p = reinterpret_cast<const float*>(v + i * vertexStride + 12); // uv after pos[3]
        out[0] = p[0]; out[1] = p[1];
    };

    for (std::size_t k = 0; k + 2 < indexCount; k += 3) {
        std::uint32_t i0 = indices[k], i1 = indices[k+1], i2 = indices[k+2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
        zq::render::RtTriangle t;
        vertPos(i0, t.p0); vertPos(i1, t.p1); vertPos(i2, t.p2);
        vertUV(i0, t.uv0); vertUV(i1, t.uv1); vertUV(i2, t.uv2);
        t.tex = tile;
        t.light = 0;
        float e1x=t.p1[0]-t.p0[0],e1y=t.p1[1]-t.p0[1],e1z=t.p1[2]-t.p0[2];
        float e2x=t.p2[0]-t.p0[0],e2y=t.p2[1]-t.p0[1],e2z=t.p2[2]-t.p0[2];
        float nx=e1y*e2z-e1z*e2y,ny=e1z*e2x-e1x*e2z,nz=e1x*e2y-e1y*e2x;
        if (nx*nx+ny*ny+nz*nz < 1e-12f) continue; // skip degenerate
        triangles_.push_back(t);
    }
    return tile;
}

std::vector<zq::render::RtTriangle> RtSceneBuilder::Triangles() {
    return triangles_;
}

void RtSceneBuilder::AddMeshWithTile(const void* vertices, std::size_t vertexCount,
                                     std::size_t vertexStride,
                                     const std::uint32_t* indices, std::size_t indexCount,
                                     std::uint32_t tileIndex, const float model[16],
                                     bool emissive, std::uint32_t tag) {
    if (!vertices || !indices || indexCount < 3) return;
    const uint8_t* v = static_cast<const uint8_t*>(vertices);
    auto vertPos = [&](std::size_t i, float out[3]) {
        const float* p = reinterpret_cast<const float*>(v + i * vertexStride);
        float x = p[0], y = p[1], z = p[2];
        if (model) {
            float w = model[3]*x + model[7]*y + model[11]*z + model[15];
            if (w == 0.0f) w = 1.0f;
            out[0] = (model[0]*x + model[4]*y + model[8]*z + model[12]) / w;
            out[1] = (model[1]*x + model[5]*y + model[9]*z + model[13]) / w;
            out[2] = (model[2]*x + model[6]*y + model[10]*z + model[14]) / w;
        } else {
            out[0] = x; out[1] = y; out[2] = z;
        }
    };
    auto vertUV = [&](std::size_t i, float out[2]) {
        const float* p = reinterpret_cast<const float*>(v + i * vertexStride + 12);
        out[0] = p[0]; out[1] = p[1];
    };
    for (std::size_t k = 0; k + 2 < indexCount; k += 3) {
        std::uint32_t i0 = indices[k], i1 = indices[k+1], i2 = indices[k+2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
        zq::render::RtTriangle t;
        vertPos(i0, t.p0); vertPos(i1, t.p1); vertPos(i2, t.p2);
        vertUV(i0, t.uv0); vertUV(i1, t.uv1); vertUV(i2, t.uv2);
        t.tex = tileIndex;
        t.light = emissive ? 1u : 0u;
        t.tag = tag;
        float e1x=t.p1[0]-t.p0[0],e1y=t.p1[1]-t.p0[1],e1z=t.p1[2]-t.p0[2];
        float e2x=t.p2[0]-t.p0[0],e2y=t.p2[1]-t.p0[1],e2z=t.p2[2]-t.p0[2];
        float nx=e1y*e2z-e1z*e2y,ny=e1z*e2x-e1x*e2z,nz=e1x*e2y-e1y*e2x;
        if (nx*nx+ny*ny+nz*nz < 1e-12f) continue;
        triangles_.push_back(t);
    }
}

} // namespace zq::app