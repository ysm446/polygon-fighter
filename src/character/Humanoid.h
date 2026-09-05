#pragma once
#include <array>
#include <cstddef>

namespace pf {
struct PartDefinition {
    const char* name;
    int parent;
    std::array<float, 3> position;
    std::array<float, 3> halfExtent;
    std::array<float, 3> pivot;
    float mass;
};

// Y-upの立位。姿勢は親に対する単位Quaternionを目標とする。
inline constexpr std::array<PartDefinition, 11> HumanoidParts{{
    {"Pelvis", -1, {0.9f,1.02f,0}, {.22f,.13f,.14f}, {0,0,0}, 12},
    {"Torso", 0, {0.9f,1.40f,0}, {.26f,.23f,.15f}, {.9f,1.16f,0}, 18},
    {"Head", 1, {0.9f,1.83f,0}, {.14f,.16f,.14f}, {.9f,1.66f,0}, 5},
    {"LeftUpperArm", 1, {.50f,1.40f,0}, {.095f,.18f,.10f}, {.50f,1.60f,0}, 3},
    {"LeftLowerArm", 3, {.50f,1.01f,0}, {.08f,.18f,.09f}, {.50f,1.205f,0}, 2},
    {"RightUpperArm", 1, {1.30f,1.40f,0}, {.095f,.18f,.10f}, {1.30f,1.60f,0}, 3},
    {"RightLowerArm", 5, {1.30f,1.01f,0}, {.08f,.18f,.09f}, {1.30f,1.205f,0}, 2},
    {"LeftUpperLeg", 0, {.76f,.68f,0}, {.10f,.20f,.12f}, {.76f,.89f,0}, 7},
    {"LeftLowerLeg", 7, {.76f,.235f,0}, {.09f,.235f,.11f}, {.76f,.475f,0}, 4},
    {"RightUpperLeg", 0, {1.04f,.68f,0}, {.10f,.20f,.12f}, {1.04f,.89f,0}, 7},
    {"RightLowerLeg", 9, {1.04f,.235f,0}, {.09f,.235f,.11f}, {1.04f,.475f,0}, 4}
}};
inline constexpr std::size_t HumanoidPartCount = HumanoidParts.size();

struct JointTuning {
    float stiffness = 600;
    float damping = 40;
    float maxTorque = 120;
    bool enabled = true;
};
}
