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
const AnimationClip& AnimationPlayer::KickClip() {
    static const AnimationClip clip=[] {
        AnimationClip result{"Kick",false,{}};
        const std::array<std::array<double,4>,8> keys{{
            {0,0,0,0},{.15,-10,-20,4},{.32,65,-95,-5},{.50,100,-5,-8},
            {.57,100,-5,-8},{.72,65,-85,-4},{1.05,-5,-8,2},{1.3,0,0,0}}};
        for (const auto& key:keys) {
            Pose pose{};
            const float guard=static_cast<float>(std::min({key[0]/.15,1.,(1.3-key[0])/.25}));
            for (std::size_t bone=1;bone<HumanoidPartCount;++bone) {
                float angle=0;
                if (bone==1) angle=static_cast<float>(key[3]);
                if (bone==2) angle=static_cast<float>(-key[3]*.35);
                if (bone>=3 && bone<=6) angle=(bone%2 ? 20.f : 65.f)*guard;
                if (bone==9 || bone==10) angle=static_cast<float>(key[bone==9 ? 1 : 2]);
                pose[bone].rotation=Store(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),XMConvertToRadians(angle)));
            }
            result.keys.push_back({key[0],pose});
        }
        return result;
    }();
    return clip;
}
const AnimationClip& AnimationPlayer::WalkClip() {
    static const AnimationClip clip=[] {
        AnimationClip result{"Walk",true,{}};
        for (int frame=0;frame<=48;++frame) {
            const float wave=std::sin(frame/48.f*XM_2PI);
            Pose pose;
            for (std::size_t side=0;side<2;++side) {
                const float phase=side==0 ? wave : -wave;
                pose[7+side*2].rotation=Store(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),XMConvertToRadians(18*phase)));
                pose[8+side*2].rotation=Store(XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),XMConvertToRadians(-24*std::max(0.f,phase))));
            }
            result.keys.push_back({frame/60.,pose});
        }
        return result;
    }();
    return clip;
}
bool AnimationPlayer::RequestAttack(AttackKind kind) {
    if (attacking_) return false;
    attacking_ = true;
    attack_ = kind;
    time_ = 0;
    target_ = AttackClip().Sample(0);
    return true;
}
void AnimationPlayer::Advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0) throw std::invalid_argument("Invalid animation elapsed time.");
    time_ += elapsed * speed_;
    if (attacking_ && time_ + 1e-12 >= AttackClip().Duration()) {
        time_ = std::max(0.0, time_ - AttackClip().Duration());
        attacking_ = false;
    }
    if (!attacking_) time_ = std::fmod(time_, Idle().Duration());
    target_ = (attacking_ ? AttackClip() : Idle()).Sample(time_);
    const float change = static_cast<float>(elapsed / .15);
    guardWeight_ = std::clamp(guardWeight_ + (guarding_ && !attacking_ ? change : -change), 0.0f, 1.0f);
    if (!attacking_ && guardWeight_ > 0) {
        target_ = ArmPose(20 * guardWeight_, 75 * guardWeight_);
        target_[3].rotation = target_[5].rotation;
        target_[4].rotation = target_[6].rotation;
        if (clips_) target_ = clips_->guard.Sample(guardWeight_ * .15);
    }
    const float desired=!attacking_ && !guarding_ ? std::clamp(std::abs(movementSpeed_)/.85f,0.f,1.f) : 0;
    walkWeight_+=std::clamp(desired-walkWeight_,-change,change);
    const auto& walk=clips_ && !clips_->walk.keys.empty() ? clips_->walk : WalkClip();
    walkTime_=std::fmod(walkTime_+elapsed*movementSpeed_/.85+walk.Duration(),walk.Duration());
    if (!attacking_ && !guarding_ && walkWeight_>0) {
        const auto pose=walk.Sample(walkTime_);
        for (std::size_t bone=7;bone<HumanoidPartCount;++bone)
            target_[bone].rotation=Store(XMQuaternionSlerp(Load(target_[bone].rotation),Load(pose[bone].rotation),walkWeight_));
    }
}
void AnimationPlayer::Reset() { attacking_ = false; guarding_ = false; guardWeight_ = 0; time_ = 0; movementSpeed_=0; walkWeight_=0; walkTime_=0; target_ = Idle().Sample(0); }
void AnimationPlayer::SetSpeed(float speed) {
    if (!std::isfinite(speed) || speed < .25f || speed > 2.0f) throw std::invalid_argument("Playback speed must be between 0.25 and 2.");
    speed_ = speed;
}
}
