#pragma once
#include "render/ray_scene.hpp"
#include <cstdint>
#include <vector>

namespace zq::app {

// Builds an in-game ray-traced scene: a triangle list + a packed RGBA texture
// atlas, ready to be uploaded to RayTracer::BuildScene. Each unique texture is
// placed into the atlas at its NATIVE resolution (no downsampling), so a large
// Quake wall texture keeps its detail. Tiles are shelf-packed; the shader uses
// the per-tile info (origin + size) to sample correctly.
class RtSceneBuilder {
public:
    RtSceneBuilder() = default;

    // Appends a triangle mesh, transforming every vertex by `model` (a 16-float
    // column-major 4x4; pass an identity for world-space geometry). The mesh's
    // texture is registered into the atlas as an RGBA8 tile. Returns the tile
    // index the texture was assigned.
    // verts: interleaved pos(3) uv(2) ... (a zq::app::WorldVertex array).
    std::uint32_t AddMesh(const void* vertices, std::size_t vertexCount,
                 std::size_t vertexStride,
                 const std::uint32_t* indices, std::size_t indexCount,
                 const std::uint8_t* texRgba, std::uint32_t texW, std::uint32_t texH,
                 const float model[16]);

    // Adds geometry using an existing atlas tile index (no texture registration).
    // Used to cheaply re-add dynamic entities each rebuild with a stable tile.
    void AddMeshWithTile(const void* vertices, std::size_t vertexCount,
                 std::size_t vertexStride,
                 const std::uint32_t* indices, std::size_t indexCount,
                 std::uint32_t tileIndex, const float model[16]);

    // Registers an RGBA texture into the atlas and returns its tile index,
    // WITHOUT adding geometry. Used to include entity skins in the atlas so
    // dynamic entity triangles (added via AddMeshWithTile) can reference them.
    std::uint32_t RegisterTexture(const std::uint8_t* rgba, std::uint32_t w, std::uint32_t h);

    // Returns the collected triangles and finalizes the atlas.
    std::vector<zq::render::RtTriangle> Triangles();
    std::vector<uint8_t> AtlasRgba() const { return atlas_.rgba; }
    std::uint32_t AtlasWidth() const { return atlas_.width; }
    std::uint32_t AtlasHeight() const { return atlas_.height; }
    const std::vector<zq::render::RtTileInfo>& TileInfos() const { return tile_infos_; }
    void Clear();

private:

    std::vector<zq::render::RtTriangle> triangles_;
    std::vector<zq::render::RtTileInfo> tile_infos_;

    struct Atlas {
        std::vector<uint8_t> rgba;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };
    Atlas atlas_;
    std::uint32_t cursor_x_ = 0, cursor_y_ = 0, row_h_ = 0;
};

} // namespace zq::app