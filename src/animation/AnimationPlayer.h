#pragma once
#include "animation/Pose.h"
#include "character/Attack.h"
#include <vector>
#include <memory>

namespace pf {
struct PoseKey { double time; Pose pose; };
struct AnimationClip {
    const char* name;
    bool looping;
    std::vector<PoseKey> keys;
    [[nodiscard]] Pose Sample(double time) const;
    [[nodiscard]] double Duration() const { return keys.back().time; }
};

struct CharacterAnimations {
    AnimationClip idle{"Idle",true,{}}, punch{"Punch",false,{}}, guard{"Guard",false,{}};
    AnimationClip walk{"Walk",true,{}}, kick{"Kick",false,{}};
};
class AnimationPlayer {
public:
    AnimationPlayer();
    bool RequestAttack(AttackKind kind);
    bool RequestPunch() { return RequestAttack(AttackKind::Punch); }
    void Advance(double elapsed);
    void Reset();
    void SetSpeed(float speed);
    void SetClips(std::shared_ptr<const CharacterAnimations> clips) { clips_ = std::move(clips); Reset(); }
    void SetGuard(bool guard) { guarding_ = guard; }
    void SetLocomotion(float speed) { movementSpeed_ = speed; }
    [[nodiscard]] bool Attacking() const { return attacking_; }
    [[nodiscard]] double Time() const { return time_; }
    [[nodiscard]] const Pose& Target() const { return target_; }
    [[nodiscard]] static const AnimationClip& IdleClip();
    [[nodiscard]] static const AnimationClip& PunchClip();
    [[nodiscard]] static const AnimationClip& WalkClip();
    [[nodiscard]] static const AnimationClip& KickClip();
    [[nodiscard]] AttackKind CurrentAttack() const { return attack_; }
private:
    std::shared_ptr<const CharacterAnimations> clips_;
    const AnimationClip& Idle() const { return clips_ ? clips_->idle : IdleClip(); }
    const AnimationClip& AttackClip() const {
        if (attack_ == AttackKind::Kick) return clips_ ? clips_->kick : KickClip();
        return clips_ ? clips_->punch : PunchClip();
    }
    AttackKind attack_ = AttackKind::Punch;
    bool attacking_ = false;
    bool guarding_ = false;
    float guardWeight_ = 0;
    double time_ = 0;
    float speed_ = 1;
    Pose target_{};
    float movementSpeed_ = 0, walkWeight_ = 0;
    double walkTime_ = 0;
};
}
