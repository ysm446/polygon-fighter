#include "assets/CharacterAsset.h"
#include "physics/PhysicsWorld.h"
#include "app/FixedClock.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void Advance(pf::PhysicsWorld& world,int frames) { for (int i=0;i<frames;++i) world.Step(); }
}
int main() {
    try {
        using pf::AttackKind;
        pf::CombatSystem combat;
        pf::FighterPoses overlap{};
        Require(combat.RequestAttack(0,AttackKind::Kick),"Kick input rejected.");
        Require(!combat.RequestPunch(0),"Punch must not cancel kick.");
        int active=0,hits=0;
        for (int frame=0;frame<78;++frame) {
            active+=combat.HitboxActive(0)?1:0;
            const auto result=combat.Step(overlap);
            hits+=static_cast<int>(result.hitCount);
            if (frame<30) Require(combat.Fighter(1).hp==100,"Kick hit during startup.");
            if (frame==30) {
                Require(combat.Fighter(1).hp==82 && combat.Fighter(1).stunFrames==30,"Kick damage/stun incorrect.");
                Require(result.hits[0].attack==AttackKind::Kick,"Hit lost attack kind.");
            }
        }
        Require(active==10 && hits==1 && combat.Fighter(0).attackFrame==-1,"Kick frame contract failed.");
        combat.Reset(); combat.SetGuard(1,true); combat.RequestAttack(0,AttackKind::Kick);
        for (int i=0;i<31;++i) combat.Step(overlap);
        Require(combat.Fighter(1).hp==96 && combat.Fighter(1).stunFrames==12,"Kick guard reduction failed.");
        Require(!combat.RequestAttack(1,AttackKind::Kick),"Stun must reject kick.");
        combat.Reset(); combat.RequestAttack(0,AttackKind::Kick);
        for (int i=0;i<9;++i) combat.Step(overlap);
        combat.RequestPunch(1);
        pf::CombatFrame trade;
        for (int i=0;i<22;++i) trade=combat.Step(overlap);
        Require(trade.hitCount==2 && combat.Fighter(0).hp==88 && combat.Fighter(1).hp==82,
            "Punch/kick trade must use each move's damage.");
        Require(combat.Fighter(0).stunFrames==24 && combat.Fighter(1).stunFrames==30
            && combat.Fighter(0).attackFrame==-1 && combat.Fighter(1).attackFrame==-1,"Trade must interrupt both moves with their own stun.");
        combat.Reset(); combat.RequestAttack(0,AttackKind::Kick); combat.RequestPunch(1);
        for (int i=0;i<78;++i) combat.Step(overlap);
        Require(combat.Fighter(0).hp==88 && combat.Fighter(1).hp==100,"Faster punch must interrupt kick startup.");
        combat.Reset();
        for (int attack=0;attack<6;++attack) {
            Require(combat.RequestAttack(0,AttackKind::Kick),"Next kick unavailable.");
            for (int i=0;i<78;++i) combat.Step(overlap);
        }
        Require(combat.Fighter(1).hp==0 && !combat.RequestAttack(1,AttackKind::Kick),"Kick KO must clamp HP and block input.");
        for (int rate:{30,60,144}) {
            combat.Reset(); combat.RequestAttack(0,AttackKind::Kick);
            pf::FixedClock clock;
            for (int i=0;i<rate*2;++i) clock.Advance(1./rate,false,false,[&]{combat.Step(overlap);});
            Require(combat.Fighter(1).hp==82 && combat.Fighter(0).attackFrame==-1,"Kick depends on render cadence.");
        }
        const auto male=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"male-fighter.glb");
        const auto female=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"female-fighter.glb");
        for (const auto* clips:{male->animations.get(),female->animations.get()}) {
            Require(std::abs(clips->kick.Duration()-1.3)<.001,"Kick duration incorrect.");
            for (double time:{.32,.50,.72,1.3}) {
                const auto actual=clips->kick.Sample(time), expected=pf::AnimationPlayer::KickClip().Sample(time);
                for (std::size_t bone=1;bone<pf::HumanoidPartCount;++bone) {
                    float dot=0;
                    for (int axis=0;axis<4;++axis) dot+=actual[bone].rotation[axis]*expected[bone].rotation[axis];
                    Require(std::abs(dot)>.999f,"Kick must preserve chamber, extension, recoil and recovery axes.");
                }
            }
        }
        pf::PhysicsWorld world(true);
        world.SetAnimations(0,male->animations); world.SetAnimations(1,female->animations);
        for (std::size_t attacker=0;attacker<2;++attacker) {
            world.ResetMatch(); Advance(world,120);
            Require(world.RequestAttack(attacker,AttackKind::Kick),"Physical kick rejected.");
            Advance(world,180);
            std::cout<<"attacker="<<attacker<<" hp="<<world.Combat().Fighter(1-attacker).hp<<" error="<<world.HumanoidPoseError()<<'\n';
            Require(world.Combat().Fighter(1-attacker).hp==82,"Physical kick must hit in both directions.");
            Require(world.HumanoidPoseError()<5,"Kick must recover standing pose.");
            world.ResetMatch(); world.SetGuard(1-attacker,true); Advance(world,60);
            world.RequestAttack(attacker,AttackKind::Kick); Advance(world,120);
            Require(world.Combat().Fighter(1-attacker).hp==96,"Physical kick must support guard.");
        }
        world.ResetMatch(); world.SetMove(0,-1); Advance(world,120); world.SetMove(0,0); Advance(world,60);
        world.RequestAttack(0,AttackKind::Kick); Advance(world,180);
        Require(world.Combat().Fighter(1).hp==100,"Distant kick must miss.");
        world.ResetMatch(); world.SetMove(0,1); Advance(world,60); world.SetMove(0,0); Advance(world,60);
        const auto root=world.HumanoidPoses()[0].position[2];
        world.RequestAttack(0,AttackKind::Kick); world.SetMove(0,-1); Advance(world,78);
        Require(std::abs(world.HumanoidPoses()[0].position[2]-root)<.001f,"Kick must block movement for all 78 frames.");
        Require(world.Combat().Fighter(1).hp==82,"Close kick must hit only once.");
        world.SetMove(0,0); Advance(world,120);
        Require(world.HumanoidPoseError()<5,"Close kick must recover.");
        world.RequestAttack(0,AttackKind::Kick); Advance(world,20); world.ResetMatch();
        Require(!world.Attacking() && world.Combat().Fighter(0).attackFrame==-1,"Reset must cancel kick animation and rules.");
        world.ResetMatch(); pf::JointTuning motors; motors.enabled=false; world.SetJointTuning(motors);
        world.RequestAttack(0,AttackKind::Kick); Advance(world,120);
        Require(world.Combat().Fighter(1).hp==100,"Target pose must not deal damage with motors off.");
        std::cout<<"PASS: kick timing, guard, cadence, Blender stages, physical hit/miss and recovery\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
