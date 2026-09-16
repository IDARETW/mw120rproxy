#pragma once

#include "iw8_focus_structs.h"
#include "iw8_zone.h"

#include <array>
#include <string>
#include <vector>

namespace iw8::vfx
{
struct Module
{
    struct Curve
    {
        std::vector<iw8_focus::ParticleCurveControlPointDef> points;
    };
    iw8_focus::ParticleModuleDef native{};
    std::vector<iw8_focus::ParticleCurveControlPointDef> spawnCurve;
    std::vector<Curve> curves;
    std::vector<std::string> models;
    std::vector<std::string> materials;
    std::vector<std::string> childEffects;
    std::vector<std::array<std::string, 3>> decalMaterials;
};

struct State
{
    iw8_focus::ParticleStateDef native{};
    std::array<std::vector<Module>, 3> groups;
};

struct Emitter
{
    iw8_focus::ParticleEmitterDef native{};
    std::vector<State> states;
    std::vector<iw8_focus::ParticleCurveControlPointDef> fadeCurve;
};

struct Effect
{
    std::string name;
    iw8_focus::ParticleSystemDef native{};
    std::vector<Emitter> emitters;
};

// Source graph creation is separate. The writer accepts only selector payloads
// whose Replay 1.20 pointer layout is pinned in iw8_focus_structs.h.
void Register(ZoneWriter &writer, Effect effect);
} // namespace iw8::vfx
