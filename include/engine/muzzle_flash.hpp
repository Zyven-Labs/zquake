// engine/muzzle_flash.hpp — cross-TU signal that a QuakeC state fired the
// weapon. The QW progs never sets EF_MUZZLEFLASH in .effects (disabled in
// qw-qc/defs.qc); shots are signalled with the SVC_MUZZLEFLASH multicast
// (qw-qc/player.qc muzzleflash()). The game builtins capture that message
// (WriteByte/WriteEntity + multicast) and raise this flag; the app's battle
// poll consumes it each tick to strobe the raytracer.
#pragma once
#include <atomic>

namespace zq::engine {

class MuzzleFlashSignal {
public:
    // Raised by the SVC_MUZZLEFLASH multicast handler when the QC fires the
    // weapon; `edict` is the entity the flash belongs to (the QC's `self`).
    static void Signal(int edict);

    // Returns true once if a pending flash was signalled for `edict` (in
    // practice the local client, kClientEdict); clears the pending state.
    static bool Consume(int edict);

private:
    static std::atomic<int> pending_edict_;
};

}