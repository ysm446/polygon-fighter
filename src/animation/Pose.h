#pragma once
#include "character/Humanoid.h"

namespace pf {
struct BodyPose {
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
};
struct BoneTransform {
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 3> scale{1, 1, 1};
};
using Pose = std::array<BoneTransform, HumanoidPartCount>;
}
