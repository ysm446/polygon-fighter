#pragma once

#include <array>
#include <memory>
#include "character/Humanoid.h"
#include "animation/Pose.h"
#include "animation/AnimationPlayer.h"
#include "combat/CombatSystem.h"
#include "physics/HitReaction.h"

namespace pf {
class PhysicsWorld {
public:
    static constexpr double FixedStep = 1.0 / 60.0;
    explicit PhysicsWorld(bool combatScene = false);
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;

    void Step();
    void ResetBox();
    void SetGravity(float gravity);
    void PushBox();
    void ResetHumanoid();
    void PushHumanoid();
    void SetJointTuning(const JointTuning& tuning);
    bool RequestPunch(std::size_t fighter = 0);
    void SetGuard(std::size_t fighter, bool held);
    void ResetMatch();
    [[nodiscard]] bool IsCombatScene() const;
    [[nodiscard]] const CombatSystem& Combat() const;
    [[nodiscard]] const CombatFrame& LastCombatFrame() const;
    void SetHitReactionTuning(const HitReactionTuning& tuning);
    [[nodiscard]] const std::array<HitReaction, 2>& HitReactions() const;
    void SetPlaybackSpeed(float speed);
    void SetAnimations(std::size_t fighter, std::shared_ptr<const CharacterAnimations> clips);
    [[nodiscard]] bool Punching() const;
    [[nodiscard]] double AnimationTime() const;
    [[nodiscard]] std::array<BodyPose, HumanoidPartCount> TargetPoses(std::size_t fighter = 0) const;
    [[nodiscard]] std::array<BodyPose, HumanoidPartCount> HumanoidPoses(std::size_t fighter = 0) const;
    [[nodiscard]] float HumanoidPoseError() const;
    [[nodiscard]] BodyPose BoxPose() const;
    [[nodiscard]] float BoxSpeed() const;
    [[nodiscard]] bool BoxActive() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
