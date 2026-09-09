#pragma once
#include "core/fixed/fixed_math.hpp"

namespace zq::engine {

struct Particle {
    zq::math::Vec3 origin;
    fixed_t velocity[3];
    fixed_t die;
    fixed_t decay;
    unsigned char color;
    unsigned char size;
};

class ParticleSystem {
public:
    static void StartParticle(const zq::math::Vec3& origin, int count, int color, fixed_t speed);
    static void UpdateParticles(float dt);
    static void RenderParticles();
    
private:
    static constexpr int MAX_PARTICLES = 1024;
    static Particle particles_[MAX_PARTICLES];
};

}
