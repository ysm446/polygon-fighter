#pragma once
#include <cstdint>
#include "character/Humanoid.h"
#include "render/DebugView.h"
#include "physics/HitReaction.h"

namespace pf {
class PhysicsWorld;
struct DebugState {
    bool paused = false;
    bool singleStep = false;
    bool reset = false;
    float gravity = -9.81f;
    JointTuning joints;
    HitReactionTuning hitReaction;
    float playbackSpeed = 1;
    DebugView view;
    std::array<bool, 2> guard{};
    std::uint64_t steps = 0;
};
void DrawDebugUI(PhysicsWorld& physics, DebugState& state, const char* adapter, bool debugLayer);
}
