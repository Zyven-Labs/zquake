#include "vm/prog_builtins.hpp"
#include "vm/quakeprog.hpp"
#include "core/logging/logger.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

namespace zq::vm {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

inline int32_t zfl(float f) { int32_t x; memcpy(&x, &f, 4); return x; }
inline float zff(int32_t i) { float f; memcpy(&f, &i, 4); return f; }

// Reference defs.h entity solid constants (progs numeric values).
constexpr float SOLID_SLIDEBOX = 3.0f;

void Unimplemented(ProgVM& vm, int num) {
    char b[64];
    snprintf(b, sizeof(b), "builtin %d unimplemented", num);
    log::Warn(b);
    vm.SetReturnInt(0);
}

// ---- PF_find (QW #21): find next entity after start matching classname/targetname ----
void PF_find(ProgVM& vm) {
    int start = vm.ParmEdictNum(0);
    const char* class_name = vm.ParmString(1);
    const char* target_name = vm.ParmString(2);

    int fclass = vm.FindField("classname");
    int ftarget = vm.FindField("targetname");
    int fmodel = vm.FindField("model");

    for (int e = start + 1; e < ProgVM::MAX_EDICTS; e++) {
        if (vm.EdictFree(e)) continue;
        if (class_name && class_name[0]) {
            if (class_name[0] == '*') {
                const char* m = fmodel >= 0 ? vm.EdictFieldString(e, fmodel) : "";
                if (m && strcmp(m, class_name + 1) == 0) { vm.SetReturnEdict(e); return; }
            } else if (fclass >= 0) {
                const char* c = vm.EdictFieldString(e, fclass);
                if (c && strcmp(c, class_name) == 0) { vm.SetReturnEdict(e); return; }
            }
        }
        if (target_name && target_name[0]) {
            const char* t = ftarget >= 0 ? vm.EdictFieldString(e, ftarget) : "";
            if (t && strcmp(t, target_name) == 0) { vm.SetReturnEdict(e); return; }
        }
    }
    vm.SetReturnInt(0);
}

// ---- PF_makevectors (QW #1): sets global v_forward / v_up / v_right ----
void PF_makevectors(ProgVM& vm) {
    float ang[3];
    vm.ParmVector(0, ang);
    float sy = sinf(ang[1] * M_PI / 180.0f);
    float cy = cosf(ang[1] * M_PI / 180.0f);
    float sp = sinf(ang[0] * M_PI / 180.0f);
    float cp = cosf(ang[0] * M_PI / 180.0f);

    int vf = vm.FindGlobal("v_forward");
    int vu = vm.FindGlobal("v_up");
    int vr = vm.FindGlobal("v_right");
    // Classic Quake convention (matches vectoyaw = atan2(v.y, v.x)):
    //   v_forward = (cos(yaw)*cos(pitch), sin(yaw)*cos(pitch), sin(pitch))
    //   v_right   = (-sin(yaw), cos(yaw), 0)
    //   v_up      = (-sin(pitch)*cos(yaw), -sin(pitch)*sin(yaw), cos(pitch))
    if (vf >= 0) vm.Global(vf + 0) = zfl(cy * cp), vm.Global(vf + 1) = zfl(sy * cp), vm.Global(vf + 2) = zfl(sp);
    if (vu >= 0) vm.Global(vu + 0) = zfl(-sp * cy), vm.Global(vu + 1) = zfl(-sp * sy), vm.Global(vu + 2) = zfl(cp);
    if (vr >= 0) vm.Global(vr + 0) = zfl(-sy), vm.Global(vr + 1) = zfl(cy), vm.Global(vr + 2) = zfl(0);
}

void PF_setorigin(ProgVM& vm) {
    int e = vm.ParmEdictNum(0);
    float o[3];
    vm.ParmVector(1, o);
    int f = vm.FindField("origin");
    if (f >= 0 && e >= 0) vm.SetEdictFieldVector(e, f, o);
}

void PF_setmodel(ProgVM& vm) {
    int e = vm.ParmEdictNum(0);
    const char* m = vm.ParmString(1);
    int f = vm.FindField("model");
    if (f >= 0 && e >= 0) vm.EdictFieldInt(e, f) = vm.InternString(m);
    // Brush submodels are referenced as "*N"; their modelindex is N (the
    // index into the BSP Models() list, world = 0). This is what the engine
    // uses to pick the clip hull for SOLID_BSP entities (SV_HullForEntity).
    int fm = vm.FindField("modelindex");
    if (fm >= 0 && e >= 0) {
        int idx = 0;
        if (m && m[0] == '*') {
            idx = atoi(m + 1);
        }
        vm.EdictFieldFloat(e, fm) = (float)idx;
    }

    // Reference ED_SetModel (pr_cmds.c): for SOLID_BSP / SOLID_SLIDEBOX, seed
    // mins/maxs/size from the brush submodel's world-space bounds so door /
    // plat thickness (self.size) and clip hulls are correct.
    if (!(m && e >= 0 && m[0] == '*')) return;
    int idx = atoi(m + 1);
    float mn[3], mx[3];
    if (!vm.BrushSubmodelBounds(idx, mn, mx)) return;

    // The callback already returns world-aligned bounds (submodel origin +
    // local box), matching how this engine carries brush geometry: edicts ride
    // a zero "parking" origin and the renderer bakes submodels in place.
    // Reference ED_SetModel folds the entity origin in instead; we don't, so
    // doors/plats colliding at their true position keep correct boxes.
    int fsolid = vm.FindField("solid");
    float solid = fsolid >= 0 ? vm.EdictFieldFloat(e, fsolid) : 0;
    if (solid == SOLID_SLIDEBOX) {  // reference subtracts/extends a 1-unit margin
        for (int i = 0; i < 3; i++) {
            mn[i] -= 1;
            mx[i] += 1;
        }
    }
    int fmins = vm.FindField("mins"), fmaxs = vm.FindField("maxs");
    int fsize = vm.FindField("size");
    if (fmins >= 0) vm.SetEdictFieldVector(e, fmins, mn);
    if (fmaxs >= 0) vm.SetEdictFieldVector(e, fmaxs, mx);
    if (fsize >= 0) {
        float sz[3] = {mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]};
        vm.SetEdictFieldVector(e, fsize, sz);
    }
}

