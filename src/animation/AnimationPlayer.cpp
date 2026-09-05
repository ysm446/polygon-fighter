#include "animation/AnimationPlayer.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pf {
namespace {
using namespace DirectX;
XMVECTOR Load(const std::array<float, 4>& q) { return XMVectorSet(q[0],q[1],q[2],q[3]); }
std::array<float, 4> Store(FXMVECTOR q) {
    XMFLOAT4 result;
    XMStoreFloat4(&result, q);
    return {result.x,result.y,result.z,result.w};
}
Pose ArmPose(float shoulder, float elbow) {
    Pose pose{};
    pose[5].rotation = Store(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0), XMConvertToRadians(shoulder)));
    pose[6].rotation = Store(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0), XMConvertToRadians(elbow)));
    return pose;
}
}
Pose AnimationClip::Sample(double time) const {
    if (keys.size() < 2 || !std::isfinite(time)) throw std::invalid_argument("Invalid animation sample.");
    time = looping ? std::fmod(std::max(0.0, time), Duration()) : std::clamp(time, 0.0, Duration());
    if (time >= Duration()) return keys.back().pose;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (time > keys[i].time) continue;
        const auto& a = keys[i - 1];
        const auto& b = keys[i];
        const float weight = static_cast<float>((time - a.time) / (b.time - a.time));
        Pose pose{};
        for (std::size_t bone = 0; bone < pose.size(); ++bone) {
            pose[bone].rotation = Store(XMQuaternionNormalize(XMQuaternionSlerp(Load(a.pose[bone].rotation), Load(b.pose[bone].rotation), weight)));
            for (std::size_t axis = 0; axis < 3; ++axis) {
                pose[bone].translation[axis] = std::lerp(a.pose[bone].translation[axis], b.pose[bone].translation[axis], weight);
                pose[bone].scale[axis] = std::lerp(a.pose[bone].scale[axis], b.pose[bone].scale[axis], weight);
            }
        }
        return pose;
    }
    return keys.back().pose;
}
const AnimationClip& AnimationPlayer::IdleClip() {
    static const AnimationClip clip{"Idle", true, {{0, {}}, {2.0, {}}}};
    return clip;
}
const AnimationClip& AnimationPlayer::PunchClip() {
    // 予備動作→伸展→短い保持→復帰。正面は-Z方向。
    static const AnimationClip clip{"Punch", false, {
        {0, {}}, {.15, ArmPose(20,75)}, {.35, ArmPose(90,0)},
        {.48, ArmPose(90,0)}, {.85, {}}, {1.0, {}}
    }};
    return clip;
}
AnimationPlayer::AnimationPlayer() { Reset(); }
bool AnimationPlayer::RequestPunch() {
    if (punching_) return false;
    punching_ = true;
    time_ = 0;
    target_ = Punch().Sample(0);
    return true;
}
void AnimationPlayer::Advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0) throw std::invalid_argument("Invalid animation elapsed time.");
    time_ += elapsed * speed_;
    if (punching_ && time_ + 1e-12 >= Punch().Duration()) {
        time_ = std::max(0.0, time_ - Punch().Duration());
        punching_ = false;
    }
    if (!punching_) time_ = std::fmod(time_, Idle().Duration());
    target_ = (punching_ ? Punch() : Idle()).Sample(time_);
    const float change = static_cast<float>(elapsed / .15);
    guardWeight_ = std::clamp(guardWeight_ + (guarding_ && !punching_ ? change : -change), 0.0f, 1.0f);
    if (!punching_ && guardWeight_ > 0) {
        target_ = ArmPose(20 * guardWeight_, 75 * guardWeight_);
        target_[3].rotation = target_[5].rotation;
        target_[4].rotation = target_[6].rotation;
        if (clips_) target_ = clips_->guard.Sample(guardWeight_ * .15);
    }
}
void AnimationPlayer::Reset() { punching_ = false; guarding_ = false; guardWeight_ = 0; time_ = 0; target_ = Idle().Sample(0); }
void AnimationPlayer::SetSpeed(float speed) {
    if (!std::isfinite(speed) || speed < .25f || speed > 2.0f) throw std::invalid_argument("Playback speed must be between 0.25 and 2.");
    speed_ = speed;
}
}
