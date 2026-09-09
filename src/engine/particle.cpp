#include "engine/particle.hpp"
#include "core/fixed/fixed_math.hpp"

namespace zq::engine {

constexpr int ParticleSystem::MAX_PARTICLES;
Particle ParticleSystem::particles_[MAX_PARTICLES];

void ParticleSystem::StartParticle(const zq::math::Vec3& origin, int count, int color, fixed_t speed) {
    (void)count;
    (void)speed;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particles_[i].die.value) {
            particles_[i].origin.x = origin.x;
            particles_[i].origin.y = origin.y;
            particles_[i].origin.z = origin.z;
            particles_[i].die = zq::math::FixedFromInt(3); // 3 seconds
            particles_[i].color = color;
            particles_[i].size = 1;
            break;
        }
    }
}

void ParticleSystem::UpdateParticles(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles_[i].die.value > 0) {
            particles_[i].die -= fixed_t(static_cast<int32_t>(dt * 256));
        }
    }
}

void ParticleSystem::RenderParticles() {
    // Submit particle draw commands to render thread
}

} // namespace zq::engine
