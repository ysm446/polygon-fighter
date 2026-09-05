#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>

#include "physics/PhysicsWorld.h"
#include "physics/PhysicsRig.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace pf {
namespace {
constexpr JPH::ObjectLayer StaticLayer = 0;
constexpr JPH::ObjectLayer DynamicLayer = 1;

void Trace(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

// Joltのグローバル登録はPhysicsSystemより長く生存させる。
struct Runtime {
    std::unique_ptr<JPH::Factory> factory;
    Runtime() {
        if (JPH::Factory::sInstance) throw std::runtime_error("Only one PhysicsWorld is supported.");
        JPH::RegisterDefaultAllocator();
        JPH::Trace = Trace;
        factory = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory.get();
        JPH::RegisterTypes();
    }
    ~Runtime() {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
    }
};
}

struct PhysicsWorld::Impl {
    Runtime runtime;
    JPH::ObjectLayerPairFilterTable pairs{2};
    JPH::BroadPhaseLayerInterfaceTable broadPhase{2, 2};
    std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> broadFilter;
    JPH::TempAllocatorImpl allocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
        static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 2u, 9u)) - 1};
    JPH::PhysicsSystem system;
    std::unique_ptr<PhysicsRig> rig;
    std::unique_ptr<PhysicsRig> opponent;
    CombatSystem combat;
    CombatFrame lastCombatFrame;
    HitReactionTuning hitTuning;
    std::array<HitReaction, 2> reactions{};
    JointTuning tuning;
    JPH::BodyID ground;
    JPH::BodyID box;

    explicit Impl(bool combatScene) {
        pairs.EnableCollision(StaticLayer, DynamicLayer);
        pairs.EnableCollision(DynamicLayer, DynamicLayer);
        broadPhase.MapObjectToBroadPhaseLayer(StaticLayer, JPH::BroadPhaseLayer(0));
        broadPhase.MapObjectToBroadPhaseLayer(DynamicLayer, JPH::BroadPhaseLayer(1));
        broadFilter = std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broadPhase, 2, pairs, 2);
        system.Init(1024, 0, 1024, 1024, broadPhase, *broadFilter, pairs);
        auto& bodies = system.GetBodyInterface();
        JPH::RefConst<JPH::Shape> groundShape = new JPH::BoxShape(JPH::Vec3(10, 0.25f, 10));
        JPH::BodyCreationSettings groundSettings(groundShape, JPH::RVec3(0, -0.25f, 0),
            JPH::Quat::sIdentity(), JPH::EMotionType::Static, StaticLayer);
        groundSettings.mFriction = 0.7f;
        ground = bodies.CreateAndAddBody(groundSettings, JPH::EActivation::DontActivate);
        JPH::RefConst<JPH::Shape> boxShape = new JPH::BoxShape(JPH::Vec3::sReplicate(0.5f));
        JPH::BodyCreationSettings boxSettings(boxShape, JPH::RVec3(-2, 4, 0),
            JPH::Quat::sIdentity(), JPH::EMotionType::Dynamic, DynamicLayer);
        boxSettings.mFriction = 0.7f;
        boxSettings.mRestitution = 0.15f;
        boxSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        boxSettings.mMassPropertiesOverride.mMass = 10.0f;
        box = bodies.CreateAndAddBody(boxSettings, JPH::EActivation::Activate);
        if (ground.IsInvalid() || box.IsInvalid()) throw std::runtime_error("Could not create physics bodies.");
        rig = std::make_unique<PhysicsRig>(system);
        if (combatScene) opponent = std::make_unique<PhysicsRig>(system, JPH::RVec3(0,0,-1.05f), JPH::JPH_PI, 2);
        system.OptimizeBroadPhase();
    }
    ~Impl() {
        rig.reset();
        opponent.reset();
        auto& bodies = system.GetBodyInterface();
        bodies.RemoveBody(box);
        bodies.DestroyBody(box);
        bodies.RemoveBody(ground);
        bodies.DestroyBody(ground);
    }
};

PhysicsWorld::PhysicsWorld(bool combatScene) : impl_(std::make_unique<Impl>(combatScene)) {}
PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Step() {
    for (auto& reaction : impl_->reactions) if (reaction.visibleFrames > 0) --reaction.visibleFrames;
    if (impl_->opponent) {
        impl_->rig->SetGuard(impl_->combat.Fighter(0).guardRequested && impl_->combat.Fighter(0).hp > 0);
        impl_->opponent->SetGuard(impl_->combat.Fighter(1).guardRequested && impl_->combat.Fighter(1).hp > 0);
        impl_->opponent->Animate(FixedStep);
    }
    impl_->rig->Animate(FixedStep);
    const auto error = impl_->system.Update(static_cast<float>(FixedStep), 1, &impl_->allocator, &impl_->jobs);
    if (error != JPH::EPhysicsUpdateError::None) throw std::runtime_error("Jolt simulation capacity exceeded.");
    if (impl_->opponent) {
        impl_->lastCombatFrame = impl_->combat.Step({impl_->rig->Poses(), impl_->opponent->Poses()});
        for (std::size_t i = 0; i < impl_->lastCombatFrame.hitCount; ++i) {
            const auto& hit = impl_->lastCombatFrame.hits[i];
            const auto defender = hit.defender;
            auto* rig = defender == 0 ? impl_->rig.get() : impl_->opponent.get();
            auto& reaction = impl_->reactions[defender];
            reaction = {hit, {}, 30};
            const float strength = impl_->hitTuning.impulse * (hit.guarded ? impl_->hitTuning.guardMultiplier : 1.0f);
            for (std::size_t axis = 0; axis < 3; ++axis) reaction.impulse[axis] = hit.direction[axis] * strength;
            rig->ApplyHit(reaction);
            if (!hit.guarded) rig->CancelAnimation();
            if (impl_->combat.Fighter(defender).hp == 0) {
                auto disabled = impl_->tuning;
                disabled.enabled = false;
                rig->Tune(disabled);
            }
        }
    }
}

