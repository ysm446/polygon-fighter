#include "assets/CharacterAsset.h"
#include "physics/PhysicsWorld.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <chrono>

namespace {
void Require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Advance(pf::PhysicsWorld& world,int count) {
    for (int i=0;i<count;++i) {
        world.Step();
        for (std::size_t fighter=0;fighter<2;++fighter)
            for (const auto& pose:world.HumanoidPoses(fighter))
                for (float p:pose.position) Require(std::isfinite(p) && std::abs(p)<5,"Imported animation diverged.");
    }
}
}
int main() {
    try {
        using namespace DirectX;
        const auto male=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"male-fighter.glb");
        const auto female=pf::CharacterAsset::Load(std::filesystem::path(ASSET_DIRECTORY)/"female-fighter.glb");
        struct TemporaryFile {
            std::filesystem::path path=std::filesystem::temp_directory_path()/
                ("polygon-fighter-invalid-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".glb");
            ~TemporaryFile() {std::error_code error; std::filesystem::remove(path,error);}
        } invalid;
        std::ifstream source(std::filesystem::path(ASSET_DIRECTORY)/"male-fighter.glb",std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
        auto reject=[&](const std::string& contents) {
            {std::ofstream output(invalid.path,std::ios::binary); output.write(contents.data(),static_cast<std::streamsize>(contents.size()));}
            bool rejected=false;
            try {pf::CharacterAsset::Load(invalid.path);} catch (const std::runtime_error&) {rejected=true;}
            Require(rejected,"Invalid GLB must be rejected.");
        };
        reject(bytes.substr(0,16));
        const auto name=bytes.find("Torso");
        Require(name!=std::string::npos,"Missing fixture bone.");
        bytes.replace(name,5,"Other");
        reject(bytes);
        for (const auto& model:{male,female}) {
            Require(model->indices.size()>3000 && model->indices.size()<6000,"Unexpected triangle count.");
            std::array<pf::BodyPose,pf::HumanoidPartCount> rest;
            for (std::size_t i=0;i<rest.size();++i) {rest[i].position=pf::HumanoidParts[i].position; rest[i].position[0]-=.9f;}
            const auto matrices=model->SkinMatrices(rest);
            for (const auto& vertex:model->vertices) {
                auto skinned=XMVectorZero();
                const auto position=XMLoadFloat3(&vertex.position);
                for (std::size_t influence=0;influence<4;++influence)
                    skinned+=XMVectorScale(XMVector3TransformCoord(position,XMLoadFloat4x4(&matrices[vertex.joints[influence]])),vertex.weights[influence]);
                Require(XMVectorGetX(XMVector3Length(skinned-position))<.0002f,"Bind pose skinning must preserve mesh.");
            }
            const auto turn=XMMatrixRotationY(XM_PI);
            for (auto& pose:rest) {
                const auto& p=pose.position;
                XMFLOAT3 transformed;
                XMStoreFloat3(&transformed,XMVector3TransformCoord(XMVectorSet(p[0],p[1],p[2],1),turn));
                pose.position={transformed.x+.9f,transformed.y,transformed.z-1.05f};
                pose.rotation={0,1,0,0};
            }
            const auto turned=model->SkinMatrices(rest);
            for (const auto& vertex:model->vertices) {
                const auto position=XMLoadFloat3(&vertex.position);
                auto actual=XMVectorZero();
                for (std::size_t influence=0;influence<4;++influence)
                    actual+=XMVectorScale(XMVector3TransformCoord(position,XMLoadFloat4x4(&turned[vertex.joints[influence]])),vertex.weights[influence]);
                const auto expected=XMVector3TransformCoord(position,turn*XMMatrixTranslation(.9f,0,-1.05f));
                Require(XMVectorGetX(XMVector3Length(actual-expected))<.0003f,"Skinning must preserve rotated P2 placement.");
            }
            for (double time:{0.,.35,1.}) {
                const auto actual=model->animations->punch.Sample(time);
                const auto expected=pf::AnimationPlayer::PunchClip().Sample(time);
                for (std::size_t part:{5u,6u}) {
                    float dot=0;
                    for (std::size_t axis=0;axis<4;++axis) dot+=actual[part].rotation[axis]*expected[part].rotation[axis];
                    Require(std::abs(dot)>.9995f,"Imported punch must match physics axes and timing.");
                }
            }
            const auto anticipation=model->animations->punch.Sample(.23);
            const auto impact=model->animations->punch.Sample(.35);
            const auto recoil=model->animations->punch.Sample(.60);
            Require(anticipation[1].rotation[1]<-.03f && impact[1].rotation[1]>.03f
                && recoil[1].rotation[1]<-.01f,"Punch needs torso anticipation, impact and recoil.");
            Require(anticipation[6].rotation[0]>.65f && std::abs(impact[6].rotation[0])<.01f
                && recoil[6].rotation[0]>.5f,"Punch must load, extend and recoil the elbow.");
            std::cout<<"Model vertices="<<model->vertices.size()<<" triangles="<<model->indices.size()/3<<'\n';
        }
        pf::PhysicsWorld world(true);
        world.SetAnimations(0,male->animations); world.SetAnimations(1,female->animations);
        for (std::size_t attacker=0;attacker<2;++attacker) {
            world.ResetMatch(); Advance(world,120); world.RequestPunch(attacker); Advance(world,180);
            Require(world.Combat().Fighter(1-attacker).hp==88,"Imported punch must hit once in both directions.");
            Require(world.HumanoidPoses(1-attacker)[2].position[1]>1.7f,"Imported rig must recover.");
        }
        world.ResetMatch(); world.SetGuard(1,true); Advance(world,120); world.RequestPunch(); Advance(world,180);
        Require(world.Combat().Fighter(1).hp==97,"Imported guard must reduce damage.");
        world.ResetMatch();
        for (int attack=0;attack<9;++attack) {world.RequestPunch(); Advance(world,120);}
        Require(world.Combat().Fighter(1).hp==0,"Imported match must reach KO.");
        world.ResetMatch(); Advance(world,120);
        Require(world.Combat().Fighter(1).hp==100 && world.HumanoidPoses(1)[2].position[1]>1.7f,"Imported KO reset failed.");
        std::cout<<"PASS: GLB mesh, skin bind pose, animation axes/timing, physical combat, guard, KO/reset\n";
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n'; return 1;}
}
