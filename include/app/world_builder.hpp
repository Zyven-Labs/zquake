#pragma once
#include <vector>
#include <cstdint>
#include "engine/bsp.hpp"

namespace zq::app {

// One vertex = 44 bytes: pos(3) uv(2) normal(3) lightmapuv(3)
struct WorldVertex {
    float pos[3];
    float uv[2];
    float normal[3];
    float lmuv[3];
};

struct TextureGroup {
    int texture_index;
    std::vector<WorldVertex> vertices;
    std::vector<uint32_t> indices;
};

// Convert an 8-bit indexed texture to RGBA8 using the palette.
std::vector<uint8_t> IndexedToRGBA(const zq::engine::BSPTexture& tex,
                                   const uint8_t palette[768]);

// Convert raw 8-bit indexed texels (e.g. an MDL skin) to RGBA8.
std::vector<uint8_t> IndexedToRGBA(const uint8_t* texels, size_t count,
                                   const uint8_t palette[768]);

// Gather world faces into per-texture groups (world model faces only;
// brush pickup/trigger submodels are built separately via BuildSubmodelGroups).
std::vector<TextureGroup> BuildGroups(const zq::engine::BSPMap& map);

// Gather the faces of a single BSP submodel into per-texture groups.
std::vector<TextureGroup> BuildSubmodelGroups(const zq::engine::BSPMap& map,
                                              int model_index);

} // namespace zq::app