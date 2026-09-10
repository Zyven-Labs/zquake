#pragma once
#include <cstdint>
#include <vector>

namespace zq::engine {

// One simulated particle (mirrors Quake CL_Particle semantics: position,
// velocity, remaining lifetime, palette color index, size class).
struct Particle {
    float pos[3] = {0, 0, 0};
    float vel[3] = {0, 0, 0};
    float die = 0;      // seconds remaining
    float decay = 0;    // seconds remaining after `die` (kept for fidelity)
    std::uint8_t color = 255;
    std::uint8_t size = 1;
};

// Engine-side particle store. The `particle` QuakeC builtin (#48) spawns here;
// Step() ages them with velocity + gravity each game frame; the renderer
// consumes Snapshot() to place emissive sparks. Consumed on the game thread
// (renderers copy), so no cross-thread sharing is needed.
class ParticleSystem {
public:
    // Spawn `count` particles at org with velocity direction dir (scaled by a
    // random coefficient, like SV_StartParticle / CL_Particle).
    static void StartParticle(const float org[3], const float dir[3],
                              int count, int color);

    static void Step(float dt);
    static void Clear();

    static std::vector<Particle> Snapshot() {
        return std::vector<Particle>(particles_, particles_ + count_visible_);
    }

    static int CountVisible() { return count_visible_; }

    static constexpr int MAX_PARTICLES = 1024;

private:
    static Particle particles_[MAX_PARTICLES];
    static int count_visible_;
};

}