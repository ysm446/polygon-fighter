#include "physics/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void CheckBodies(const pf::PhysicsWorld& world) {
    for (const auto& pose : world.HumanoidPoses()) {
        for (float p : pose.position) Require(std::isfinite(p) && std::abs(p) < 5, "Body position diverged");
        float norm = 0;
        for (float q : pose.rotation) { Require(std::isfinite(q), "Non-finite rotation"); norm += q * q; }
        Require(std::abs(norm - 1) < .001f, "Rotation lost normalization");
        Require(pose.position[1] > .15f, "Humanoid part fell through floor");
    }
}
float Disturb(pf::PhysicsWorld& world, pf::JointTuning tuning) {
    world.ResetHumanoid();
    world.SetJointTuning(tuning);
    world.PushHumanoid();
    float peak = 0;
    for (int i = 0; i < 120; ++i) {
        world.Step();
        peak = std::max(peak, world.HumanoidPoseError());
        CheckBodies(world);
    }
    return peak;
}
}
int main() {
    try {
        pf::PhysicsWorld world;
        for (int i = 0; i < 1800; ++i) { world.Step(); CheckBodies(world); }
        Require(world.HumanoidPoseError() < 2, "Standing target drifted");
        Require(world.HumanoidPoses()[2].position[1] > 1.75f, "Head did not remain upright");
        const float strong = Disturb(world, {});
        Require(strong > 1, "Impulse must cause a physical reaction");
        for (int i = 0; i < 300; ++i) world.Step();
        const float recovered = world.HumanoidPoseError();
        Require(recovered < 2, "Powered joints must recover to target");
        auto weak = pf::JointTuning{};
        weak.stiffness = 60;
        weak.damping = 4;
        const float soft = Disturb(world, weak);
        Require(soft > strong * 1.2f, "Stiffness and damping tuning must affect reaction");
        auto off = pf::JointTuning{};
        off.enabled = false;
        const float unpowered = Disturb(world, off);
        Require(unpowered > strong * 1.2f, "Disabled motors must permit larger displacement");
        world.SetJointTuning({});
        for (int i = 0; i < 600; ++i) { world.Step(); CheckBodies(world); }
        Require(world.HumanoidPoseError() < 2, "Re-enabled motors must recover");
        world.ResetHumanoid();
        const auto reset = world.HumanoidPoses();
        for (std::size_t i = 0; i < reset.size(); ++i)
            for (int axis = 0; axis < 3; ++axis)
                Require(std::abs(reset[i].position[axis] - pf::HumanoidParts[i].position[axis]) < .001f, "Reset pose mismatch");
        for (int i = 0; i < 120; ++i) world.Step();
        Require(world.HumanoidPoseError() < 2, "Reset left residual motion");
        std::cout << "PASS: 30s standing, finite transforms, impulse recovery, tuning, motors off/on, reset\n"
            << "strong_peak_deg=" << strong << " soft_peak_deg=" << soft << " motors_off_peak_deg=" << unpowered
            << " recovered_deg=" << recovered << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
