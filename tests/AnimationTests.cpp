#include "animation/AnimationPlayer.h"
#include "physics/PhysicsWorld.h"
#include "app/FixedClock.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Near(float actual, float expected, float tolerance, const char* message) { Require(std::abs(actual - expected) < tolerance, message); }
float RunPunch(pf::PhysicsWorld& world, bool powered) {
    world.ResetHumanoid();
    auto tuning = pf::JointTuning{};
    tuning.enabled = powered;
    world.SetJointTuning(tuning);
    world.SetPlaybackSpeed(1);
    const auto before = world.HumanoidPoses();
    Require(world.RequestPunch(), "Idle must accept punch");
    Require(!world.RequestPunch(), "Repeated input must not restart an active punch");
    const auto requested = world.HumanoidPoses();
    Require(before[6].position == requested[6].position && before[6].rotation == requested[6].rotation,
        "Request must not teleport a physics body");
    float reach = 0;
    float targetReach = 0;
    for (int step = 0; step < 60; ++step) {
        world.Step();
        const auto actual = world.HumanoidPoses();
        for (const auto& part : actual)
            for (float p : part.position) Require(std::isfinite(p) && std::abs(p) < 5, "Punch destabilized rig");
        reach = std::max(reach, -actual[6].position[2]);
        targetReach = std::max(targetReach, -world.TargetPoses()[6].position[2]);
    }
    Require(!world.Punching(), "Punch must return to Idle after 60 steps");
    Require(targetReach > .55f, "Target forearm must extend forward");
    return reach;
}
}
int main() {
    try {
        pf::AnimationPlayer player;
        player.Advance(6.25);
        Near(static_cast<float>(player.Time()), .25f, .0001f, "Idle loop time is incorrect");
        Require(player.RequestPunch(), "Punch request failed");
        player.SetSpeed(2);
        player.Advance(.25);
        Require(player.Punching(), "Double-speed punch ended too soon");
        player.Advance(.25);
        Require(!player.Punching(), "Double-speed punch must finish at 0.5 seconds");
        const auto midpoint = pf::AnimationPlayer::PunchClip().Sample(.25);
        Near(midpoint[5].rotation[0], std::sin(55.0f * 3.14159265f / 360), .0001f, "Rotation interpolation is incorrect");
        pf::Pose a{}, b{};
        a[1].rotation = {0,0,0,-1};
        b[1].translation = {2,4,6};
        b[1].scale = {3,3,3};
        const pf::AnimationClip sample{"Test", false, {{0,a},{1,b}}};
        const auto blended = sample.Sample(.5);
        Near(std::abs(blended[1].rotation[3]), 1, .0001f, "Equivalent quaternion signs must use shortest arc");
        Near(blended[1].translation[2], 3, .0001f, "Translation interpolation failed");
        Near(blended[1].scale[0], 2, .0001f, "Scale interpolation failed");

        pf::PhysicsWorld world;
        const float poweredReach = RunPunch(world, true);
        Require(poweredReach > .45f, "Powered forearm must physically extend forward");
        for (int i = 0; i < 240; ++i) world.Step();
        Require(world.HumanoidPoseError() < 2, "Arm must recover to Idle");
        const float unpoweredReach = RunPunch(world, false);
        Require(unpoweredReach < .05f, "Motors OFF must not follow animation by setting transforms");
        world.SetJointTuning({});
        world.ResetHumanoid();
        world.RequestPunch();
        for (int i = 0; i < 20; ++i) world.Step();
        world.ResetHumanoid();
        Require(!world.Punching(), "Reset must cancel punch");
        Near(world.TargetPoses()[6].position[2], 0, .0001f, "Reset must clear punch target");

        float positions[3]{};
        constexpr int rates[] = {30,60,144};
        for (int r = 0; r < 3; ++r) {
            world.ResetHumanoid();
            world.RequestPunch();
            pf::FixedClock clock;
            for (int frame = 0; frame < rates[r] / 2; ++frame)
                clock.Advance(1.0 / rates[r], false, false, [&] { world.Step(); });
            positions[r] = world.HumanoidPoses()[6].position[2];
        }
        Near(positions[0], positions[1], .001f, "30/60 Hz changed physical punch");
        Near(positions[0], positions[2], .001f, "30/144 Hz changed physical punch");
        std::cout << "PASS: clip interpolation, looping, speed, punch, motors OFF, recovery, reset, render cadence\n"
            << "powered_forearm_reach_m=" << poweredReach << " unpowered_reach_m=" << unpoweredReach << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
