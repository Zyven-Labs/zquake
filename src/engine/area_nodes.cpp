#include "engine/area_nodes.hpp"
#include <cstring>

namespace zq::engine {

static void VecCopy3(const float* a, float* b) { b[0] = a[0]; b[1] = a[1]; b[2] = a[2]; }

static void VecSub3(const float* a, const float* b, float* out) {
    out[0] = a[0] - b[0]; out[1] = a[1] - b[1]; out[2] = a[2] - b[2];
}

int AreaNodes::CreateNode(int depth, const float mins[3], const float maxs[3]) {
    int idx = num_nodes_;
    if (idx >= AREA_NODES) return -1;
    int node = idx;
    num_nodes_++;
    AreaNode& anode = nodes_[node];
    anode.axis = -1;
    anode.dist = 0;
    anode.child[0] = anode.child[1] = -1;

    if (depth == AREA_DEPTH) return node;

    float size[3];
    VecSub3(maxs, mins, size);
    anode.axis = size[0] > size[1] ? 0 : 1;
    anode.dist = 0.5f * (maxs[anode.axis] + mins[anode.axis]);

    float mins1[3], maxs1[3], mins2[3], maxs2[3];
    VecCopy3(mins, mins1);
    VecCopy3(mins, mins2);
    VecCopy3(maxs, maxs1);
    VecCopy3(maxs, maxs2);
    maxs1[anode.axis] = mins2[anode.axis] = anode.dist;

    anode.child[0] = CreateNode(depth + 1, mins2, maxs2);
    anode.child[1] = CreateNode(depth + 1, mins1, maxs1);
    return node;
}

void AreaNodes::Init(const float world_mins[3], const float world_maxs[3]) {
    num_nodes_ = 0;
    nodes_[0] = AreaNode{};
    for (int i = 0; i < AREA_NODES; i++) nodes_[i] = AreaNode{};
    link_state_.assign(0x4000, LinkState{-2, false});
    CreateNode(0, world_mins, world_maxs);
}

void AreaNodes::RemoveFrom(std::vector<int>& list, int edict) {
    for (size_t i = 0; i < list.size(); i++) {
        if (list[i] == edict) {
            list[i] = list.back();
            list.pop_back();
            return;
        }
    }
}

void AreaNodes::InsertInto(std::vector<int>& list, int edict) {
    for (int e : list)
        if (e == edict) return;   // already linked
    list.push_back(edict);
}

void AreaNodes::Unlink(int edict) {
    if (edict < 0 || edict >= (int)link_state_.size()) return;
    LinkState& st = link_state_[edict];
    if (st.node < 0 || st.node >= num_nodes_) { st = LinkState{-2, false}; return; }
    if (st.solid)
        RemoveFrom(nodes_[st.node].solids, edict);
    else
        RemoveFrom(nodes_[st.node].triggers, edict);
    st = LinkState{-2, false};
}

void AreaNodes::Link(int edict, const float absmin[3], const float absmax[3],
                     bool trigger) {
    if (edict < 0) return;
    Unlink(edict);   // reference: SV_LinkEdict unlinks if already linked
    if (edict >= (int)link_state_.size()) return;
    if (num_nodes_ == 0) return;

    int node = 0;
    while (true) {
        const AreaNode& n = nodes_[node];
        if (n.axis == -1) break;
        if (absmin[n.axis] > n.dist)
            node = n.child[0];
        else if (absmax[n.axis] < n.dist)
            node = n.child[1];
        else
            break;   // crosses the node
    }
    if (node < 0) return;

    link_state_[edict] = LinkState{node, !trigger};
    if (trigger)
        InsertInto(nodes_[node].triggers, edict);
    else
        InsertInto(nodes_[node].solids, edict);
}

void AreaNodes::CollectSolids(const float absmin[3], const float absmax[3],
                              std::vector<int>& out) const {
    out.clear();
    // iterative stack walk matching the reference clip traversal
    int stack[AREA_NODES];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
        int idx = stack[--sp];
        if (idx < 0 || idx >= num_nodes_) continue;
        const AreaNode& n = nodes_[idx];
        for (int e : n.solids) out.push_back(e);
        if (n.axis == -1) continue;
        if (absmax[n.axis] > n.dist) stack[sp++] = n.child[0];
        if (absmin[n.axis] < n.dist) stack[sp++] = n.child[1];
    }
}

bool AreaNodes::BoxOverlap(const float amin[3], const float amax[3],
                           const float bmin[3], const float bmax[3]) {
    for (int i = 0; i < 3; i++) {
        if (amin[i] > bmax[i] || bmin[i] > amax[i]) return false;
    }
    return true;
}

} // namespace zq::engine