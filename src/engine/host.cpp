#include "engine/host.hpp"
#include "engine/console.hpp"
#include "engine/cvar_system.hpp"
#include "engine/entity.hpp"
#include "engine/game.hpp"
#include "engine/input.hpp"
#include "engine/particle.hpp"
#include "core/logging/logger.hpp"
#include "vm/vm.hpp"
#include "vm/builtin_registry.hpp"
#include <chrono>

namespace zq::engine {

using clock = std::chrono::steady_clock;

Host& Host::Instance() {
    static Host instance;
    return instance;
}

bool Host::Initialize() {
    log::SetLogger(&log::ConsoleLogger::Instance());
    log::SetMinLevel(log::LogLevel::Info);
    log::Info("zquake host initializing...");
    
    Console::Init();
    Input::Init();
    
    // Register QuakeC builtin handlers
    // Server builtins (range 0-99)
    zq::vm::BuiltinRegistry::Register(0, [](zq::vm::VMState& state) {
        int32_t str_off = *(state.stack_top - 1);
        const char* str = reinterpret_cast<const char*>(state.string_base) + str_off;
        Console::Instance().Print(String(str));
    });
    zq::vm::BuiltinRegistry::Register(1, [](zq::vm::VMState& state) {
        int ent = Entity::GetFreeEdict();
        *state.stack_top++ = ent;
    });
    zq::vm::BuiltinRegistry::Register(2, [](zq::vm::VMState&) {});
    zq::vm::BuiltinRegistry::Register(3, [](zq::vm::VMState&) {});
    zq::vm::BuiltinRegistry::Register(4, [](zq::vm::VMState& state) {
        int32_t str_off = *(state.stack_top - 1);
        const char* msg = reinterpret_cast<const char*>(state.string_base) + str_off;
        log::Error(String(msg));
    });
    
    // Client builtins (range 100+)
    zq::vm::BuiltinRegistry::Register(100, [](zq::vm::VMState& state) {
        int32_t str_off = *(state.stack_top - 1);
        const char* str = reinterpret_cast<const char*>(state.string_base) + str_off;
        Console::Instance().Print(String(str));
    });
    
    // Entity builtins (range 200+)
    zq::vm::BuiltinRegistry::Register(200, [](zq::vm::VMState&) {});
    zq::vm::BuiltinRegistry::Register(201, [](zq::vm::VMState& state) {
        int ent = Entity::GetFreeEdict();
        *state.stack_top++ = ent;
    });
    zq::vm::BuiltinRegistry::Register(202, [](zq::vm::VMState&) {
        *zq::vm::VM::state_.stack_top++ = 0;
    });
    zq::vm::BuiltinRegistry::Register(203, [](zq::vm::VMState&) {});
    zq::vm::BuiltinRegistry::Register(204, [](zq::vm::VMState&) {});
    
    CVarSystem::RegisterCVar("r_fov", "90");
    CVarSystem::RegisterCVar("cl_updaterate", "30");
    CVarSystem::RegisterCVar("sv_maxspeed", "320");
    
    state_ = State::Running;
    return true;
}

void Host::Shutdown() {
    log::Info("zquake host shutting down...");
    state_ = State::Shutdown;
}

void Host::RunFrame() {
    if (state_ != State::Running) return;
    
    auto start = clock::now();
    
    Input::Poll();
    
    float dt = static_cast<float>(frame_time_);
    ParticleSystem::UpdateParticles(dt);
    
    auto end = clock::now();
    frame_time_ = std::chrono::duration<double>(end - start).count();
    if (frame_time_ < 0.001) frame_time_ = 0.001;
    time_ += frame_time_;
}

void Host::Quit() {
    log::Info("zquake shutdown requested");
    state_ = State::Shutdown;
}

Host::State Host::GetState() const { return state_; }
double Host::GetTime() const { return time_; }
double Host::GetFrameTime() const { return frame_time_; }

} // namespace zq::engine
