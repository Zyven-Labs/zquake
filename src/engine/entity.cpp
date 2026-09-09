#include "engine/entity.hpp"

namespace zq::engine {

Edict Entity::edicts_[MAX_EDICTS];

Edict* Entity::GetEdict(int index) {
    if (index < 0 || index >= MAX_EDICTS) return nullptr;
    return &edicts_[index];
}

int Entity::Spawn(Edict* edict) {
    if (!edict) return -1;
    edict->free = false;
    return static_cast<int>(edict - edicts_);
}

void Entity::Remove(Edict* edict) {
    edict->free = true;
}

int Entity::GetFreeEdict() {
    for (int i = 0; i < MAX_EDICTS; i++) {
        if (edicts_[i].free) {
            edicts_[i].free = false;
            return i;
        }
    }
    return -1;
}

}
