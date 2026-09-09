#include "app/world_builder.hpp"

namespace zq::app {

std::vector<uint8_t> IndexedToRGBA(const zq::engine::BSPTexture& tex,
                                   const uint8_t palette[768]) {
    return IndexedToRGBA(tex.pixels.data(), tex.pixels.size(), palette);
}

std::vector<uint8_t> IndexedToRGBA(const uint8_t* texels, size_t count,
                                   const uint8_t palette[768]) {
    std::vector<uint8_t> rgba(count * 4);
    for (size_t i = 0; i < count; i++) {
        uint8_t idx = texels[i];
        rgba[i * 4 + 0] = palette[idx * 3 + 0];
        rgba[i * 4 + 1] = palette[idx * 3 + 1];
        rgba[i * 4 + 2] = palette[idx * 3 + 2];
        rgba[i * 4 + 3] = 255;
    }
    return rgba;
}

namespace {
// Shared face->TextureGroup builder over a face range.
std::vector<TextureGroup> BuildGroupsRange(const zq::engine::BSPMap& map,
                                           int face_begin, int face_end) {
    std::vector<TextureGroup> groups;
    const auto& faces = map.Faces();
    const auto& texinfos = map.TexInfos();
    if (face_begin < 0) face_begin = 0;
    if (face_end > (int)faces.size()) face_end = (int)faces.size();

    for (int f = face_begin; f < face_end; f++) {
        const auto& face = faces[f];
        int ti = face.texinfo;
        if (ti < 0 || ti >= (int)texinfos.size()) continue;
        int miptex = texinfos[ti].miptex;
        if (miptex < 0) continue;

        int group_idx = -1;
        for (size_t g = 0; g < groups.size(); g++) {
            if (groups[g].texture_index == miptex) { group_idx = (int)g; break; }
        }
        if (group_idx < 0) {
            groups.push_back(zq::app::TextureGroup{});
            groups.back().texture_index = miptex;
            group_idx = (int)groups.size() - 1;
        }

        const auto& plane = map.FacePlane(face);
        float nx = plane.normal[0], ny = plane.normal[1], nz = plane.normal[2];
        if (face.side) { nx = -nx; ny = -ny; nz = -nz; }

        auto poly = map.FacePolygon(face);
        if (poly.size() < 3) continue;

        float tex_w = 1.0f, tex_h = 1.0f;
        if (miptex < (int)map.Textures().size()) {
            tex_w = (float)map.Textures()[miptex].width;
            tex_h = (float)map.Textures()[miptex].height;
        }

        float texs[2] = { texinfos[ti].vecs[0][0], texinfos[ti].vecs[0][1] };
        float text[2] = { texinfos[ti].vecs[1][0], texinfos[ti].vecs[1][1] };

        uint32_t base = (uint32_t)groups[group_idx].vertices.size();
        for (size_t i = 0; i < poly.size(); i++) {
            zq::app::WorldVertex v;
            v.pos[0] = poly[i].point[0];
            v.pos[1] = poly[i].point[1];
            v.pos[2] = poly[i].point[2];

            float s = v.pos[0] * texs[0] + v.pos[1] * texs[1] +
                      v.pos[2] * texinfos[ti].vecs[0][2] + texinfos[ti].vecs[0][3];
            float t = v.pos[0] * text[0] + v.pos[1] * text[1] +
                      v.pos[2] * texinfos[ti].vecs[1][2] + texinfos[ti].vecs[1][3];
            v.uv[0] = s / tex_w;
            v.uv[1] = t / tex_h;

            v.normal[0] = nx; v.normal[1] = ny; v.normal[2] = nz;
            v.lmuv[0] = 0.5f; v.lmuv[1] = 0.5f; v.lmuv[2] = 0.0f;

            groups[group_idx].vertices.push_back(v);
        }
        for (size_t i = 1; i + 1 < poly.size(); i++) {
            groups[group_idx].indices.push_back(base);
            groups[group_idx].indices.push_back(base + (uint32_t)i);
            groups[group_idx].indices.push_back(base + (uint32_t)(i + 1));
        }
    }
    return groups;
}
} // namespace

std::vector<TextureGroup> BuildGroups(const zq::engine::BSPMap& map) {
    const zq::engine::BSPModel* world = map.WorldModel();
    if (!world) return {};
    // Only the world model's faces (brush pickup/trigger submodels are drawn
    // separately so consumed items can disappear).
    return BuildGroupsRange(map, world->firstface, world->firstface + world->numfaces);
}

std::vector<TextureGroup> BuildSubmodelGroups(const zq::engine::BSPMap& map,
                                              int model_index) {
    if (model_index < 0 || model_index >= (int)map.Models().size()) return {};
    const auto& m = map.Models()[model_index];
    return BuildGroupsRange(map, m.firstface, m.firstface + m.numfaces);
}

} // namespace zq::app