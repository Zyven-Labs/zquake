#include "engine/bsp.hpp"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace zq::engine {

static float qAbs(float v) { return v < 0 ? -v : v; }

bool BSPMap::LoadLump(const uint8_t* data, size_t size, int lump, const void** out, size_t* out_len) const {
    if (lump < 0 || lump >= HEADER_LUMPS) {
        *out = nullptr;
        *out_len = 0;
        return false;
    }
    const BSPHeader* header = reinterpret_cast<const BSPHeader*>(data);
    if (size < sizeof(BSPHeader)) {
        *out = nullptr;
        *out_len = 0;
        return false;
    }
    int32_t ofs = header->lumps[lump].fileofs;
    int32_t len = header->lumps[lump].filelen;
    if (ofs < 0 || len < 0 || (size_t)(ofs + len) > size) {
        *out = nullptr;
        *out_len = 0;
        return false;
    }
    *out = data + ofs;
    *out_len = static_cast<size_t>(len);
    return true;
}

bool BSPMap::Load(const uint8_t* data, size_t size) {
    if (size < sizeof(BSPHeader)) return false;
    const BSPHeader* header = reinterpret_cast<const BSPHeader*>(data);
    if (header->version != 29) return false;

    const void* lump_data = nullptr;
    size_t lump_len = 0;

    // Entities (text)
    if (LoadLump(data, size, LUMP_ENTITIES, &lump_data, &lump_len)) {
        entity_string_.assign(static_cast<const char*>(lump_data), lump_len);
    }

    // Planes
    if (LoadLump(data, size, LUMP_PLANES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPPlane);
        planes_.assign(reinterpret_cast<const BSPPlane*>(lump_data),
                       reinterpret_cast<const BSPPlane*>(lump_data) + count);
    }

    // Vertexes
    if (LoadLump(data, size, LUMP_VERTEXES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPVertex);
        vertexes_.assign(reinterpret_cast<const BSPVertex*>(lump_data),
                         reinterpret_cast<const BSPVertex*>(lump_data) + count);
    }

    // Nodes
    if (LoadLump(data, size, LUMP_NODES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPNode);
        nodes_.assign(reinterpret_cast<const BSPNode*>(lump_data),
                      reinterpret_cast<const BSPNode*>(lump_data) + count);
    }

    // Clipnodes
    if (LoadLump(data, size, LUMP_CLIPNODES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPClipNode);
        clipnodes_.assign(reinterpret_cast<const BSPClipNode*>(lump_data),
                          reinterpret_cast<const BSPClipNode*>(lump_data) + count);
    }

    // Leafs
    if (LoadLump(data, size, LUMP_LEAFS, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPLeaf);
        leafs_.assign(reinterpret_cast<const BSPLeaf*>(lump_data),
                      reinterpret_cast<const BSPLeaf*>(lump_data) + count);
    }

    // Marksurfaces
    if (LoadLump(data, size, LUMP_MARKSURFACES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(uint16_t);
        marksurfaces_.assign(reinterpret_cast<const uint16_t*>(lump_data),
                             reinterpret_cast<const uint16_t*>(lump_data) + count);
    }

    // Texinfo
    if (LoadLump(data, size, LUMP_TEXINFO, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPTexInfo);
        texinfos_.assign(reinterpret_cast<const BSPTexInfo*>(lump_data),
                         reinterpret_cast<const BSPTexInfo*>(lump_data) + count);
    }

    // Faces
    if (LoadLump(data, size, LUMP_FACES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPFace);
        faces_.assign(reinterpret_cast<const BSPFace*>(lump_data),
                      reinterpret_cast<const BSPFace*>(lump_data) + count);
    }

    // Edges
    if (LoadLump(data, size, LUMP_EDGES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPEdge);
        edges_.assign(reinterpret_cast<const BSPEdge*>(lump_data),
                      reinterpret_cast<const BSPEdge*>(lump_data) + count);
    }

    // Surfedges
    if (LoadLump(data, size, LUMP_SURFEDGES, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(int32_t);
        surfedges_.assign(reinterpret_cast<const int32_t*>(lump_data),
                          reinterpret_cast<const int32_t*>(lump_data) + count);
    }

    // Lighting
    if (LoadLump(data, size, LUMP_LIGHTING, &lump_data, &lump_len)) {
        lighting_.assign(static_cast<const uint8_t*>(lump_data),
                         static_cast<const uint8_t*>(lump_data) + lump_len);
    }

    // Models
    if (LoadLump(data, size, LUMP_MODELS, &lump_data, &lump_len)) {
        size_t count = lump_len / sizeof(BSPModel);
        models_.assign(reinterpret_cast<const BSPModel*>(lump_data),
                       reinterpret_cast<const BSPModel*>(lump_data) + count);
    }

    // Textures (miptex lump)
    if (LoadLump(data, size, LUMP_TEXTURES, &lump_data, &lump_len)) {
        const uint8_t* texdata = static_cast<const uint8_t*>(lump_data);
        if (lump_len >= 4) {
            int32_t nummiptex = 0;
            std::memcpy(&nummiptex, texdata, 4);
            if (nummiptex > 0 && nummiptex < 4096) {
                const uint8_t* offsets = texdata + 4;
                for (int32_t i = 0; i < nummiptex; i++) {
                    int32_t dataofs = 0;
                    std::memcpy(&dataofs, offsets + i * 4, 4);
                    if (dataofs <= 0 || (size_t)dataofs + 40 > lump_len) continue;
                    const uint8_t* miptex = texdata + dataofs;
                    BSPTexture tex;
                    std::memcpy(tex.name, miptex, 16);
                    tex.name[15] = '\0';
                    std::memcpy(&tex.width, miptex + 16, 4);
                    std::memcpy(&tex.height, miptex + 20, 4);
                    uint32_t off0 = 0;
                    std::memcpy(&off0, miptex + 24, 4);
                    if (tex.width == 0 || tex.height == 0 ||
                        tex.width > 2048 || tex.height > 2048) {
                        continue;
                    }
                    size_t pixels_size = (size_t)tex.width * tex.height;
                    if ((size_t)dataofs + off0 + pixels_size > lump_len) continue;
                    tex.pixels.assign(miptex + off0, miptex + off0 + pixels_size);
                    textures_.push_back(std::move(tex));
                }
            }
        }
    }

    // After clipnodes/models are in, neutralise broken submodel clip trees so
    // they cannot act as phantom walls (see SanitizeSubmodelHulls()).
    SanitizeSubmodelHulls();

    return !planes_.empty() || !vertexes_.empty();
}

void BSPMap::SanitizeSubmodelHulls() {
    if (clipnodes_.empty() || models_.size() < 2) return;
    const BSPModel& w = models_[0];
    float gmin[3], gmax[3];
    for (int k = 0; k < 3; k++) {
        gmin[k] = w.mins[k] < w.maxs[k] ? w.mins[k] : w.maxs[k];
        gmax[k] = w.mins[k] < w.maxs[k] ? w.maxs[k] : w.mins[k];
    }

    // Sparse vertical bands sampled across the whole world. A healthy submodel
    // clip tree is solid only inside (roughly) its own model bounds, so its
    // solid sample count scales with its bounds. A broken tree is solid over
    // large unrelated spans (phantom walls at the player spawn), so it vastly
    // exceeds its own expected count.
    const float step = 64.0f;
    const float zl[3] = {0.0f, 40.0f, 120.0f};

    for (size_t i = 1; i < models_.size(); i++) {
        BSPModel& m = models_[i];
        for (int h = 0; h < 4; h++) {
            int hn = m.headnode[h];
            if (hn < 0) continue; // already empty / nothing to test
            float mn[3], mx[3];
            for (int k = 0; k < 3; k++) {
                mn[k] = m.mins[k] < m.maxs[k] ? m.mins[k] : m.maxs[k];
                mx[k] = m.mins[k] < m.maxs[k] ? m.maxs[k] : m.mins[k];
            }
            // expected solid samples if the tree really only covers its bounds
            int expected = 1;
            for (int k = 0; k < 2; k++) {
                float span = mx[k] - mn[k];
                expected *= 1 + (int)(span / step + 0.5f);
            }
            expected *= 3; // up to three z bands

            int solid = 0;
            for (float z : zl) {
                if (z < gmin[2] - 8.0f || z > gmax[2] + 8.0f) continue;
                for (float x = gmin[0] + step / 2; x <= gmax[0] - step / 2; x += step)
                    for (float y = gmin[1] + step / 2; y <= gmax[1] - step / 2; y += step) {
                        float p[3] = {x, y, z};
                        if (PointContentsHull(p, hn) == CONTENTS_SOLID) solid++;
                    }
            }
            if ((float)solid > 4.0f * expected + 16.0f) m.headnode[h] = CONTENTS_EMPTY;
        }
    }
}

const BSPPlane& BSPMap::FacePlane(const BSPFace& face) const {
    if (face.planenum < 0 || face.planenum >= (int)planes_.size()) {
        static BSPPlane fallback = {{0, 0, 1}, 0, PLANE_Z};
        return fallback;
    }
    return planes_[face.planenum];
}

std::vector<BSPVertex> BSPMap::FacePolygon(const BSPFace& face) const {
    std::vector<BSPVertex> result;
    int first = face.firstedge;
    int num = face.numedges;
    for (int i = 0; i < num && (first + i) < (int)surfedges_.size(); i++) {
        int surfedge = surfedges_[first + i];
        // Quake convention: surfedge value directly indexes the edges array
        // (edge 0 in the file is a dummy that is never referenced), so no -1.
        int vindex = 0;
        if (surfedge < 0) {
            int edge_index = -surfedge;
            if (edge_index <= 0 || edge_index >= (int)edges_.size()) continue;
            vindex = edges_[edge_index].v[1];
        } else {
            int edge_index = surfedge;
            if (edge_index <= 0 || edge_index >= (int)edges_.size()) continue;
            vindex = edges_[edge_index].v[0];
        }
        if (vindex >= (int)vertexes_.size()) continue;
        result.push_back(vertexes_[vindex]);
    }
    return result;
}

namespace {
constexpr float DIST_EPSILON = 0.03125f;

struct HullTrace {
    float fraction = 1.0f;
    float endpos[3] = {0, 0, 0};
    float plane_normal[3] = {0, 0, 0};
    float plane_dist = 0.0f;
    bool allsolid = true;
    bool startsolid = false;
};
}

int BSPMap::PointContents(const float p[3]) const {
    return PointContentsHull(p, 0);
}

int BSPMap::PointContentsHull(const float p[3], int headnode) const {
    if (clipnodes_.empty()) return CONTENTS_EMPTY;
    int num = headnode;
    int guard = 0;
    while (num >= 0) {
        if (++guard > (int)clipnodes_.size() + 4) return CONTENTS_EMPTY;
        if (num < 0 || num >= (int)clipnodes_.size()) return CONTENTS_EMPTY;
        const BSPClipNode& node = clipnodes_[num];
        if (node.planenum < 0 || node.planenum >= (int)planes_.size()) return CONTENTS_EMPTY;
        const BSPPlane& plane = planes_[node.planenum];
        float d;
        if (plane.type < 3) {
            d = p[plane.type] - plane.dist;
        } else {
            d = plane.normal[0] * p[0] + plane.normal[1] * p[1] + plane.normal[2] * p[2] - plane.dist;
        }
        if (d < 0) num = node.children[1];
        else num = node.children[0];
    }
    return num;
}

int BSPMap::PointContentsFromNode(const float p[3], int nodenum) const {
    if (clipnodes_.empty()) return CONTENTS_EMPTY;
    int num = nodenum;
    int guard = 0;
    while (num >= 0) {
        if (++guard > (int)clipnodes_.size() + 4) return CONTENTS_EMPTY;
        if (num < 0 || num >= (int)clipnodes_.size()) return CONTENTS_EMPTY;
        const BSPClipNode& node = clipnodes_[num];
        if (node.planenum < 0 || node.planenum >= (int)planes_.size()) return CONTENTS_EMPTY;
        const BSPPlane& plane = planes_[node.planenum];
        float d;
        if (plane.type < 3) {
            d = p[plane.type] - plane.dist;
        } else {
            d = plane.normal[0] * p[0] + plane.normal[1] * p[1] + plane.normal[2] * p[2] - plane.dist;
        }
        if (d < 0) num = node.children[1];
        else num = node.children[0];
    }
    return num;
}

bool TraceRecursiveHull(const BSPMap& map, int nodenum, float p1f, float p2f,
                        const float* p1, const float* p2, HullTrace& trace,
                        int& guard, int* hit_plane) {
    const auto& clipnodes = map.ClipNodes();
    const auto& planes = map.Planes();

    if (nodenum < 0) {
        if (nodenum != CONTENTS_SOLID) {
            trace.allsolid = false;
        } else {
            trace.startsolid = true;
        }
        return true; // empty (from the solid's perspective, keep going)
    }

    if (nodenum >= (int)clipnodes.size()) return true;
    if (++guard > (int)clipnodes.size() + 8) return false;

    const BSPClipNode& node = clipnodes[nodenum];
    if (node.planenum < 0 || node.planenum >= (int)planes.size()) return true;
    const BSPPlane& plane = planes[node.planenum];

    float t1, t2;
    if (plane.type < 3) {
        t1 = p1[plane.type] - plane.dist;
        t2 = p2[plane.type] - plane.dist;
    } else {
        t1 = plane.normal[0]*p1[0] + plane.normal[1]*p1[1] + plane.normal[2]*p1[2] - plane.dist;
        t2 = plane.normal[0]*p2[0] + plane.normal[1]*p2[1] + plane.normal[2]*p2[2] - plane.dist;
    }

    if (t1 >= 0 && t2 >= 0) {
        return TraceRecursiveHull(map, node.children[0], p1f, p2f, p1, p2, trace, guard, hit_plane);
    }
    if (t1 < 0 && t2 < 0) {
        return TraceRecursiveHull(map, node.children[1], p1f, p2f, p1, p2, trace, guard, hit_plane);
    }

    // Crosses the plane: place crosspoint DIST_EPSILON on near side
    float frac;
    if (t1 < 0) frac = (t1 + DIST_EPSILON) / (t1 - t2);
    else frac = (t1 - DIST_EPSILON) / (t1 - t2);
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;

    float midf = p1f + (p2f - p1f) * frac;
    float mid[3];
    mid[0] = p1[0] + frac * (p2[0] - p1[0]);
    mid[1] = p1[1] + frac * (p2[1] - p1[1]);
    mid[2] = p1[2] + frac * (p2[2] - p1[2]);

    int side = (t1 < 0) ? 1 : 0;

    // Move up to the node through the near side
    if (!TraceRecursiveHull(map, node.children[side], p1f, midf, p1, mid, trace, guard, hit_plane)) {
        return false;
    }

    // Check the far side of the node (from the far child, like Quake's
    // SV_HullPointContents(hull, node->children[side^1], mid))
    if (map.PointContentsFromNode(mid, node.children[side ^ 1]) != CONTENTS_SOLID) {
        // Go past the node
        return TraceRecursiveHull(map, node.children[side ^ 1], midf, p2f, mid, p2, trace, guard, hit_plane);
    }

    if (trace.allsolid) {
        return false; // never got out of the solid area
    }

    // The other side of the node is solid; this is the impact point
    if (!side) {
        trace.plane_normal[0] = plane.normal[0];
        trace.plane_normal[1] = plane.normal[1];
        trace.plane_normal[2] = plane.normal[2];
        trace.plane_dist = plane.dist;
    } else {
        trace.plane_normal[0] = -plane.normal[0];
        trace.plane_normal[1] = -plane.normal[1];
        trace.plane_normal[2] = -plane.normal[2];
        trace.plane_dist = -plane.dist;
    }
    if (hit_plane) *hit_plane = node.planenum;

    // Backup if the impact point ends up inside solid (happens occasionally)
    while (map.PointContents(mid) == CONTENTS_SOLID) {
        frac -= 0.1f;
        if (frac < 0) {
            trace.fraction = midf;
            trace.endpos[0] = mid[0];
            trace.endpos[1] = mid[1];
            trace.endpos[2] = mid[2];
            return false;
        }
        midf = p1f + (p2f - p1f) * frac;
        mid[0] = p1[0] + frac * (p2[0] - p1[0]);
        mid[1] = p1[1] + frac * (p2[1] - p1[1]);
        mid[2] = p1[2] + frac * (p2[2] - p1[2]);
    }

    trace.fraction = midf;
    trace.endpos[0] = mid[0];
    trace.endpos[1] = mid[1];
    trace.endpos[2] = mid[2];
    return false;
}

void BSPMap::TraceMove(const float start[3], const float end[3], float result[3],
                       int* hit_plane, float* hit_dist) const {
    if (clipnodes_.empty()) {
        result[0] = end[0]; result[1] = end[1]; result[2] = end[2];
        return;
    }

    HullTrace trace;
    trace.fraction = 1.0f;
    trace.endpos[0] = end[0]; trace.endpos[1] = end[1]; trace.endpos[2] = end[2];
    trace.allsolid = true;

    int guard = 0;
    int plane_idx = -1;
    TraceRecursiveHull(*this, 0, 0.0f, 1.0f, start, end, trace, guard, &plane_idx);

    // If a solid was hit the recursion set endpos; otherwise endpos == end.
    if (trace.fraction != 1.0f || trace.startsolid) {
        if (trace.startsolid && trace.fraction == 1.0f &&
            trace.endpos[0] == end[0] && trace.endpos[1] == end[1] && trace.endpos[2] == end[2]) {
            // Started inside solid with no clear impact; hold position.
            result[0] = start[0]; result[1] = start[1]; result[2] = start[2];
        } else {
            result[0] = trace.endpos[0];
            result[1] = trace.endpos[1];
            result[2] = trace.endpos[2];
        }
    } else {
        result[0] = trace.endpos[0];
        result[1] = trace.endpos[1];
        result[2] = trace.endpos[2];
    }

    if (hit_plane) *hit_plane = plane_idx;
    if (hit_dist && plane_idx >= 0 && plane_idx < (int)planes_.size()) {
        *hit_dist = planes_[plane_idx].dist;
    }
}

void BSPMap::BoxTrace(const float start[3], const float end[3], float result[3],
                      int headnode) const {
    if (clipnodes_.empty()) {
        result[0] = end[0]; result[1] = end[1]; result[2] = end[2];
        return;
    }
    HullTrace trace;
    trace.fraction = 1.0f;
    trace.endpos[0] = end[0]; trace.endpos[1] = end[1]; trace.endpos[2] = end[2];
    trace.allsolid = true;
    int guard = 0;
    int plane_idx = -1;
    TraceRecursiveHull(*this, headnode, 0.0f, 1.0f, start, end, trace, guard, &plane_idx);
    if (trace.fraction != 1.0f || trace.startsolid) {
        if (trace.startsolid && trace.fraction == 1.0f &&
            trace.endpos[0] == end[0] && trace.endpos[1] == end[1] && trace.endpos[2] == end[2]) {
            result[0] = start[0]; result[1] = start[1]; result[2] = start[2];
        } else {
            result[0] = trace.endpos[0]; result[1] = trace.endpos[1]; result[2] = trace.endpos[2];
        }
    } else {
        result[0] = trace.endpos[0]; result[1] = trace.endpos[1]; result[2] = trace.endpos[2];
    }
}

// Core point trace through a clip hull tree rooted at `root`, with the
// SV_HullForEntity offset: the trace points are shifted by -offset, and the
// impact point is shifted back by +offset. This is SV_ClipMoveToEntity.
static BSPMap::TraceResult TraceThroughHull(const BSPMap& map, int root,
                                            const float clipmin[3],
                                            const float origin[3],
                                            const float start[3], const float end[3],
                                            const float mins[3], const float maxs[3]) {
    BSPMap::TraceResult out;
    out.fraction = 1.0f;
    out.endpos[0] = end[0]; out.endpos[1] = end[1]; out.endpos[2] = end[2];
    out.allsolid = true;
    if (map.ClipNodes().empty()) return out;

    float offset[3];
    float p1[3], p2[3];
    for (int i = 0; i < 3; i++) {
        offset[i] = clipmin[i] - mins[i] + origin[i];
        p1[i] = start[i] - offset[i];
        p2[i] = end[i] - offset[i];
    }

    // pick the hull by size (SV_HullForEntity) if root not already chosen
    float size0 = maxs[0] - mins[0];
    if (size0 < 0) size0 = -size0;

    HullTrace trace;
    trace.fraction = 1.0f;
    trace.endpos[0] = p2[0]; trace.endpos[1] = p2[1]; trace.endpos[2] = p2[2];
    trace.allsolid = true;
    int guard = 0;
    int plane_idx = -1;
    TraceRecursiveHull(map, root, 0.0f, 1.0f, p1, p2, trace, guard, &plane_idx);

    out.fraction = trace.fraction;
    out.startsolid = trace.startsolid;
    out.allsolid = trace.allsolid;
    out.plane_normal[0] = trace.plane_normal[0];
    out.plane_normal[1] = trace.plane_normal[1];
    out.plane_normal[2] = trace.plane_normal[2];
    out.plane_dist = trace.plane_dist;

    if (out.fraction < 0.0f) out.fraction = 0.0f;
    if (out.fraction > 1.0f) out.fraction = 1.0f;
    if (out.fraction < 1.0f) {
        out.endpos[0] = trace.endpos[0] + offset[0];
        out.endpos[1] = trace.endpos[1] + offset[1];
        out.endpos[2] = trace.endpos[2] + offset[2];
    } else {
        out.endpos[0] = end[0]; out.endpos[1] = end[1]; out.endpos[2] = end[2];
    }

    int cont = map.PointContents(out.endpos);
    if (cont == CONTENTS_EMPTY) out.inopen = true;
    else if (cont != CONTENTS_SOLID) out.inwater = true;

    return out;
}

BSPMap::TraceResult BSPMap::WorldTrace(const float start[3], const float end[3],
                                       const float mins[3], const float maxs[3]) const {
    TraceResult out;
    if (clipnodes_.empty()) {
        out.fraction = 1.0f;
        out.endpos[0] = end[0]; out.endpos[1] = end[1]; out.endpos[2] = end[2];
        out.allsolid = true;
        return out;
    }

    const BSPModel* world = WorldModel();
    int root = 0;
    float clipmin[3] = {0, 0, 0};
    float zorigin[3] = {0, 0, 0};
    float size0 = maxs[0] - mins[0];
    if (size0 < 0) size0 = -size0;
    if (world) {
        if (size0 >= 3) {
            if (size0 <= 32) { root = world->headnode[1]; clipmin[0]=-16; clipmin[1]=-16; clipmin[2]=-24; }
            else { root = world->headnode[2]; clipmin[0]=-32; clipmin[1]=-32; clipmin[2]=-24; }
        } else {
            root = world->headnode[0];
        }
    }
    return TraceThroughHull(*this, root, clipmin, zorigin, start, end, mins, maxs);
}

BSPMap::TraceResult BSPMap::ModelTrace(const float start[3], const float end[3],
                                       const float mins[3], const float maxs[3],
                                       int model_index, const float ent_origin[3]) const {
    TraceResult out;
    if (clipnodes_.empty()) {
        out.fraction = 1.0f;
        out.endpos[0] = end[0]; out.endpos[1] = end[1]; out.endpos[2] = end[2];
        out.allsolid = true;
        return out;
    }
    if (model_index < 0 || model_index >= (int)models_.size()) {
        model_index = 0;
    }
    const BSPModel& m = models_[model_index];
    int root = 0;
    float clipmin[3] = {0, 0, 0};
    float size0 = maxs[0] - mins[0];
    if (size0 < 0) size0 = -size0;
    if (size0 >= 3) {
        if (size0 <= 32) { root = m.headnode[1]; clipmin[0]=-16; clipmin[1]=-16; clipmin[2]=-24; }
        else { root = m.headnode[2]; clipmin[0]=-32; clipmin[1]=-32; clipmin[2]=-24; }
    } else {
        root = m.headnode[0];
    }
    return TraceThroughHull(*this, root, clipmin, ent_origin, start, end, mins, maxs);
}

bool BSPMap::BoxInSolid(const float origin[3], const float mins[3],
                        const float maxs[3]) const {
    if (clipnodes_.empty()) return false;
    // Real maps bake the player hull into the clip tree, so a point at the
    // entity origin in that hull IS the box-vs-solid test (offset = clipmin-mins).
    float size0 = maxs[0] - mins[0];
    if (size0 < 0) size0 = -size0;
    const BSPModel* world = WorldModel();
    if (!world) return false;
    int root = 0;
    float clipmin[3] = {0, 0, 0};
    if (size0 < 3) { root = world->headnode[0]; }
    else if (size0 <= 32) { root = world->headnode[1]; clipmin[0]=-16; clipmin[1]=-16; clipmin[2]=-24; }
    else { root = world->headnode[2]; clipmin[0]=-32; clipmin[1]=-32; clipmin[2]=-24; }
    float p[3];
    for (int i = 0; i < 3; i++) p[i] = origin[i] - (clipmin[i] - mins[i]);
    return PointContentsHull(p, root) == CONTENTS_SOLID;
}


bool BSPMap::FindPlayerStart(float origin[3], float angles[3]) const {
    origin[0] = origin[1] = origin[2] = 0.0f;
    angles[0] = angles[1] = angles[2] = 0.0f;

    auto ents = ParseEntities();
    for (auto& ent : ents) {
        if (ent.classname == "info_player_start") {
            origin[0] = ent.origin[0];
            origin[1] = ent.origin[1];
            origin[2] = ent.origin[2];
            angles[1] = ent.angles[1];
            return true;
        }
    }
    return false;
}

bool BSPMap::FindSpawnPoint(float origin[3], float angles[3]) const {
    origin[0] = origin[1] = origin[2] = 0.0f;
    angles[0] = angles[1] = angles[2] = 0.0f;

    const char* preferred[] = {
        "info_player_start",
        "info_player_coop",
        "info_player_deathmatch"
    };

    auto valid_at = [this](const float p[3]) -> bool {
        // The full player box (feet at mins, head above) must be clear of
        // solid, not just the origin/eye points — otherwise the player spawns
        // wedged into the floor and the physics can't move them.
        static const float pmin[3] = {-16, -16, -24};
        static const float pmax[3] = {16, 16, 32};
        return !BoxInSolid(p, pmin, pmax);
    };

    auto ents = ParseEntities();
    for (const char* cls : preferred) {
        for (auto& ent : ents) {
            if (ent.classname != cls) continue;
            if (!ent.has_origin) continue;
            // If the authored spawn is embedded in geometry, nudge it up to
            // the first empty spot (feet and eye both clear) instead of
            // spawning the camera inside the level geometry.
            for (int step = 0; step <= 12; step++) {
                float p[3] = { ent.origin[0], ent.origin[1], ent.origin[2] + step * 8.0f };
                if (valid_at(p)) {
                    origin[0] = p[0]; origin[1] = p[1]; origin[2] = p[2];
                    angles[1] = ent.angles[1];
                    return true;
                }
            }
        }
    }

    // No usable start entity: drop a point down from above the world model
    // onto the nearest empty spot so the camera is never outside the level.
    const BSPModel* world = WorldModel();
    if (!world) return false;
    float center[3] = {
        (world->mins[0] + world->maxs[0]) * 0.5f,
        (world->mins[1] + world->maxs[1]) * 0.5f,
        world->maxs[2] + 8.0f
    };
    float bottom = world->mins[2] - 8.0f;
    while (center[2] > bottom) {
        if (valid_at(center)) {
            origin[0] = center[0]; origin[1] = center[1]; origin[2] = center[2];
            return true;
        }
        center[2] -= 8.0f;
    }
    return false;
}

std::vector<BSPEntity> BSPMap::ParseEntities() const {
    std::vector<BSPEntity> out;
    const std::string& ent = entity_string_;
    size_t pos = 0;
    while (pos < ent.size()) {
        size_t open = ent.find('{', pos);
        if (open == std::string::npos) break;
        size_t close = ent.find('}', open);
        if (close == std::string::npos) break;
        std::string block = ent.substr(open, close - open);

        BSPEntity e;
        size_t p = 0;
        while (p < block.size()) {
            size_t q = block.find('"', p);
            if (q == std::string::npos) break;
            size_t q2 = block.find('"', q + 1);
            if (q2 == std::string::npos) break;
            std::string key = block.substr(q + 1, q2 - q - 1);
            p = q2 + 1;
            size_t q3 = block.find('"', p);
            if (q3 == std::string::npos) break;
            size_t q4 = block.find('"', q3 + 1);
            if (q4 == std::string::npos) break;
            std::string value = block.substr(q3 + 1, q4 - q3 - 1);
            p = q4 + 1;

            if (key == "classname") {
                e.classname = value;
            } else if (key == "origin") {
                e.has_origin = (sscanf(value.c_str(), "%f %f %f",
                                       &e.origin[0], &e.origin[1], &e.origin[2]) == 3);
            } else if (key == "angle") {
                float ang = 0;
                if (sscanf(value.c_str(), "%f", &ang) == 1) e.angles[1] = ang;
            } else if (key == "angles") {
                sscanf(value.c_str(), "%f %f %f",
                       &e.angles[0], &e.angles[1], &e.angles[2]);
            } else if (key == "model") {
                e.model = value;
            } else if (key == "frame") {
                e.frame = atoi(value.c_str());
            } else if (key == "skin") {
                e.skin = atoi(value.c_str());
            } else if (key == "scale") {
                e.scale = (float)atof(value.c_str());
            } else if (key == "style") {
                e.style = atoi(value.c_str());
            } else if (key == "light") {
                e.light = atoi(value.c_str());
            }
        }

        if (!e.classname.empty()) {
            out.push_back(std::move(e));
        }
        pos = close + 1;
    }
    return out;
}

} // namespace zq::engine