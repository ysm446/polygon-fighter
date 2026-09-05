#include "physics/PhysicsWorld.h"
#include "assets/CharacterAsset.h"
#include "app/FixedClock.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok,const char* message) {if (!ok) throw std::runtime_error(message);}
void Advance(pf::PhysicsWorld& world,int count) {
    for (int i=0;i<count;++i) {
        world.Step();
        for (std::size_t f=0;f<2;++f)
            for (const auto& part:world.HumanoidPoses(f))
                for (float p:part.position) Require(std::isfinite(p) && std::abs(p)<6,"Moving rig diverged.");
    }
}
float Root(const pf::PhysicsWorld& world,std::size_t f) {return world.HumanoidPoses(f)[0].position[2];}
float Cadence(int fps) {
    pf::PhysicsWorld world(true);
    world.SetMove(0,-1);
    pf::FixedClock clock;
    for (int frame=0;frame<fps*2;++frame) clock.Advance(1./fps,false,false,[&]{world.Step();});
    return Root(world,0);
}
}
int main() {
    try {
        const float reference=Cadence(60);
        Require(std::abs(Cadence(30)-reference)<.001f && std::abs(Cadence(144)-reference)<.001f,"Movement depends on render cadence.");
        pf::PhysicsWorld world(true);
        const auto male=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"male-fighter.glb");
        const auto female=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"female-fighter.glb");
        world.SetAnimations(0,male->animations); world.SetAnimations(1,female->animations);
        for (int step=0;step<660;++step) {
            world.SetMove(0,step<90?-1.f:(step>=240 && step<360?1.f:0.f));
            if (step==120 || step==480) world.RequestPunch();
            Advance(world,1);
        }
        Require(world.Combat().Fighter(1).hp==88,"Retreat/approach sequence must miss once and hit once.");
        Require(world.HumanoidPoseError()<5,"Legs must return to idle after retreat and approach.");
        world.ResetMatch();
        Advance(world,120);
        world.SetMove(0,-1); Advance(world,30);
        const auto walking=world.TargetPoses();
        Require(std::abs(walking[7].rotation[0])+std::abs(walking[9].rotation[0])>.02f,"Moving fighter must animate legs.");
        Advance(world,30); world.SetMove(0,0); Advance(world,90);
        Require(world.FighterDistance()>1.6f,"Retreat must open range.");
        world.RequestPunch(); Advance(world,120);
        Require(world.Combat().Fighter(1).hp==100,"Out-of-range punch must miss.");
        world.SetMove(0,1); Advance(world,90); world.SetMove(0,0); Advance(world,120);
        Require(world.FighterDistance()>=.799f && world.FighterDistance()<.85f,"Approach must stop at minimum distance.");
        Require(world.HumanoidPoses()[2].position[1]>1.7f,"Moving fighter must recover standing pose.");
        const auto attackRoot=Root(world,0);
        world.RequestPunch(); world.SetMove(0,-1); Advance(world,30);
        Require(std::abs(Root(world,0)-attackRoot)<.001f,"Attacking fighter must not move.");
        world.SetMove(0,0);
        Require(world.Combat().Fighter(1).hp==88,"Approach must restore punch range.");
        const auto stunnedRoot=Root(world,1);
        world.SetMove(1,-1);
        while (world.Combat().Fighter(1).stunFrames>0) Advance(world,1);
        Require(std::abs(Root(world,1)-stunnedRoot)<.001f,"Hitstun must stop movement.");
        world.SetMove(1,0); Advance(world,120);
        world.SetGuard(0,true); world.SetMove(0,-1); Advance(world,60);
        Require(std::abs(Root(world,0)-attackRoot)<.001f,"Guard must stop movement.");
        world.ResetMatch(); world.SetMove(0,1); world.SetMove(1,1); Advance(world,180);
        Require(std::abs(world.FighterDistance()-.8f)<.001f,"Simultaneous approach must not cross fighters.");
        world.SetMove(0,-1); world.SetMove(1,-1); Advance(world,600);
        Require(std::abs(Root(world,0)-2)<.001f && std::abs(Root(world,1)+3)<.001f,"Stage limits failed.");
        Require(std::abs(world.FighterSpeed(0))<.001f && std::abs(world.FighterSpeed(1))<.001f,"Blocked movement must stop gait.");
        world.ResetMatch(); Advance(world,60);
        Require(std::abs(Root(world,0))<.001f && std::abs(Root(world,1)+1.05f)<.001f,"Reset must restore roots and clear held input.");
        for (int attack=0;attack<9;++attack) {world.RequestPunch(); Advance(world,120);}
        Require(world.Combat().Fighter(1).hp==0,"Movement scene must still reach KO.");
        const auto koRoot=Root(world,1); world.SetMove(1,-1); Advance(world,60);
        Require(std::abs(Root(world,1)-koRoot)<.001f,"KO fighter must not move.");
        std::cout<<"PASS: walk, retreat/miss, approach/hit, state locks, bounds, no crossing, reset, cadence\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
