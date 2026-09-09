#include "engine/mdl_model.hpp"
#include <cstring>

namespace zq::engine {

namespace {

constexpr uint32_t IDPOLYHEADER = 0x4F504449; // little-endian "IDPO"
constexpr int ALIAS_VERSION = 6;

// On-disk structs (all little-endian)
struct alignas(1) MdlHeader {
    int32_t ident;
    int32_t version;
    float scale[3];
    float scale_origin[3];
    float boundingradius;
    float eyeposition[3];
    int32_t numskins;
    int32_t skinwidth;
    int32_t skinheight;
    int32_t numverts;
    int32_t numtris;
    int32_t numframes;
    int32_t synctype;
    int32_t flags;
    float size;
};

struct alignas(1) StVert {
    int32_t onseam;
    int32_t s;
    int32_t t;
};

struct alignas(1) DTriangle {
    int32_t facesfront;
    int32_t vertindex[3];
};

struct alignas(1) Trivertex {
    uint8_t v[3];
    uint8_t lightnormalindex;
};

struct alignas(1) AliasFrameSingle {
    Trivertex bboxmin;
    Trivertex bboxmax;
    char name[16];
};

struct alignas(1) AliasFrameGroup {
    int32_t numframes;
    Trivertex bboxmin;
    Trivertex bboxmax;
};

struct alignas(1) AliasSkinType {
    int32_t type; // 0 single, 1 group
};

struct alignas(1) AliasFrameType {
    int32_t type; // 0 single, 1 group
};

} // namespace

bool MdlModel::Load(const uint8_t* data, size_t size) {
    if (size < sizeof(MdlHeader)) return false;
    const MdlHeader* h = reinterpret_cast<const MdlHeader*>(data);

    if (static_cast<uint32_t>(h->ident) != IDPOLYHEADER) return false;
    if (h->version != ALIAS_VERSION) return false;

    for (int i = 0; i < 3; i++) scale_[i] = h->scale[i];
    for (int i = 0; i < 3; i++) scale_origin_[i] = h->scale_origin[i];

    num_skins_ = h->numskins;
    skin_width_ = h->skinwidth;
    skin_height_ = h->skinheight;
    num_verts_ = h->numverts;
    num_tris_ = h->numtris;
    num_frames_ = h->numframes;

    if (num_skins_ < 0 || num_verts_ <= 0 || num_tris_ <= 0 || num_frames_ < 0) return false;
    if (skin_width_ <= 0 || skin_height_ <= 0 || skin_width_ * skin_height_ > (1 << 18)) return false;

    size_t pos = sizeof(MdlHeader);
    uint8_t* p = const_cast<uint8_t*>(data);
    size_t skin_pixels = (size_t)skin_width_ * skin_height_;

    // ---- Skins ----
    for (int i = 0; i < num_skins_; i++) {
        if (pos + sizeof(AliasSkinType) > size) return false;
        AliasSkinType st;
        memcpy(&st, p + pos, sizeof(st));
        pos += sizeof(AliasSkinType);
        if (st.type == 0) {
            if (pos + skin_pixels > size) return false;
            std::vector<uint8_t> skin(p + pos, p + pos + skin_pixels);
            skins_.push_back(std::move(skin));
            pos += skin_pixels;
        } else {
            // skin group: numskins, then intervals, then skins
            int32_t groupskins = 0;
            if (pos + 4 > size) return false;
            memcpy(&groupskins, p + pos, 4);
            pos += 4;
            // skip intervals
            if (pos + (size_t)groupskins * 4 > size) return false;
            pos += (size_t)groupskins * 4;
            // first skin of the group
            if (groupskins > 0) {
                if (pos + skin_pixels > size) return false;
                std::vector<uint8_t> skin(p + pos, p + pos + skin_pixels);
                skins_.push_back(std::move(skin));
                pos += skin_pixels;
            }
        }
    }

    // ---- Base s/t verts ----
    if (pos + (size_t)num_verts_ * sizeof(StVert) > size) return false;
    base_verts_.resize(num_verts_);
    const StVert* sv = reinterpret_cast<const StVert*>(p + pos);
    for (int i = 0; i < num_verts_; i++) {
        base_verts_[i].onseam = sv[i].onseam;
        base_verts_[i].s = (float)sv[i].s;
        base_verts_[i].t = (float)sv[i].t;
        base_verts_[i].pos[0] = 0;
        base_verts_[i].pos[1] = 0;
        base_verts_[i].pos[2] = 0;
    }
    pos += (size_t)num_verts_ * sizeof(StVert);

    // ---- Triangles ----
    if (pos + (size_t)num_tris_ * sizeof(DTriangle) > size) return false;
    triangles_.resize(num_tris_);
    const DTriangle* tr = reinterpret_cast<const DTriangle*>(p + pos);
    for (int i = 0; i < num_tris_; i++) {
        triangles_[i].facesfront = tr[i].facesfront;
        triangles_[i].vertindex[0] = tr[i].vertindex[0];
        triangles_[i].vertindex[1] = tr[i].vertindex[1];
        triangles_[i].vertindex[2] = tr[i].vertindex[2];
    }
    pos += (size_t)num_tris_ * sizeof(DTriangle);

    // ---- Frames ----
    for (int f = 0; f < num_frames_; f++) {
        if (pos + sizeof(AliasFrameType) > size) return false;
        AliasFrameType ft;
        memcpy(&ft, p + pos, sizeof(ft));
        pos += sizeof(AliasFrameType);

        if (ft.type == 0) {
            if (pos + sizeof(AliasFrameSingle) > size) return false;
            pos += sizeof(AliasFrameSingle);
            if (pos + (size_t)num_verts_ * sizeof(Trivertex) > size) return false;
            std::vector<uint8_t[3]> frame(num_verts_);
            const Trivertex* tv = reinterpret_cast<const Trivertex*>(p + pos);
            for (int i = 0; i < num_verts_; i++) {
                frame[i][0] = tv[i].v[0];
                frame[i][1] = tv[i].v[1];
                frame[i][2] = tv[i].v[2];
            }
            frames_.push_back(std::move(frame));
            pos += (size_t)num_verts_ * sizeof(Trivertex);
        } else {
            // group frame: numframes, bbox, intervals, then frames
            if (pos + sizeof(AliasFrameGroup) > size) return false;
            AliasFrameGroup fg;
            memcpy(&fg, p + pos, sizeof(fg));
            int32_t numframes2 = fg.numframes;
            if (numframes2 < 0) return false;
            pos += sizeof(AliasFrameGroup);
            // intervals
            if (pos + (size_t)numframes2 * 4 > size) return false;
            pos += (size_t)numframes2 * 4;
            // each subframe: AliasFrameSingle + verts
            if (numframes2 > 0) {
                if (pos + sizeof(AliasFrameSingle) > size) return false;
                pos += sizeof(AliasFrameSingle);
                if (pos + (size_t)num_verts_ * sizeof(Trivertex) > size) return false;
                std::vector<uint8_t[3]> frame(num_verts_);
                const Trivertex* tv = reinterpret_cast<const Trivertex*>(p + pos);
                for (int i = 0; i < num_verts_; i++) {
                    frame[i][0] = tv[i].v[0];
                    frame[i][1] = tv[i].v[1];
                    frame[i][2] = tv[i].v[2];
                }
                frames_.push_back(std::move(frame));
                pos += (size_t)num_verts_ * sizeof(Trivertex);
                // skip remaining subframes
                if (pos + (size_t)(numframes2 - 1) * (sizeof(AliasFrameSingle) + (size_t)num_verts_ * sizeof(Trivertex)) > size) return false;
                pos += (size_t)(numframes2 - 1) * (sizeof(AliasFrameSingle) + (size_t)num_verts_ * sizeof(Trivertex));
            }
        }
    }

    return true;
}

void MdlModel::BuildMesh(int frame, std::vector<float>& out_verts,
                         std::vector<float>& out_uv,
                         std::vector<uint32_t>& out_tris) const {
    out_verts.clear();
    out_uv.clear();
    out_tris.clear();

    if (frame < 0 || frame >= (int)frames_.size()) return;
    if (base_verts_.size() != frames_[frame].size()) return;

    // Two-sided skins are twice as wide as one side: the front half is on
    // the left, the back (seam-reversed) half on the right. Back-facing
    // triangles sample the right half: their seam verts add skinwidth/2 to
    // s. Vertices are emitted per-triangle because a seam vert is shared by
    // a front and a back triangle with different effective s.
    const float seam = (float)skin_width_ * 0.5f;
    for (const auto& t : triangles_) {
        for (int k = 0; k < 3; k++) {
            int vi = t.vertindex[k];
            const auto& bv = base_verts_[vi];
            const auto& fv = frames_[frame][vi];
            out_verts.push_back(scale_[0] * (float)fv[0] + scale_origin_[0]);
            out_verts.push_back(scale_[1] * (float)fv[1] + scale_origin_[1]);
            out_verts.push_back(scale_[2] * (float)fv[2] + scale_origin_[2]);
            float s = bv.s;
            if (!t.facesfront && bv.onseam) s += seam;
            out_uv.push_back(s / (float)skin_width_);
            out_uv.push_back(bv.t / (float)skin_height_);
        }
        uint32_t base = (uint32_t)out_tris.size();
        out_tris.push_back(base);
        out_tris.push_back(base + 1);
        out_tris.push_back(base + 2);
    }
}

} // namespace zq::engine