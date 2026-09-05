#pragma once
#include "combat/CombatSystem.h"
#include "physics/HitReaction.h"

namespace pf {
struct RenderScene {
    BodyPose box;
    FighterPoses bodies{};
    FighterPoses targets{};
    std::size_t fighterCount = 1;
    std::array<bool, 2> hitboxActive{};
    std::array<HitReaction, 2> reactions{};
};
}
