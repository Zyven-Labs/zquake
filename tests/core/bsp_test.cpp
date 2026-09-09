#include <catch2/catch_test_macros.hpp>
#include "engine/bsp.hpp"
#include <cstring>
#include <vector>
#include <cstdint>
#include <cmath>

using namespace zq::engine;

// Build a minimal but valid Quake BSP v29 in memory.
// Contains:
//  - one plane (normal +Z, dist 0) => solid above y, empty below
//  - clipnodes: node 0 -> if dist >= 0 go child[0]=-1(empty) else child[1]=-2(solid)
//  - entities string with an info_player_start
struct TinyBSP {
    std::vector<uint8_t> data;

    // Lumps
    static constexpr int LUMP_PLANES = 1;
    static constexpr int LUMP_CLIPNODES = 9;

    struct Lump { int32_t fileofs, filelen; };

    TinyBSP() {
        struct Header {
            int32_t version;
            Lump lumps[15];
        };

        // Plane (8 bytes): normal[3] float + dist float + type int -> actually 12 bytes? No:
        // BSPPlane = 12 bytes (3 floats) + 4 bytes (dist) + 4 bytes (type) = 20 bytes. Wait no.
        // Quake BSP plane is: vec3_t normal; float dist; int type; = 20 bytes.
        // bspfile.h: typedef struct { vec3_t normal; vec_t dist; int type; } dplane_t;
        // vec3_t is float[3] = 12 bytes, dist 4, type 4 = 20 bytes total.
        std::vector<BSPPlane> planes;
        BSPPlane pl = {};
        pl.normal[0] = 0; pl.normal[1] = 0; pl.normal[2] = 1;
        pl.dist = 0;
        pl.type = PLANE_Z;
        planes.push_back(pl);

        std::vector<BSPClipNode> clipnodes;
        BSPClipNode cn = {};
        cn.planenum = 0;
        cn.children[0] = -1; // CONTENTS_EMPTY = -1, in front (d>=0)
        cn.children[1] = -2; // CONTENTS_SOLID = -2, behind (d<0)
        clipnodes.push_back(cn);

        std::string ents =
            "{\n\"classname\" \"worldspawn\"\n}\n"
            "{\n\"classname\" \"info_player_start\"\n\"origin\" \"64 64 32\"\n\"angle\" \"90\"\n}\n";

        Header h = {};
        h.version = 29;

        int32_t offset = sizeof(Header);

        // Planes lump
        h.lumps[LUMP_PLANES].fileofs = offset;
        h.lumps[LUMP_PLANES].filelen = (int32_t)(planes.size() * sizeof(BSPPlane));
        offset += h.lumps[LUMP_PLANES].filelen;

        // Clipnodes lump
        h.lumps[LUMP_CLIPNODES].fileofs = offset;
        h.lumps[LUMP_CLIPNODES].filelen = (int32_t)(clipnodes.size() * sizeof(BSPClipNode));
        offset += h.lumps[LUMP_CLIPNODES].filelen;

        // Entities lump
        h.lumps[0].fileofs = offset;
        h.lumps[0].filelen = (int32_t)ents.size();
        offset += (int32_t)ents.size();

        data.resize(offset);
        std::memcpy(data.data(), &h, sizeof(h));
        std::memcpy(data.data() + h.lumps[LUMP_PLANES].fileofs, planes.data(),
                    h.lumps[LUMP_PLANES].filelen);
        std::memcpy(data.data() + h.lumps[LUMP_CLIPNODES].fileofs, clipnodes.data(),
                    h.lumps[LUMP_CLIPNODES].filelen);
        std::memcpy(data.data() + h.lumps[0].fileofs, ents.data(), ents.size());
    }
};

TEST_CASE("BSP loader basic", "[bsp]") {
    TinyBSP t;

    BSPMap map;
    bool ok = map.Load(t.data.data(), t.data.size());
    REQUIRE(ok);

    SECTION("Planes parsed") {
        REQUIRE(map.Planes().size() == 1);
        REQUIRE(map.Planes()[0].normal[2] == 1.0f);
    }

    SECTION("Clipnodes parsed") {
        REQUIRE(map.ClipNodes().size() == 1);
    }

    SECTION("Entities parsed") {
        float origin[3] = {0,0,0};
        float angles[3] = {0,0,0};
        REQUIRE(map.FindPlayerStart(origin, angles));
        REQUIRE(origin[0] == 64.0f);
        REQUIRE(origin[1] == 64.0f);
        REQUIRE(origin[2] == 32.0f);
        REQUIRE(angles[1] == 90.0f);
    }
}

TEST_CASE("BSP PointContents", "[bsp]") {
    TinyBSP t;
    BSPMap map;
    REQUIRE(map.Load(t.data.data(), t.data.size()));

    // Above plane z=0 (dist 0, normal +Z): d = z - 0 >= 0 -> child[0] = empty
    float above[3] = {0, 0, 10};
    REQUIRE(map.PointContents(above) == CONTENTS_EMPTY);

    // Below plane z=0: d = z < 0 -> child[1] = solid
    float below[3] = {0, 0, -10};
    REQUIRE(map.PointContents(below) == CONTENTS_SOLID);

    // On the plane (d = 0): goes front -> empty
    float on[3] = {0, 0, 0};
    REQUIRE(map.PointContents(on) == CONTENTS_EMPTY);
}

TEST_CASE("BSP TraceMove", "[bsp]") {
    TinyBSP t;
    BSPMap map;
    REQUIRE(map.Load(t.data.data(), t.data.size()));

    SECTION("Free move above plane") {
        float start[3] = {0, 0, 10};
        float end[3] = {5, 0, 20};
        float result[3];
        map.TraceMove(start, end, &result[0]);
        REQUIRE(std::fabs(result[0] - 5.0f) < 0.01f);
        REQUIRE(std::fabs(result[2] - 20.0f) < 0.01f);
    }

    SECTION("Blocked by solid plane from above") {
        // Move from z=10 to z=-10 crosses z=0 plane into solid
        float start[3] = {0, 0, 10};
        float end[3] = {0, 0, -10};
        float result[3];
        int plane = -1;
        float dist = 0;
        map.TraceMove(start, end, &result[0], &plane, &dist);
        REQUIRE(plane == 0);
        // Should stop at/near the plane (z=0), not below it
        REQUIRE(result[2] >= -0.1f);
        REQUIRE(result[2] <= 0.1f);
    }
}

TEST_CASE("BSP invalid data", "[bsp]") {
    BSPMap map;
    REQUIRE_FALSE(map.Load(nullptr, 0));

    // Wrong version
    std::vector<uint8_t> bad(sizeof(BSPHeader));
    auto* hdr = reinterpret_cast<BSPHeader*>(bad.data());
    hdr->version = 99;
    REQUIRE_FALSE(map.Load(bad.data(), bad.size()));
}
