#pragma once
#include <cstdint>

namespace zq::engine {

// Quake edict_t - server + client state for each entity
struct Edict {
    // Server-side state
    struct ServerState {
        float origin[3];
        float angles[3];
        int modelindex;
        int frame;
        int skin;
        int effects;
        int colormap;
        int team;
        float scale;
        int solid;
    } server;
    
    // Client-side state
    struct ClientState {
        float origin[3];
        float angles[3];
        float old_origin[3];
        int modelindex;
        int frame;
        int skin;
        int effects;
        int light_level;
    } client;
    
    bool free;
    int serialnumber;
};

class Entity {
public:
    static Edict* GetEdict(int index);
    static int Spawn(Edict* edict);
    static void Remove(Edict* edict);
    static int GetFreeEdict();
    
private:
    static constexpr int MAX_EDICTS = 1024;
    static Edict edicts_[MAX_EDICTS];
};

}
