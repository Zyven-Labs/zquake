#pragma once
// Quake's area-node spatial partitioning (quake/WinQuake/world.c SV_CreateAreaNode,
// SV_UnlinkEdict, SV_LinkEdict, SV_TouchLinks). A depth-4 binary tree over the
// world bounds partitions edicts into 32 nodes so trigger/touch queries only
// visit the nodes a bounding box actually crosses.
//
// This is edict-agnostic: callers (the server frame) compute absolute bounds
// and hand them in. Link state is kept in a side table keyed by an opaque
// edict id.

#include <vector>

namespace zq::engine {

struct AreaNode {
    int axis = -1;          // -1 == leaf; else 0 (x) / 1 (y) split axis
    float dist = 0;
    int child[2] = {-1, -1};
    std::vector<int> triggers;   // SOLID_TRIGGER edicts
    std::vector<int> solids;     // everything else solid
};

inline constexpr int AREA_DEPTH = 4;
inline constexpr int AREA_NODES = 32;

class AreaNodes {
public:
    // (Re)builds the tree from the world model bounds (SV_ClearWorld).
    void Init(const float world_mins[3], const float world_maxs[3]);

    // Remove an edict from whatever node/list it is in (SV_UnlinkEdict).
    void Unlink(int edict);

    // Insert `edict` into the tree using its absolute bounding box
    // (SV_LinkEdict's area-node walk). `trigger` selects trigger/solid list.
    void Link(int edict, const float absmin[3], const float absmax[3],
              bool trigger);

    // SV_TouchLinks: invoke `fn(trigger_edict)` for every SOLID_TRIGGER edict
    // in the nodes overlapping the box (self/other handled by the caller).
    template <typename Fn>
    void ForEachTrigger(const float absmin[3], const float absmax[3], Fn&& fn) {
        TouchWalk(0, absmin, absmax, fn);
    }

    // Collect every solid edict in the nodes overlapping the box.
    void CollectSolids(const float absmin[3], const float absmax[3],
                       std::vector<int>& out) const;

    static bool BoxOverlap(const float amin[3], const float amax[3],
                           const float bmin[3], const float bmax[3]);

    const AreaNode& Node(int i) const { return nodes_[i]; }

private:
    int CreateNode(int depth, const float mins[3], const float maxs[3]);
    void InsertInto(std::vector<int>& list, int edict);
    void RemoveFrom(std::vector<int>& list, int edict);

    template <typename Fn>
    void TouchWalk(int node, const float absmin[3], const float absmax[3],
                   Fn& fn) {
        const AreaNode& n = nodes_[node];
        for (int e : n.triggers)
            if (e >= 0) fn(e);
        if (n.axis == -1) return;
        if (absmax[n.axis] > n.dist) TouchWalk(n.child[0], absmin, absmax, fn);
        if (absmin[n.axis] < n.dist) TouchWalk(n.child[1], absmin, absmax, fn);
    }

    AreaNode nodes_[AREA_NODES];
    int num_nodes_ = 0;
    // side table: -2 = unlinked; else node index + list flag.
    struct LinkState { int node = -2; bool solid = false; };
    std::vector<LinkState> link_state_;
};

} // namespace zq::engine