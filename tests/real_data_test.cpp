// Real-data diagnostic: opens the actual pak0.pak, lists entries, loads a
// real map (maps/e1m1.bsp), and reports what the loader extracts. This is
// the ground-truth check that the pak + BSP loading paths actually work on
// real Quake data.
#include "filesystem/pak_archive.hpp"
#include "engine/bsp.hpp"
#include "engine/mdl_model.hpp"
#include "core/logging/logger.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    zq::log::SetLogger(&zq::log::ConsoleLogger::Instance());
    zq::log::SetMinLevel(zq::log::LogLevel::Error);

    const char* pak_path = argv[1];
    if (!pak_path) pak_path = "id1/pak0.pak";

    zq::fs::PAKArchive pak;
    if (!pak.Open(pak_path)) {
        std::printf("FAIL: cannot open %s\n", pak_path);
        return 1;
    }
    std::printf("pak opened: %s\n", pak.GetName().c_str());
    auto entries = pak.GetEntries();
    std::printf("entries: %zu\n", entries.size());
    if (entries.empty()) {
        std::printf("FAIL: pak directory parsed to 0 entries\n");
        return 1;
    }

    // Print first 8 entries as a sanity check
    for (size_t i = 0; i < 8 && i < entries.size(); i++) {
        std::printf("  entry[%zu] name='%s' offset=%u size=%u\n",
                    i, entries[i].name, entries[i].file_offset, entries[i].file_size);
    }

    // Count entries by extension
    int bsp_count = 0, wad_count = 0, lmp_count = 0, mdl_count = 0, spr_count = 0;
    for (auto& e : entries) {
        const char* n = e.name;
        size_t len = strlen(n);
        if (len >= 4) {
            if (strcmp(n + len - 4, ".bsp") == 0) bsp_count++;
            else if (strcmp(n + len - 4, ".wad") == 0) wad_count++;
            else if (strcmp(n + len - 4, ".lmp") == 0) lmp_count++;
            else if (strcmp(n + len - 4, ".mdl") == 0) mdl_count++;
            else if (strcmp(n + len - 4, ".spr") == 0) spr_count++;
        }
    }
    std::printf("counts: bsp=%d wad=%d lmp=%d mdl=%d spr=%d\n",
                bsp_count, wad_count, lmp_count, mdl_count, spr_count);

    // Look for maps/e1m1.bsp
    const char* mapname = "maps/e1m1.bsp";
    bool found_map = false;
    for (auto& e : entries) {
        if (strcmp(e.name, mapname) == 0) { found_map = true; break; }
    }
    std::printf("found maps/e1m1.bsp: %s\n", found_map ? "YES" : "NO");
    if (!found_map) return 1;

    std::vector<uint8_t> mapdata;
    if (![&pak, mapname](std::vector<uint8_t>& out) -> bool {
        for (auto& e : pak.GetEntries())
            if (strcmp(e.name, mapname) == 0) {
                out.resize(e.file_size);
                return e.file_size == 0 || pak.ReadFile(mapname, out.data(), out.size());
            }
        return false;
    }(mapdata)) {
        std::printf("FAIL: could not read %s data\n", mapname);
        return 1;
    }
    std::printf("map %s bytes: %zu\n", mapname, mapdata.size());

    zq::engine::BSPMap map;
    if (!map.Load(mapdata.data(), mapdata.size())) {
        std::printf("FAIL: BSPMap::Load returned false for %s\n", mapname);
        return 1;
    }
    std::printf("BSP parse OK:\n");
    std::printf("  planes=%zu vertexes=%zu nodes=%zu clipnodes=%zu\n",
                map.Planes().size(), map.Vertexes().size(), map.Nodes().size(), map.ClipNodes().size());
    std::printf("  leafs=%zu faces=%zu edges=%zu surfedges=%zu\n",
                map.Leafs().size(), map.Faces().size(), map.Edges().size(), map.SurfEdges().size());
    std::printf("  texinfos=%zu models=%zu textures=%zu lighting=%zu\n",
                map.TexInfos().size(), map.Models().size(), map.Textures().size(), map.Lighting().size());
    std::printf("  entity_string_len=%zu\n", map.EntityString().size());

    if (map.Faces().empty() || map.Vertexes().empty()) {
        std::printf("FAIL: map has no geometry\n");
        return 1;
    }

    // Player start
    float o[3], a[3];
    bool ps = map.FindPlayerStart(o, a);
    std::printf("player start: %s", ps ? "found " : "NOT FOUND");
    if (ps) std::printf("origin=(%.1f, %.1f, %.1f) yaw=%.1f", o[0], o[1], o[2], a[1]);
    std::printf("\n");

    // Sanity: point contents at player start (should be EMPTY, not SOLID)
    if (ps) {
        int cont = map.PointContents(o);
        std::printf("point content at spawn: %d (%s)\n", cont,
                    cont == -1 ? "EMPTY" : (cont == -2 ? "SOLID (BAD)" : "other"));
        if (cont == -2) {
            std::printf("FAIL: spawn point is inside solid geometry\n");
            return 1;
        }
    }

    // Lighting present?
    std::printf("has lighting data: %s\n", map.Lighting().size() > 0 ? "YES" : "NO");

    // ---- MDL model parsing ----
    const char* mdlname = "progs/player.mdl";
    std::vector<uint8_t> mdl_data;
    for (auto& e : pak.GetEntries()) {
        if (strcmp(e.name, mdlname) == 0) {
            mdl_data.resize(e.file_size);
            pak.ReadFile(e.name, mdl_data.data(), mdl_data.size());
            break;
        }
    }
    if (mdl_data.empty()) {
        std::printf("MODEL FAIL: %s not in pak\n", mdlname);
        return 1;
    }
    std::printf("model %s bytes: %zu\n", mdlname, mdl_data.size());

    zq::engine::MdlModel mdl;
    if (!mdl.Load(mdl_data.data(), mdl_data.size())) {
        std::printf("MODEL FAIL: MdlModel::Load returned false\n");
        return 1;
    }
    std::printf("mdl parse OK: verts=%d tris=%d frames=%d skin=%dx%d skins=%d\n",
                mdl.NumVerts(), mdl.NumTris(), mdl.NumFrames(),
                mdl.SkinWidth(), mdl.SkinHeight(), mdl.NumSkins());
    if (mdl.NumVerts() <= 0 || mdl.NumTris() <= 0 || mdl.NumFrames() < 1 ||
        mdl.SkinWidth() <= 0 || mdl.SkinHeight() <= 0) {
        std::printf("MODEL FAIL: bad mdl geometry counts\n");
        return 1;
    }

    // Build a mesh for frame 0 and sanity-check sizes
    std::vector<float> verts, uv;
    std::vector<uint32_t> tris;
    mdl.BuildMesh(0, verts, uv, tris);
    std::printf("mdl mesh: %zu verts, %zu tris (expect %dx / %dx verts)\n",
                verts.size() / 3, tris.size() / 3,
                (size_t)mdl.NumTris() * 3, (size_t)mdl.NumTris());
    // Vertices are emitted per-triangle for seam-correct UVs.
    if (verts.size() / 3 != (size_t)mdl.NumTris() * 3 || tris.size() / 3 != (size_t)mdl.NumTris()) {
        std::printf("MODEL FAIL: mesh build mismatch\n");
        return 1;
    }

    // ---- Entity parsing ----
    {
        auto ents = map.ParseEntities();
        std::printf("entities parsed: %zu\n", ents.size());
        int with_model = 0, player_starts = 0;
        for (auto& e : ents) {
            if (!e.model.empty()) with_model++;
            if (e.classname == "info_player_start") player_starts++;
        }
        std::printf("  entities with model: %d, player_starts: %d\n", with_model, player_starts);
        if (ents.empty() || player_starts == 0) {
            std::printf("ENTFAIL: no useful entities\n");
            return 1;
        }
        // print a few model-bearing entities
        int shown = 0;
        for (auto& e : ents) {
            if (!e.model.empty() && shown < 5) {
                std::printf("  ent: class=%s model=%s origin=(%.0f %.0f %.0f) frame=%d\n",
                            e.classname.c_str(), e.model.c_str(),
                            e.origin[0], e.origin[1], e.origin[2], e.frame);
                shown++;
            }
        }
    }

    std::printf("PASSDATA-OK\n");
    return 0;
}