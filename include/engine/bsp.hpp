#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>
#include "core/container/string.hpp"

namespace zq::engine {

// Quake BSP file format version 29
struct BSPHeader {
    int32_t version;
    struct { int32_t fileofs, filelen; } lumps[15];
};

inline constexpr int LUMP_ENTITIES = 0;
inline constexpr int LUMP_PLANES = 1;
inline constexpr int LUMP_TEXTURES = 2;
inline constexpr int LUMP_VERTEXES = 3;
inline constexpr int LUMP_VISIBILITY = 4;
inline constexpr int LUMP_NODES = 5;
inline constexpr int LUMP_TEXINFO = 6;
inline constexpr int LUMP_FACES = 7;
inline constexpr int LUMP_LIGHTING = 8;
inline constexpr int LUMP_CLIPNODES = 9;
inline constexpr int LUMP_LEAFS = 10;
inline constexpr int LUMP_MARKSURFACES = 11;
inline constexpr int LUMP_EDGES = 12;
inline constexpr int LUMP_SURFEDGES = 13;
inline constexpr int LUMP_MODELS = 14;
inline constexpr int HEADER_LUMPS = 15;

inline constexpr int CONTENTS_EMPTY = -1;
inline constexpr int CONTENTS_SOLID = -2;
inline constexpr int CONTENTS_WATER = -3;
inline constexpr int CONTENTS_SKY = -6;
inline constexpr int CONTENTS_CLIP = -8;

struct BSPVertex {
    float point[3];
};

struct BSPPlane {
    float normal[3];
    float dist;
    int type;
};

enum { PLANE_X = 0, PLANE_Y = 1, PLANE_Z = 2, PLANE_ANYX = 3, PLANE_ANYY = 4, PLANE_ANYZ = 5 };

struct BSPNode {
    int32_t planenum;
    int32_t children[2];   // negative = -(leaf+1)
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t firstface;
    uint16_t numfaces;
};

struct BSPClipNode {
    int32_t planenum;
    int16_t children[2];   // negative = -contents
};

struct BSPLeaf {
    int32_t contents;
    int32_t visofs;
    int16_t mins[3];
    int16_t maxs[3];
    uint16_t firstmarksurface;
    uint16_t nummarksurfaces;
    uint8_t ambient_level[4];
};

struct BSPTexInfo {
    float vecs[2][4];
    int32_t miptex;
    int32_t flags;
};
inline constexpr int TEX_SPECIAL = 1;

struct BSPFace {
    int16_t planenum;
    int16_t side;
    int32_t firstedge;
    int16_t numedges;
    int16_t texinfo;
    uint8_t styles[4];
    int32_t lightofs;
};

struct BSPEdge {
    uint16_t v[2];
};

struct BSPModel {
    float mins[3], maxs[3];
    float origin[3];
    int32_t headnode[4];
    int32_t visleafs;
    int32_t firstface;
    int32_t numfaces;
};

// 8-bit texture (mip level 0)
struct BSPTexture {
    char name[16];
    uint32_t width;
    uint32_t height;
    std::vector<uint8_t> pixels;   // 8-bit indices
};

struct BSPSurface {
    int texinfo_index;
    int plane_index;
    bool side;
    int firstedge;
    int numedges;
    int lightofs;          // offset into lighting lump, -1 if none
    std::vector<int> edges;  // surfedge indices
};

// A map entity parsed from the entities lump ("classname" "value" pairs).
struct BSPEntity {
    std::string classname;
    float origin[3] = {0, 0, 0};
    float angles[3] = {0, 0, 0};
    std::string model;      // e.g. "progs/player.mdl" or "*1" (brush submodel)
    int frame = 0;
    int skin = 0;
    float scale = 1.0f;
    int style = 0;
    int light = 0;
    bool has_origin = false;
};

class BSPMap {
public:
    bool Load(const uint8_t* data, size_t size);

    // Geometry
    const std::vector<BSPPlane>& Planes() const { return planes_; }
    const std::vector<BSPVertex>& Vertexes() const { return vertexes_; }
    const std::vector<BSPNode>& Nodes() const { return nodes_; }
    const std::vector<BSPClipNode>& ClipNodes() const { return clipnodes_; }
    const std::vector<BSPLeaf>& Leafs() const { return leafs_; }
    const std::vector<BSPTexInfo>& TexInfos() const { return texinfos_; }
    const std::vector<BSPFace>& Faces() const { return faces_; }
    const std::vector<BSPEdge>& Edges() const { return edges_; }
    const std::vector<int>& SurfEdges() const { return surfedges_; }
    const std::vector<BSPModel>& Models() const { return models_; }
    const std::vector<BSPTexture>& Textures() const { return textures_; }
    const std::vector<uint8_t>& Lighting() const { return lighting_; }
    const std::string& EntityString() const { return entity_string_; }

