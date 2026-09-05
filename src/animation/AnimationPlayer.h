#pragma once
#include "animation/Pose.h"
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
};
class AnimationPlayer {
public:
    AnimationPlayer();
    bool RequestPunch();
    void Advance(double elapsed);
    void Reset();
    void SetSpeed(float speed);
    void SetClips(std::shared_ptr<const CharacterAnimations> clips) { clips_ = std::move(clips); Reset(); }
    void SetGuard(bool guard) { guarding_ = guard; }
    [[nodiscard]] bool Punching() const { return punching_; }
    [[nodiscard]] double Time() const { return time_; }
    [[nodiscard]] const Pose& Target() const { return target_; }
    [[nodiscard]] static const AnimationClip& IdleClip();
    [[nodiscard]] static const AnimationClip& PunchClip();
private:
    std::shared_ptr<const CharacterAnimations> clips_;
    const AnimationClip& Idle() const { return clips_ ? clips_->idle : IdleClip(); }
    const AnimationClip& Punch() const { return clips_ ? clips_->punch : PunchClip(); }
    bool punching_ = false;
    bool guarding_ = false;
    float guardWeight_ = 0;
    double time_ = 0;
    float speed_ = 1;
    Pose target_{};
};
}
