#include "engine/particle.hpp"
#include <cstdlib>

namespace zq::engine {

Particle ParticleSystem::particles_[MAX_PARTICLES];
int ParticleSystem::count_visible_ = 0;

void ParticleSystem::StartParticle(const float org[3], const float dir[3],
                                   int count, int color) {
    if (count <= 0) count = 1;
    if (count > 256) count = 256;
    for (int i = 0; i < count && count_visible_ < MAX_PARTICLES; i++) {
        Particle& p = particles_[count_visible_++];
        p.pos[0] = org[0]; p.pos[1] = org[1]; p.pos[2] = org[2];
        // Quake scales the direction by a random-coefficient each particle
        // (CL_Particle), producing a spray rather than a rigid beam.
        float coef = 0.3f + (float)(rand() % 1000) / 1000.0f * 0.9f;
        float speed = 60.0f + (float)(rand() % 300);
        p.vel[0] = dir[0] * coef * speed;
        p.vel[1] = dir[1] * coef * speed;
        p.vel[2] = dir[2] * coef * speed;
        p.die = 0.4f + (float)(rand() % 500) / 1000.0f * 0.7f;
        p.decay = p.die;
        p.color = (std::uint8_t)color;
        p.size = 1;
    }
}

void ParticleSystem::Step(float dt) {
    int n = 0;
    for (int i = 0; i < count_visible_; i++) {
        Particle& p = particles_[i];
        p.die -= dt;
        if (p.die <= 0.0f) continue;
        // Move with light gravity; sparks and puffs settle out fast.
        p.vel[2] -= 20.0f * dt;
        p.pos[0] += p.vel[0] * dt;
        p.pos[1] += p.vel[1] * dt;
        p.pos[2] += p.vel[2] * dt;
        if (n != i) particles_[n] = p;
        n++;
    }
    count_visible_ = n;
}

void ParticleSystem::Clear() {
    count_visible_ = 0;
}

}