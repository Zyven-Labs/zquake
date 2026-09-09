// Inspect the real e1m1 map: world-model face range vs all faces, submodel
// geometry, and whether FacePolygon returns in-bounds coordinates.
#include "filesystem/pak_archive.hpp"
#include "engine/bsp.hpp"
#include "core/logging/logger.hpp"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>

int main() {
    zq::log::SetMinLevel(zq::log::LogLevel::Error);
    zq::fs::PAKArchive pak;
    if (!pak.Open("id1/pak0.pak")) { printf("pak fail\n"); return 1; }
    std::vector<uint8_t> md;
    for (auto& e : pak.GetEntries())
        if (strcmp(e.name, "maps/e1m1.bsp") == 0) { md.resize(e.file_size); pak.ReadFile(e.name, md.data(), md.size()); break; }
    if (md.empty()) { printf("no map\n"); return 1; }
    zq::engine::BSPMap map;
    if (!map.Load(md.data(), md.size())) { printf("bsp fail\n"); return 1; }

    size_t tf = map.Faces().size();
    printf("total faces=%zu vertexes=%zu models=%zu\n", tf, map.Vertexes().size(), map.Models().size());
    const auto* w = map.WorldModel();
    if (!w) { printf("no world\n"); return 1; }
    printf("world firstface=%d numfaces=%d\n", w->firstface, w->numfaces);
    printf("world mins=(%.1f %.1f %.1f) maxs=(%.1f %.1f %.1f)\n",
           w->mins[0], w->mins[1], w->mins[2], w->maxs[0], w->maxs[1], w->maxs[2]);

    long sum = 0, mx = 0;
    for (auto& m : map.Models()) { sum += m.numfaces; if (m.firstface+m.numfaces > mx) mx = m.firstface+m.numfaces; }
    printf("sum model numfaces=%ld over range up to %ld (world=0..%d)\n",
           sum, mx, w->numfaces);

    // Count faces with big / garbage polygons from FacePolygon
    long bad = 0, degenerate = 0;
    double minx=1e9,miny=1e9,minz=1e9,maxx=-1e9,maxy=-1e9,maxz=-1e9;
    for (int fi = w->firstface; fi < w->firstface + w->numfaces && fi < (int)tf; fi++) {
        auto poly = map.FacePolygon(map.Faces()[fi]);
        if (poly.size() < 3) { degenerate++; continue; }
        for (auto& v : poly) {
            minx = std::min(minx,(double)v.point[0]); maxx = std::max(maxx,(double)v.point[0]);
            miny = std::min(miny,(double)v.point[1]); maxy = std::max(maxy,(double)v.point[1]);
            minz = std::min(minz,(double)v.point[2]); maxz = std::max(maxz,(double)v.point[2]);
        }
    }
    printf("FacePolygon over world faces: vertex bbox=(%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
           minx,miny,minz,maxx,maxy,maxz);
    printf("  vs model bbox=(%.0f %.0f %.0f)-(%.0f %.0f %.0f)\n",
           w->mins[0],w->mins[1],w->mins[2], w->maxs[0],w->maxs[1],w->maxs[2]);
    printf("  degenerate(<3 verts) polys=%ld\n", degenerate);

    // A sample world face
    int fi = w->firstface;
    auto poly = map.FacePolygon(map.Faces()[fi]);
    printf("sample face[%d]: %zu verts:", fi, poly.size());
    for (auto& v : poly) printf(" (%.0f,%.0f,%.0f)", v.point[0],v.point[1],v.point[2]);
    printf("\n");
    return 0;
}