void PF_setsize(ProgVM& vm) {
    int e = vm.ParmEdictNum(0);
    float mn[3], mx[3];
    vm.ParmVector(1, mn);
    vm.ParmVector(2, mx);
    int f = vm.FindField("mins");
    if (f >= 0 && e >= 0) vm.SetEdictFieldVector(e, f, mn);
    f = vm.FindField("maxs");
    if (f >= 0 && e >= 0) vm.SetEdictFieldVector(e, f, mx);
}

void PF_error(ProgVM& vm) {
    vm.RunError("Program error: %s", vm.ParmString(0));
}

void PF_vlen(ProgVM& vm) {
    float v[3];
    vm.ParmVector(0, v);
    vm.SetReturnFloat(sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
}

void PF_vectoyaw(ProgVM& vm) {
    float v[3];
    vm.ParmVector(0, v);
    float yaw = 0.0f;
    if (v[1] == 0.0f && v[0] == 0.0f) {
        yaw = 0.0f;
    } else {
        yaw = (float)(atan2(v[1], v[0]) * 180.0f / M_PI);
        if (yaw < 0.0f) yaw += 360.0f;
    }
    vm.SetReturnFloat(yaw);
}

void PF_vectoangles(ProgVM& vm) {
    float v[3];
    vm.ParmVector(0, v);
    float forward = sqrtf(v[0] * v[0] + v[1] * v[1]);
    float angles[3] = {0, 0, 0};
    if (forward == 0.0f && v[2] == 0.0f) {
        angles[0] = 0.0f;
    } else {
        angles[0] = (float)(atan2(v[2], forward) * 180.0f / M_PI);
    }
    if (v[0] == 0.0f && v[1] == 0.0f) {
        angles[1] = 0.0f;
    } else {
        float yaw = (float)(atan2(v[1], v[0]) * 180.0f / M_PI);
        angles[1] = yaw;
    }
    vm.SetReturnVector(angles);
}

} // namespace

