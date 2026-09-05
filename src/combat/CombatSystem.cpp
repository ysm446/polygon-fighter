#include "combat/CombatSystem.h"
#include <algorithm>
#include <cmath>

namespace pf {
CombatState FighterCombat::State() const {
    if (hp <= 0) return CombatState::KO;
    if (stunFrames > 0) return CombatState::Hitstun;
    if (attackFrame >= 0) return CombatState::Attacking;
    return guardRequested ? CombatState::Guarding : CombatState::Idle;
}
const char* StateName(CombatState state) {
    switch (state) {
    case CombatState::Idle: return "Idle";
    case CombatState::Attacking: return "Attacking";
    case CombatState::Guarding: return "Guarding";
    case CombatState::Hitstun: return "Hitstun";
    case CombatState::KO: return "KO";
    }
    return "Unknown";
}
bool CombatSystem::RequestAttack(std::size_t fighter, AttackKind kind) {
    auto& state = fighters_.at(fighter);
    if (state.State() != CombatState::Idle) return false;
    state.attackFrame = 0;
    state.connected = false;
    state.attack = kind;
    return true;
}
void CombatSystem::SetGuard(std::size_t fighter, bool held) { fighters_.at(fighter).guardRequested = held; }
void CombatSystem::Reset() { fighters_ = {}; }
bool CombatSystem::HitboxActive(std::size_t fighter) const {
    const auto& state = fighters_.at(fighter);
    const auto& data = Data(state.attack);
    return state.State() == CombatState::Attacking && state.attackFrame >= data.startupFrames
        && state.attackFrame < data.startupFrames + data.activeFrames;
}
bool CombatSystem::Intersects(const Sphere& a, const Sphere& b) {
    float distanceSquared = 0;
    for (std::size_t i = 0; i < 3; ++i) { const float d = a.center[i] - b.center[i]; distanceSquared += d * d; }
    const float radius = a.radius + b.radius;
    return distanceSquared <= radius * radius;
}
Sphere CombatSystem::PunchSphere(const std::array<BodyPose, HumanoidPartCount>& pose) {
    return AttackSphere(pose,AttackKind::Punch);
}
Sphere CombatSystem::AttackSphere(const std::array<BodyPose, HumanoidPartCount>& pose, AttackKind kind) {
    const std::size_t bone = kind == AttackKind::Kick ? 10 : 6;
    const auto& part = pose[bone];
    const auto& q = part.rotation;
    // 前腕または下腿のローカル-Y端点を回転する。物理エンジンには依存しない。
    const float length = HumanoidParts[bone].halfExtent[1];
    const std::array<float, 3> offset{
        -length * 2 * (q[0] * q[1] - q[2] * q[3]),
        -length * (1 - 2 * (q[0] * q[0] + q[2] * q[2])),
        -length * 2 * (q[1] * q[2] + q[0] * q[3])};
    Sphere result{part.position, Data(kind).hitboxRadius};
    for (std::size_t i = 0; i < 3; ++i) result.center[i] += offset[i];
    return result;
}
std::array<Sphere, HumanoidPartCount> CombatSystem::HurtSpheres(const std::array<BodyPose, HumanoidPartCount>& pose) {
    std::array<Sphere, HumanoidPartCount> spheres;
    for (std::size_t i = 0; i < spheres.size(); ++i)
        spheres[i] = {pose[i].position, i == 1 ? .36f : i == 0 ? .26f : i == 2 ? .20f : .14f};
    return spheres;
}
CombatFrame CombatSystem::Step(const FighterPoses& poses) {
    CombatFrame frame;
    for (std::size_t i = 0; i < fighters_.size(); ++i) frame.active[i] = HitboxActive(i);
    // 両者の命中を先に収集し、配列順による相打ちの不公平を避ける。
    for (std::size_t attacker = 0; attacker < fighters_.size(); ++attacker) {
        const std::size_t defender = 1 - attacker;
        if (!HitboxActive(attacker) || fighters_[attacker].connected || fighters_[defender].hp == 0) continue;
        const auto kind = fighters_[attacker].attack;
        const auto& data = Data(kind);
        const auto hitbox = AttackSphere(poses[attacker],kind);
        const auto hurtboxes = HurtSpheres(poses[defender]);
        for (std::size_t bone = 0; bone < hurtboxes.size(); ++bone) {
            if (!Intersects(hitbox, hurtboxes[bone])) continue;
            const bool guard = fighters_[defender].State() == CombatState::Guarding;
            frame.hits[frame.hitCount++] = {attacker, defender, bone, guard ? data.guardDamage : data.damage, guard};
            auto& hit = frame.hits[frame.hitCount - 1];
            hit.attack = kind;
            // 水平の攻撃方向と、被攻撃球の拳側の表面を命中位置として記録する。
            float distance = 0, separation = 0;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                hit.position[axis] = hitbox.center[axis] - hurtboxes[bone].center[axis];
                distance += hit.position[axis] * hit.position[axis];
                hit.direction[axis] = axis == 1 ? 0 : poses[defender][0].position[axis] - poses[attacker][0].position[axis];
                separation += hit.direction[axis] * hit.direction[axis];
            }
            distance = std::sqrt(distance);
            separation = std::sqrt(separation);
            if (separation < 1e-5f) hit.direction = {0, 0, -1};
            else for (auto& value : hit.direction) value /= separation;
            for (std::size_t axis = 0; axis < 3; ++axis)
                hit.position[axis] = hurtboxes[bone].center[axis] + hurtboxes[bone].radius
                    * (distance < 1e-5f ? -hit.direction[axis] : hit.position[axis] / distance);
            break;
        }
    }
    for (auto& state : fighters_) {
        const auto& data = Data(state.attack);
        if (state.stunFrames > 0) --state.stunFrames;
        if (state.attackFrame >= 0 && ++state.attackFrame >= data.startupFrames + data.activeFrames + data.recoveryFrames)
            state.attackFrame = -1;
    }
    for (std::size_t i = 0; i < frame.hitCount; ++i) {
        const auto& hit = frame.hits[i];
        auto& target = fighters_[hit.defender];
        fighters_[hit.attacker].connected = true;
        target.hp = std::max(0, target.hp - hit.damage);
        const auto& data = Data(hit.attack);
        target.stunFrames = target.hp == 0 ? 0 : hit.guarded ? data.guardStun : data.hitstun;
        target.attackFrame = -1;
    }
    return frame;
}
}
