#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace zq::engine {

// Quake alias model (.mdl) loader, ported from the GPL reference
// (modelgen.h / gl_model.c). Parses header, skins, vertices, triangles and
// frames so the model can be rendered with the Vulkan pipeline.
class MdlModel {
public:
    struct Vertex {
        float pos[3];
        float s, t;          // texture coords (in texels)
        int onseam = 0;      // stvert onseam: back-face triangles offset s
    };
    struct Triangle {
        int facesfront = 0;  // dtriangle_t.facesfront (DT_FACES_FRONT)
        int vertindex[3];
    };

    bool Load(const uint8_t* data, size_t size);

    // Header info
    int NumFrames() const { return num_frames_; }
    int NumVerts() const { return num_verts_; }
    int NumTris() const { return num_tris_; }
    int SkinWidth() const { return skin_width_; }
    int SkinHeight() const { return skin_height_; }
    int NumSkins() const { return num_skins_; }
    const float* Scale() const { return scale_; }
    const float* ScaleOrigin() const { return scale_origin_; }
    const std::string& Name() const { return name_; }

    // First skin's 8-bit texels (indexed by palette). Empty if none.
    const std::vector<uint8_t>& Skin(int index = 0) const {
        static const std::vector<uint8_t> empty;
        return (index < (int)skins_.size()) ? skins_[index] : empty;
    }

    // Build a static mesh for frame `frame` (0..numframes-1).
    // out_verts: per-vertex positions (world units, scale applied)
    // out_uv: per-vertex uv (normalized by skin size)
    // out_tris: triangle vertex indices
    void BuildMesh(int frame, std::vector<float>& out_verts,
                    std::vector<float>& out_uv,
                    std::vector<uint32_t>& out_tris) const;

private:
    std::string name_;
    float scale_[3] = {1, 1, 1};
    float scale_origin_[3] = {0, 0, 0};
    int num_skins_ = 0;
    int skin_width_ = 0, skin_height_ = 0;
    int num_verts_ = 0, num_tris_ = 0, num_frames_ = 0;

    std::vector<Vertex> base_verts_;      // s/t texture coords
    std::vector<Triangle> triangles_;
    std::vector<std::vector<uint8_t>> skins_;
    // frames_[f] = per-vertex byte position (v[3]) for frame f
    std::vector<std::vector<uint8_t[3]>> frames_;
};

} // namespace zq::engine