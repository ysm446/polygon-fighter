#pragma once
#include "combat/CombatSystem.h"

namespace pf {
struct HitReactionTuning {
    float impulse = 20.0f;
    float guardMultiplier = .25f;
};
struct HitReaction {
    HitEvent hit;
    std::array<float, 3> impulse{};
    int visibleFrames = 0;
};
}
