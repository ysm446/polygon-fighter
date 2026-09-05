#include "physics/PhysicsRig.h"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pf {
namespace {
JPH::RVec3 Position(const std::array<float, 3>& value) { return JPH::RVec3(value[0], value[1], value[2]); }
}
JPH::RVec3 PhysicsRig::SpawnPosition(const std::array<float, 3>& position) const {
    const auto root = Position(HumanoidParts[0].position);
    return root + offset_ + JPH::RVec3(0,0,movementZ_) + rotation_ * (Position(position) - root);
}
PhysicsRig::PhysicsRig(JPH::PhysicsSystem& system, JPH::RVec3 offset, float yaw, JPH::uint group, bool movable)
    : system_(system), offset_(offset), rotation_(JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw)), movable_(movable) {
    auto& api = system_.GetBodyInterface();
    JPH::Ref<JPH::GroupFilterTable> filter = new JPH::GroupFilterTable(static_cast<JPH::uint>(HumanoidPartCount));
    std::array<JPH::Body*, HumanoidPartCount> bodies{};
    // 関節で直接つながる部位だけ衝突を除外する。
    for (std::size_t i = 1; i < HumanoidPartCount; ++i)
        filter->DisableCollision(static_cast<JPH::uint>(i), static_cast<JPH::uint>(HumanoidParts[i].parent));
    try {
        for (std::size_t i = 0; i < HumanoidPartCount; ++i) {
            const auto& part = HumanoidParts[i];
            JPH::RefConst<JPH::Shape> shape = new JPH::BoxShape(JPH::Vec3(part.halfExtent[0], part.halfExtent[1], part.halfExtent[2]), 0.01f);
            JPH::BodyCreationSettings settings(shape, SpawnPosition(part.position), rotation_, JPH::EMotionType::Dynamic, 1);
            if (movable_ && i==0) settings.mMotionType=JPH::EMotionType::Kinematic;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = part.mass;
            settings.mCollisionGroup = JPH::CollisionGroup(filter, group, static_cast<JPH::uint>(i));
            // 支持点で移動する脚は無摩擦とし、床に引っ掛からず停止後に立位へ戻す。
            settings.mFriction = movable_ && i >= 7 ? 0.0f : 0.7f;
            settings.mAllowSleeping = false;
            bodies[i] = api.CreateBody(settings);
            if (!bodies[i]) throw std::runtime_error("Could not create humanoid part.");
            bodies_[i] = bodies[i]->GetID();
            api.AddBody(bodies_[i], JPH::EActivation::Activate);
        }
        if (!movable_) {
            JPH::FixedConstraintSettings support;
            support.mAutoDetectPoint = true;
            anchor_ = support.Create(JPH::Body::sFixedToWorld, *bodies[0]);
            system_.AddConstraint(anchor_);
        }
        for (std::size_t i = 1; i < HumanoidPartCount; ++i) {
            JPH::SwingTwistConstraintSettings settings;
            settings.mPosition1 = settings.mPosition2 = SpawnPosition(HumanoidParts[i].pivot);
            settings.mTwistAxis1 = settings.mTwistAxis2 = rotation_ * JPH::Vec3::sAxisY();
            settings.mPlaneAxis1 = settings.mPlaneAxis2 = rotation_ * JPH::Vec3::sAxisX();
            settings.mNormalHalfConeAngle = settings.mPlaneHalfConeAngle = JPH::DegreesToRadians(45);
            if (i >= 3)
                settings.mNormalHalfConeAngle = settings.mPlaneHalfConeAngle = JPH::DegreesToRadians(110);
            settings.mTwistMinAngle = JPH::DegreesToRadians(-25);
            settings.mTwistMaxAngle = JPH::DegreesToRadians(25);
            auto& joint = joints_[i - 1];
            joint = static_cast<JPH::SwingTwistConstraint*>(settings.Create(*bodies[HumanoidParts[i].parent], *bodies[i]));
            joint->SetNumVelocityStepsOverride(16);
            joint->SetNumPositionStepsOverride(4);
            joint->SetTargetOrientationBS(JPH::Quat::sIdentity());
            system_.AddConstraint(joint);
        }
        Tune(JointTuning{});
    } catch (...) {
        for (auto& joint : joints_) if (joint) system_.RemoveConstraint(joint);
        if (anchor_) system_.RemoveConstraint(anchor_);
        for (auto id : bodies_) if (!id.IsInvalid()) { api.RemoveBody(id); api.DestroyBody(id); }
        throw;
    }
}
PhysicsRig::~PhysicsRig() {
    for (auto& joint : joints_) system_.RemoveConstraint(joint);
    if (anchor_) system_.RemoveConstraint(anchor_);
    for (auto id : bodies_) { system_.GetBodyInterface().RemoveBody(id); system_.GetBodyInterface().DestroyBody(id); }
}
void PhysicsRig::Wake() { for (auto id : bodies_) system_.GetBodyInterface().ActivateBody(id); }
void PhysicsRig::ApplyHit(const HitReaction& reaction) {
    const auto& impulse = reaction.impulse;
    system_.GetBodyInterface().AddImpulse(bodies_.at(reaction.hit.bone),
        JPH::Vec3(impulse[0], impulse[1], impulse[2]), Position(reaction.hit.position));
}
void PhysicsRig::Reset() {
    movementZ_=0;
    animation_.Reset();
    for (auto& joint : joints_) joint->SetTargetOrientationBS(JPH::Quat::sIdentity());
    for (std::size_t i = 0; i < HumanoidPartCount; ++i)
        system_.GetBodyInterface().SetPositionRotationAndVelocity(bodies_[i], SpawnPosition(HumanoidParts[i].position),
            rotation_, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    for (auto& joint : joints_) joint->ResetWarmStart();
    if (anchor_) anchor_->ResetWarmStart();
    Wake();
}
void PhysicsRig::MoveRoot(float worldZ) {
    if (!movable_) return;
    const float delta=worldZ-RootZ();
    movementZ_=worldZ-offset_.GetZ();
    system_.GetBodyInterface().MoveKinematic(bodies_[0],SpawnPosition(HumanoidParts[0].position),rotation_,static_cast<float>(PhysicsWorld::FixedStep));
    const float forward=(rotation_*JPH::Vec3(0,0,-1)).GetZ();
    animation_.SetLocomotion(delta/static_cast<float>(PhysicsWorld::FixedStep)*forward);
}
void PhysicsRig::Tune(const JointTuning& tuning) {
    if (!std::isfinite(tuning.stiffness) || !std::isfinite(tuning.damping) || !std::isfinite(tuning.maxTorque)
        || tuning.stiffness < 0 || tuning.damping < 0 || tuning.maxTorque < 0)
        throw std::invalid_argument("Joint tuning must be finite and nonnegative.");
    for (auto& joint : joints_) {
        for (auto* motor : {&joint->GetSwingMotorSettings(), &joint->GetTwistMotorSettings()}) {
            motor->mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, tuning.stiffness, tuning.damping);
            motor->SetTorqueLimit(tuning.maxTorque);
        }
        const auto state = tuning.enabled && tuning.stiffness > 0 ? JPH::EMotorState::Position : JPH::EMotorState::Off;
        joint->SetSwingMotorState(state);
        joint->SetTwistMotorState(state);
    }
    Wake();
}
void PhysicsRig::Push() {
    const auto position = system_.GetBodyInterface().GetPosition(bodies_[1]);
    system_.GetBodyInterface().AddImpulse(bodies_[1], JPH::Vec3(0, 0, 12), position + JPH::RVec3(.12f,.18f,0));
}
void PhysicsRig::Animate(double elapsed) {
    animation_.Advance(elapsed);
    const auto& target = animation_.Target();
    for (std::size_t i = 1; i < HumanoidPartCount; ++i) {
        const auto& q = target[i].rotation;
        joints_[i - 1]->SetTargetOrientationBS(JPH::Quat(q[0],q[1],q[2],q[3]));
    }
}
bool PhysicsRig::RequestAttack(AttackKind kind) { return animation_.RequestAttack(kind); }
std::array<BodyPose, HumanoidPartCount> PhysicsRig::TargetPoses() const {
    std::array<BodyPose, HumanoidPartCount> result{};
    std::array<JPH::RVec3, HumanoidPartCount> positions;
    std::array<JPH::Quat, HumanoidPartCount> rotations;
    positions[0] = SpawnPosition(HumanoidParts[0].position);
    rotations[0] = rotation_;
    for (std::size_t i = 0; i < HumanoidPartCount; ++i) {
        if (i > 0) {
            const auto& part = HumanoidParts[i];
            const auto parent = static_cast<std::size_t>(part.parent);
            const auto& q = animation_.Target()[i].rotation;
            rotations[i] = rotations[parent] * JPH::Quat(q[0],q[1],q[2],q[3]);
            const auto pivot = Position(part.pivot);
            positions[i] = positions[parent] + rotations[parent] * (pivot - Position(HumanoidParts[parent].position))
                - rotations[i] * (pivot - Position(part.position));
        }
        const auto& p = positions[i];
        const auto& q = rotations[i];
        result[i] = {{p.GetX(),p.GetY(),p.GetZ()},{q.GetX(),q.GetY(),q.GetZ(),q.GetW()}};
    }
    return result;
}
std::array<BodyPose, HumanoidPartCount> PhysicsRig::Poses() const {
    std::array<BodyPose, HumanoidPartCount> poses;
    const auto& api = system_.GetBodyInterface();
    for (std::size_t i = 0; i < HumanoidPartCount; ++i) {
        const auto p = api.GetPosition(bodies_[i]);
        const auto q = api.GetRotation(bodies_[i]);
        poses[i] = {{p.GetX(),p.GetY(),p.GetZ()}, {q.GetX(),q.GetY(),q.GetZ(),q.GetW()}};
    }
    return poses;
}
float PhysicsRig::PoseError() const {
    float error = 0;
    for (const auto& joint : joints_) {
        const auto q = joint->GetTargetOrientationCS().Conjugated() * joint->GetRotationInConstraintSpace();
        error = std::max(error, 2.0f * std::acos(std::clamp(std::abs(q.GetW()), 0.0f, 1.0f)));
    }
    return JPH::RadiansToDegrees(error);
}
}
