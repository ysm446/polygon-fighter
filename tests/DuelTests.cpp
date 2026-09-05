#include "physics/PhysicsWorld.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void Advance(pf::PhysicsWorld& world, int count) {
    for (int i = 0; i < count; ++i) {
        world.Step();
        for (std::size_t fighter = 0; fighter < 2; ++fighter)
            for (const auto& part : world.HumanoidPoses(fighter))
                for (float p : part.position) Require(std::isfinite(p) && std::abs(p) < 5, "Duel physics diverged");
    }
}
struct ReactionResult { float peak = 0, finalError = 0, impulse = 0; };
ReactionResult MeasureReaction(pf::PhysicsWorld& world, bool guard, float strength, std::size_t attacker = 0) {
    world.ResetMatch();
    world.SetHitReactionTuning({strength,.25f});
    const auto defender = 1 - attacker;
    world.SetGuard(defender,guard);
    Advance(world,120);
    const auto rest = world.HumanoidPoses(defender)[2].position;
    Require(world.RequestPunch(attacker), "Reaction test punch rejected");
    ReactionResult result;
    int hits = 0;
    for (int frame = 0; frame < 300; ++frame) {
        Advance(world,1);
        if (world.LastCombatFrame().hitCount) {
            ++hits;
            const auto& reaction = world.HitReactions()[defender];
            for (float value : reaction.impulse) result.impulse += value * value;
            result.impulse = std::sqrt(result.impulse);
            Require(reaction.hit.bone < pf::HumanoidPartCount, "Invalid hit part");
            Require(attacker == 0 ? reaction.impulse[2] < 0 || strength == 0 : reaction.impulse[2] > 0 || strength == 0,
                "Impulse must follow attack direction");
            for (float value : reaction.hit.position) Require(std::isfinite(value), "Invalid hit position");
        }
        const auto position = world.HumanoidPoses(defender)[2].position;
        float error = 0;
        for (std::size_t axis = 0; axis < 3; ++axis) error += (position[axis]-rest[axis])*(position[axis]-rest[axis]);
        result.finalError = std::sqrt(error);
        result.peak = std::max(result.peak,result.finalError);
    }
    Require(hits == 1, "Reaction must occur once per attack");
    Require(world.HitReactions()[defender].visibleFrames == 0, "Impact marker must expire");
    Require(result.finalError < .02f, "Defender must recover without teleporting");
    return result;
}
}
int main() {
    try {
        pf::PhysicsWorld world(true);
        const auto baseline = MeasureReaction(world,false,0);
        const auto guardBaseline = MeasureReaction(world,true,0);
        const auto normal = MeasureReaction(world,false,20);
        const auto guard = MeasureReaction(world,true,20);
        const auto reverse = MeasureReaction(world,false,20,1);
        const auto maximum = MeasureReaction(world,false,40);
        Require(maximum.peak > normal.peak, "Impulse slider must change physical response");
        bool invalidRejected = false;
        try { world.SetHitReactionTuning({std::numeric_limits<float>::quiet_NaN(),.25f}); }
        catch (const std::invalid_argument&) { invalidRejected = true; }
        Require(invalidRejected, "Nonfinite impulse must be rejected");
        world.SetHitReactionTuning({});
        std::cout << "head peak: baseline=" << baseline.peak << " normal=" << normal.peak
            << " guard=" << guard.peak << " guard baseline=" << guardBaseline.peak << " reverse=" << reverse.peak << '\n';
        Require(normal.peak > baseline.peak + .01f, "Hit impulse must add visible motion");
        Require(guard.peak - guardBaseline.peak < normal.peak - baseline.peak, "Guard must reduce added physical reaction");
        Require(std::abs(normal.impulse - 20) < .001f && std::abs(guard.impulse - 5) < .001f,
            "Guard impulse scaling incorrect");
        world.ResetMatch();
        Require(world.HitReactions()[0].visibleFrames == 0 && world.HitReactions()[1].visibleFrames == 0,
            "Reset must clear hit markers");
        Advance(world, 180);
        Require(world.Combat().Fighter(0).hp == 100 && world.Combat().Fighter(1).hp == 100, "Passive physics must not cause damage");
        world.RequestPunch();
        Advance(world, 120);
        Require(world.Combat().Fighter(1).hp == 88, "Physical P1 punch must hit P2 once");
        world.ResetMatch();
        world.SetGuard(1, true);
        Advance(world, 60);
        world.RequestPunch();
        Advance(world, 120);
        Require(world.Combat().Fighter(1).hp == 97, "Physical guard must reduce damage");
        world.ResetMatch();
        world.RequestPunch(1);
        Advance(world, 120);
        Require(world.Combat().Fighter(0).hp == 88, "Rotated P2 punch must hit P1 once");
        world.ResetMatch();
        auto off = pf::JointTuning{};
        off.enabled = false;
        world.SetJointTuning(off);
        world.RequestPunch();
        Advance(world, 120);
        Require(world.Combat().Fighter(1).hp == 100, "An unmoving physical fist must not hit from target pose alone");
        world.SetJointTuning({});
        world.ResetMatch();
        for (int attack = 0; attack < 9; ++attack) {
            Require(world.RequestPunch(), "Repeated physical attack failed");
            Advance(world, 120);
        }
        Require(world.Combat().Fighter(1).hp == 0 && !world.RequestPunch(1), "Physical match must reach KO");
        world.ResetMatch();
        Advance(world, 120);
        Require(world.Combat().Fighter(1).hp == 100 && world.HumanoidPoses(1)[2].position[1] > 1.7f,
            "Reset after KO must restore health and standing pose");
        std::cout << "PASS: two rigs, P1 hit, P2 hit, guard, motors OFF miss, KO, reset\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
