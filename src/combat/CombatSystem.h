#pragma once
#include "animation/Pose.h"
#include <array>
#include <cstddef>

namespace pf {
struct Sphere { std::array<float, 3> center{}; float radius = 0; };
using FighterPoses = std::array<std::array<BodyPose, HumanoidPartCount>, 2>;
struct AttackData {
    int startupFrames = 21;
    int activeFrames = 8;
    int recoveryFrames = 31;
    int damage = 12;
    int hitstun = 24;
    int guardDamage = 3;
    int guardStun = 8;
    float hitboxRadius = .18f;
};
enum class CombatState { Idle, Attacking, Guarding, Hitstun, KO };
struct FighterCombat {
    int hp = 100;
    int attackFrame = -1;
    int stunFrames = 0;
    bool guardRequested = false;
    bool connected = false;
    [[nodiscard]] CombatState State() const;
};
struct HitEvent {
    std::size_t attacker = 0, defender = 1, bone = 0;
    int damage = 0;
    bool guarded = false;
    std::array<float, 3> position{};
    std::array<float, 3> direction{};
};
struct CombatFrame {
    std::array<HitEvent, 2> hits{};
    std::size_t hitCount = 0;
    std::array<bool, 2> active{};
};
class CombatSystem {
public:
    static constexpr AttackData Punch{};
    bool RequestPunch(std::size_t fighter);
    void SetGuard(std::size_t fighter, bool held);
    void Reset();
    CombatFrame Step(const FighterPoses& poses);
    [[nodiscard]] const FighterCombat& Fighter(std::size_t fighter) const { return fighters_.at(fighter); }
    [[nodiscard]] bool HitboxActive(std::size_t fighter) const;
    [[nodiscard]] static Sphere PunchSphere(const std::array<BodyPose, HumanoidPartCount>& pose);
    [[nodiscard]] static std::array<Sphere, HumanoidPartCount> HurtSpheres(const std::array<BodyPose, HumanoidPartCount>& pose);
    [[nodiscard]] static bool Intersects(const Sphere& a, const Sphere& b);
private:
    std::array<FighterCombat, 2> fighters_{};
};
const char* StateName(CombatState state);
}