void PhysicsWorld::ResetBox() {
    auto& bodies = impl_->system.GetBodyInterface();
    bodies.SetPositionRotationAndVelocity(impl_->box, JPH::RVec3(-2, 4, 0), JPH::Quat::sIdentity(),
        JPH::Vec3::sZero(), JPH::Vec3::sZero());
    bodies.ActivateBody(impl_->box);
}

void PhysicsWorld::SetGravity(float gravity) {
    impl_->system.SetGravity(JPH::Vec3(0, gravity, 0));
    impl_->system.GetBodyInterface().ActivateBody(impl_->box);
    impl_->rig->Wake();
    if (impl_->opponent) impl_->opponent->Wake();
}

void PhysicsWorld::PushBox() {
    const auto position = impl_->system.GetBodyInterface().GetPosition(impl_->box);
    impl_->system.GetBodyInterface().AddImpulse(impl_->box, JPH::Vec3(12, 45, 0),
        position + JPH::RVec3(0.35f, 0, 0));
}

BodyPose PhysicsWorld::BoxPose() const {
    const auto& bodies = impl_->system.GetBodyInterface();
    const auto p = bodies.GetPosition(impl_->box);
    const auto q = bodies.GetRotation(impl_->box);
    return {{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())},
        {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}};
}

float PhysicsWorld::BoxSpeed() const { return impl_->system.GetBodyInterface().GetLinearVelocity(impl_->box).Length(); }
bool PhysicsWorld::BoxActive() const { return impl_->system.GetBodyInterface().IsActive(impl_->box); }
void PhysicsWorld::ResetHumanoid() { if (IsCombatScene()) ResetMatch(); else impl_->rig->Reset(); }
void PhysicsWorld::PushHumanoid() { impl_->rig->Push(); }
void PhysicsWorld::SetJointTuning(const JointTuning& tuning) {
    impl_->tuning = tuning;
    for (std::size_t i = 0; i < (IsCombatScene() ? 2u : 1u); ++i) {
        auto effective = tuning;
        if (IsCombatScene() && impl_->combat.Fighter(i).hp == 0) effective.enabled = false;
        (i == 0 ? impl_->rig : impl_->opponent)->Tune(effective);
    }
}
std::array<BodyPose, HumanoidPartCount> PhysicsWorld::HumanoidPoses(std::size_t fighter) const {
    if (fighter > 1 || (fighter == 1 && !IsCombatScene())) throw std::out_of_range("Fighter does not exist.");
    return (fighter == 0 ? impl_->rig : impl_->opponent)->Poses();
}
float PhysicsWorld::HumanoidPoseError() const { return impl_->rig->PoseError(); }
bool PhysicsWorld::RequestPunch(std::size_t fighter) {
    if (fighter > 1 || (fighter == 1 && !IsCombatScene())) return false;
    if (IsCombatScene() && !impl_->combat.RequestPunch(fighter)) return false;
    return (fighter == 0 ? impl_->rig : impl_->opponent)->RequestPunch();
}
void PhysicsWorld::SetPlaybackSpeed(float speed) { impl_->rig->SetPlaybackSpeed(IsCombatScene() ? 1.0f : speed); }
void PhysicsWorld::SetAnimations(std::size_t fighter, std::shared_ptr<const CharacterAnimations> clips) {
    if (fighter>1 || (fighter==1 && !IsCombatScene())) throw std::out_of_range("Fighter does not exist.");
    (fighter==0 ? impl_->rig : impl_->opponent)->SetClips(std::move(clips));
    ResetMatch();
}
bool PhysicsWorld::Punching() const { return impl_->rig->Punching(); }
double PhysicsWorld::AnimationTime() const { return impl_->rig->AnimationTime(); }
std::array<BodyPose, HumanoidPartCount> PhysicsWorld::TargetPoses(std::size_t fighter) const {
    if (fighter > 1 || (fighter == 1 && !IsCombatScene())) throw std::out_of_range("Fighter does not exist.");
    return (fighter == 0 ? impl_->rig : impl_->opponent)->TargetPoses();
}
void PhysicsWorld::SetGuard(std::size_t fighter, bool held) { impl_->combat.SetGuard(fighter, held); }
void PhysicsWorld::ResetMatch() {
    impl_->combat.Reset();
    impl_->lastCombatFrame = {};
    impl_->reactions = {};
    impl_->rig->Reset();
    if (impl_->opponent) impl_->opponent->Reset();
    SetJointTuning(impl_->tuning);
}
bool PhysicsWorld::IsCombatScene() const { return impl_->opponent != nullptr; }
const CombatSystem& PhysicsWorld::Combat() const { return impl_->combat; }
const CombatFrame& PhysicsWorld::LastCombatFrame() const { return impl_->lastCombatFrame; }
void PhysicsWorld::SetHitReactionTuning(const HitReactionTuning& tuning) {
    if (!std::isfinite(tuning.impulse) || tuning.impulse < 0 || tuning.impulse > 40
        || !std::isfinite(tuning.guardMultiplier) || tuning.guardMultiplier < 0 || tuning.guardMultiplier > 1)
        throw std::invalid_argument("Invalid hit reaction tuning.");
    impl_->hitTuning = tuning;
}
const std::array<HitReaction, 2>& PhysicsWorld::HitReactions() const { return impl_->reactions; }
}