    // World model (model 0)
    const BSPModel* WorldModel() const {
        return models_.empty() ? nullptr : &models_[0];
    }

    // Compute polygon vertices for a face in world space
    std::vector<BSPVertex> FacePolygon(const BSPFace& face) const;
    const BSPPlane& FacePlane(const BSPFace& face) const;

    // Point contents test using clip hull
    int PointContents(const float point[3]) const;
    // Point contents test using a specific clip hull headnode (1=player, 2=large).
    int PointContentsHull(const float point[3], int headnode) const;
    // Point contents starting from a specific clipnode (not root 0), used by
    // the hull trace to match Quake's SV_HullPointContents(child, mid).
    int PointContentsFromNode(const float point[3], int nodenum) const;
    // Moves a point with collision against hull 0 (Quake SV_RecursiveHullCheck algorithm).
    // Fills result with the furthest reachable position, and, on impact,
    // the index of the plane that stopped the move plus its distance.
    void TraceMove(const float start[3], const float end[3], float result[3],
                   int* hit_plane = nullptr, float* hit_dist = nullptr) const;
    // Box trace using a specific clip hull headnode (1=player, 2=large).
    void BoxTrace(const float start[3], const float end[3], float result[3],
                  int headnode) const;

    // Full box trace result (mirrors trace_t from the reference).
    struct TraceResult {
        float fraction = 1.0f;         // 1 = no obstruction
        float endpos[3] = {0, 0, 0};
        float plane_normal[3] = {0, 0, 0};
        float plane_dist = 0;
        bool allsolid = false;         // start inside solid and never left
        bool startsolid = false;
        bool inopen = false;
        bool inwater = false;
        bool hit_brush = true;         // blocked by a SOLID_BSP surface (world/submodel)
        int hit_edict = -1;            // blocking edict (1..1023); 0 = world/submodel
    };

    // Quake SV_ClipMoveToEntity / SV_Move against the world BSP: a box
    // (mins..maxs around its centre) moved from start to end. The box
    // extents expand each clip plane by dot(|normal|, half-extents) during
    // the walk, which is exactly the baked-hull expansion qbsp performs.
    TraceResult WorldTrace(const float start[3], const float end[3],
                           const float mins[3], const float maxs[3]) const;

    // Same trace against a specific BSP submodel (used for SOLID_BSP brush
    // entities such as doors / func_wall), with the entity origin offset
    // applied (SV_HullForEntity).
    TraceResult ModelTrace(const float start[3], const float end[3],
                           const float mins[3], const float maxs[3],
                           int model_index, const float ent_origin[3]) const;

    // True if the box (origin with mins..maxs) overlaps any SOLID region.
    // Used for spawn validation and static containment tests.
    bool BoxInSolid(const float origin[3], const float mins[3],
                    const float maxs[3]) const;

    // Parse entities string, find "classname" "info_player_start"
    bool FindPlayerStart(float origin[3], float angles[3]) const;

    // Find a guaranteed playable spawn point. Prefers info_player_start,
    // then coop/deathmatch starts; validates the point is not inside solid
    // (nudging up if embedded) and falls back to a point centered over the
    // world model if no start entity exists at all. Returns false only if
    // the map has no geometry to stand on.
    bool FindSpawnPoint(float origin[3], float angles[3]) const;

    // Parse the entity string into structured entities.
    std::vector<BSPEntity> ParseEntities() const;

private:
    bool LoadLump(const uint8_t* data, size_t size, int lump, const void** out, size_t* out_len) const;

    // After loading, some tools write brush submodel clip trees whose geometry
    // is solid across huge spans that have nothing to do with the submodel's
    // own model bounds (e1m1's doors claim solid right at the player spawn).
    // That turns SOLID_BSP doors/plats into invisible walls that block gunshot
    // traces and movement at arbitrary points. For each submodel hull we sample
    // the whole world on a sparse grid; a healthy tree is solid only near its
    // own bounds, so its sample count scales with them. A tree whose solid
    // sample count vastly exceeds its own expected footprint is bogus and that
    // hull is neutralised (CONTENTS_EMPTY) for collision.
    void SanitizeSubmodelHulls();

    std::vector<BSPPlane> planes_;
    std::vector<BSPVertex> vertexes_;
    std::vector<BSPNode> nodes_;
    std::vector<BSPClipNode> clipnodes_;
    std::vector<BSPLeaf> leafs_;
    std::vector<BSPTexInfo> texinfos_;
    std::vector<BSPFace> faces_;
    std::vector<BSPEdge> edges_;
    std::vector<int> surfedges_;
    std::vector<BSPModel> models_;
    std::vector<BSPTexture> textures_;
    std::vector<uint8_t> lighting_;
    std::string entity_string_;
    std::vector<uint16_t> marksurfaces_;
};

} // namespace zq::engine