#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include "physics/PhysicsWorld.h"
#include "animation/AnimationPlayer.h"

namespace pf {
class PhysicsRig {
public:
    explicit PhysicsRig(JPH::PhysicsSystem& system, JPH::RVec3 offset = JPH::RVec3::sZero(), float yaw = 0, JPH::uint group = 1);
    ~PhysicsRig();
    PhysicsRig(const PhysicsRig&) = delete;
    PhysicsRig& operator=(const PhysicsRig&) = delete;
    void Reset();
    void Tune(const JointTuning& tuning);
    void Push();
    void ApplyHit(const HitReaction& reaction);
    void Wake();
    void Animate(double elapsed);
    bool RequestPunch();
    void SetGuard(bool held) { animation_.SetGuard(held); }
    void CancelAnimation() { animation_.Reset(); }
    void SetPlaybackSpeed(float speed) { animation_.SetSpeed(speed); }
    void SetClips(std::shared_ptr<const CharacterAnimations> clips) { animation_.SetClips(std::move(clips)); }
    [[nodiscard]] bool Punching() const { return animation_.Punching(); }
    [[nodiscard]] double AnimationTime() const { return animation_.Time(); }
    [[nodiscard]] std::array<BodyPose, HumanoidPartCount> TargetPoses() const;
    [[nodiscard]] std::array<BodyPose, HumanoidPartCount> Poses() const;
    [[nodiscard]] float PoseError() const;
private:
    JPH::PhysicsSystem& system_;
    std::array<JPH::BodyID, HumanoidPartCount> bodies_;
    std::array<JPH::Ref<JPH::SwingTwistConstraint>, HumanoidPartCount - 1> joints_;
    JPH::Ref<JPH::TwoBodyConstraint> anchor_;
    AnimationPlayer animation_;
    JPH::RVec3 offset_;
    JPH::Quat rotation_;
    [[nodiscard]] JPH::RVec3 SpawnPosition(const std::array<float, 3>& position) const;
};
}