void RegisterDefaultBuiltins(ProgVM& vm) {
    // index 0 is unused in the reference table (PF_Fixme)
    vm.RegisterBuiltin(1, PF_makevectors);
    vm.RegisterBuiltin(2, PF_setorigin);
    vm.RegisterBuiltin(3, PF_setmodel);
    vm.RegisterBuiltin(4, PF_setsize);
    vm.RegisterBuiltin(6, [](ProgVM& v) { (void)v; log::Warn("PF_break"); });
    vm.RegisterBuiltin(7, [](ProgVM& v) { v.SetReturnFloat(rand() / (float)INT32_MAX); });
    vm.RegisterBuiltin(10, PF_error);
    vm.RegisterBuiltin(11, PF_error);
    vm.RegisterBuiltin(12, PF_vlen);
    vm.RegisterBuiltin(13, PF_vectoyaw);
    vm.RegisterBuiltin(14, [](ProgVM& v) { v.SetReturnEdict(v.AllocEdict()); });
    vm.RegisterBuiltin(15, [](ProgVM& v) { v.FreeEdict(v.ParmEdictNum(0)); });
    vm.RegisterBuiltin(19, [](ProgVM& v) { v.SetReturnString(v.ParmString(0)); }); // precache_sound
    vm.RegisterBuiltin(20, [](ProgVM& v) { v.SetReturnString(v.ParmString(0)); }); // precache_model
    vm.RegisterBuiltin(23, [](ProgVM& v) { log::Info(v.ParmString(1)); }); // bprint
    vm.RegisterBuiltin(24, [](ProgVM& v) { log::Info(v.ParmString(2)); }); // sprint
    vm.RegisterBuiltin(25, [](ProgVM& v) { log::Info(v.ParmString(0)); }); // dprint
    vm.RegisterBuiltin(26, [](ProgVM& v) { // ftos
        char b[32];
        snprintf(b, sizeof(b), "%f", v.ParmFloat(0));
        v.SetReturnString(b);
    });
    vm.RegisterBuiltin(27, [](ProgVM& v) { // vtos
        float o[3];
        v.ParmVector(0, o);
        char b[64];
        snprintf(b, sizeof(b), "'%f %f %f'", o[0], o[1], o[2]);
        v.SetReturnString(b);
    });
    vm.RegisterBuiltin(36, [](ProgVM& v) { v.SetReturnFloat(rintf(v.ParmFloat(0))); });
    vm.RegisterBuiltin(37, [](ProgVM& v) { v.SetReturnFloat(floorf(v.ParmFloat(0))); });
    vm.RegisterBuiltin(38, [](ProgVM& v) { v.SetReturnFloat(ceilf(v.ParmFloat(0))); });
    vm.RegisterBuiltin(43, [](ProgVM& v) { v.SetReturnFloat(fabsf(v.ParmFloat(0))); });
    vm.RegisterBuiltin(51, PF_vectoangles);
    vm.RegisterBuiltin(70, [](ProgVM& v) { log::Info("changelevel: "); log::Info(v.ParmString(0)); });
    vm.RegisterBuiltin(81, [](ProgVM& v) { v.SetReturnFloat((float)atof(v.ParmString(0))); });

    // everything else: register stubs so calls don't abort the VM
    for (int i = 0; i <= 82; i++) {
        if (i == 0) continue;
        if (!vm.HasBuiltin(i)) {
            vm.RegisterBuiltin(i, [i](ProgVM& v) { Unimplemented(v, i); });
        }
    }
}

} // namespace zq::vm
