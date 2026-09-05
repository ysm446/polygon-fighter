#include "physics/PhysicsWorld.h"
#include "app/FixedClock.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
}

int main() {
    try {
        pf::PhysicsWorld world;
        for (int i = 0; i < 30; ++i) world.Step();
        Require(world.BoxPose().position[1] < 3.0f, "Box must fall under gravity");
        for (int i = 0; i < 570; ++i) {
            world.Step();
            Require(world.BoxPose().position[1] > 0.40f, "Box tunneled through ground");
        }
        Require(std::abs(world.BoxPose().position[1] - 0.5f) < 0.04f, "Box must rest on floor");
        Require(world.BoxSpeed() < 0.05f, "Box must settle");
        world.PushBox();
        world.Step();
        Require(world.BoxSpeed() > 1.0f, "Impulse must wake and move box");
        world.ResetBox();
        Require(std::abs(world.BoxPose().position[1] - 4.0f) < 0.001f, "Reset must restore position");
        Require(world.BoxSpeed() < 0.001f, "Reset must clear velocity");
        world.SetGravity(0);
        for (int i = 0; i < 120; ++i) world.Step();
        Require(std::abs(world.BoxPose().position[1] - 4.0f) < 0.001f, "Zero gravity must preserve height");
        world.SetGravity(-9.81f);

        float heights[3]{};
        constexpr int rates[] = {30, 60, 144};
        for (int r = 0; r < 3; ++r) {
            world.ResetBox();
            pf::FixedClock clock;
            int steps = 0;
            for (int frame = 0; frame < rates[r] / 2; ++frame)
                steps += clock.Advance(1.0 / rates[r], false, false, [&] { world.Step(); });
            Require(steps == 30, "Render cadence must not change physics step count");
            heights[r] = world.BoxPose().position[1];
        }
        Require(std::abs(heights[0] - heights[1]) < 1e-5f && std::abs(heights[0] - heights[2]) < 1e-5f,
            "Render cadence must not change simulated trajectory");
        pf::FixedClock clock;
        int calls = 0;
        clock.Advance(1, true, false, [&] { ++calls; });
        Require(calls == 0, "Paused clock must not simulate");
        clock.Advance(1, true, true, [&] { ++calls; });
        Require(calls == 1, "Single step must advance exactly once");
        Require(clock.Advance(10, false, false, [] {}) == 15, "Long stall must be bounded");
        std::cout << "PASS: fall, floor collision, settle, impulse, reset, gravity, 30/60/144 Hz, pause, step, stall\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
