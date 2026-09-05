#include "assets/CharacterAsset.h"
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace pf {
namespace {
using namespace DirectX;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
XMMATRIX Matrix(const float* values) {
    XMFLOAT4X4 result;
    std::memcpy(&result,values,sizeof(result));
    // glTFの列優先配列を行ベクトル用行列として扱う。
    return XMLoadFloat4x4(&result);
}
XMMATRIX World(const cgltf_node* node) {
    float values[16];
    cgltf_node_transform_world(node,values);
    return Matrix(values);
}
bool Near(FXMMATRIX a, CXMMATRIX b, float epsilon = .0002f) {
    XMFLOAT4X4 aa,bb;
    XMStoreFloat4x4(&aa,a); XMStoreFloat4x4(&bb,b);
    for (int row=0;row<4;++row) for (int col=0;col<4;++col)
        if (!std::isfinite(aa.m[row][col]) || std::abs(aa.m[row][col]-bb.m[row][col])>epsilon) return false;
    return true;
}
void Read(const cgltf_accessor* accessor, std::size_t index, float* output, std::size_t count) {
    Require(accessor && !accessor->is_sparse && index < accessor->count
        && cgltf_accessor_read_float(accessor,index,output,count),"Cannot read glTF accessor.");
    for (std::size_t i=0;i<count;++i) Require(std::isfinite(output[i]),"Nonfinite glTF value.");
}
XMVECTOR Sample(const cgltf_animation_sampler& sampler, float time, std::size_t count, bool rotation) {
    Require(sampler.input && sampler.output && sampler.input->count>0
        && sampler.input->count == sampler.output->count,"Invalid animation sampler.");
    Require(sampler.interpolation == cgltf_interpolation_type_linear || sampler.interpolation == cgltf_interpolation_type_step,
        "Only LINEAR / STEP animation is supported.");
    std::size_t lower = 0;
    float aTime=0,bTime=0;
    Read(sampler.input,0,&aTime,1);
    while (lower+1 < sampler.input->count) {
        Read(sampler.input,lower+1,&bTime,1);
        Require(bTime>aTime,"Animation times must increase.");
        if (bTime>time) break;
        ++lower; aTime=bTime;
    }
    float a[4]{0,0,0,1},b[4]{0,0,0,1};
    Read(sampler.output,lower,a,count);
    const auto av=XMVectorSet(a[0],a[1],a[2],a[3]);
    if (lower+1 == sampler.input->count || sampler.interpolation == cgltf_interpolation_type_step) return av;
    Read(sampler.output,lower+1,b,count);
    const auto bv=XMVectorSet(b[0],b[1],b[2],b[3]);
    const float blend=std::clamp((time-aTime)/(bTime-aTime),0.f,1.f);
    return rotation ? XMQuaternionNormalize(XMQuaternionSlerp(av,bv,blend)) : XMVectorLerp(av,bv,blend);
}
void LoadClip(cgltf_data& data, const cgltf_animation& animation, AnimationClip& clip,
              const std::array<cgltf_node*,HumanoidPartCount>& bones) {
    const float duration = std::strcmp(clip.name,"Idle")==0 ? 2.f : std::strcmp(clip.name,"Walk")==0 ? .8f : std::strcmp(clip.name,"Kick")==0 ? 1.3f : 1.f;
    float last=0;
    for (std::size_t channel=0;channel<animation.channels_count;++channel) {
        const auto* sampler=animation.channels[channel].sampler;
        Require(sampler && sampler->input && sampler->input->count>0,"Missing animation sampler.");
        float end; Read(sampler->input,sampler->input->count-1,&end,1); last=std::max(last,end);
    }
    Require(std::abs(last-duration)<.001f,"Animation duration does not match combat timing.");
    std::vector<cgltf_node> rest(data.nodes,data.nodes+data.nodes_count);
    std::array<XMFLOAT4X4,HumanoidPartCount> inverseBind;
    for (std::size_t part=0;part<HumanoidPartCount;++part)
        XMStoreFloat4x4(&inverseBind[part],XMMatrixInverse(nullptr,World(bones[part])));
    for (int frame=0;frame<=static_cast<int>(duration*60);++frame) {
        const float time=frame/60.f;
        for (std::size_t index=0;index<data.nodes_count;++index) data.nodes[index]=rest[index];
        for (std::size_t channel=0;channel<animation.channels_count;++channel) {
            const auto& c=animation.channels[channel];
            Require(c.target_node && c.sampler,"Missing animation target.");
            const auto index=static_cast<std::size_t>(c.target_node-data.nodes);
            Require(index<data.nodes_count && !c.target_node->has_matrix,"Unsupported animation node.");
            auto& node=data.nodes[index];
            XMFLOAT4 value;
            const bool rotation=c.target_path==cgltf_animation_path_type_rotation;
            Require(rotation || c.target_path==cgltf_animation_path_type_translation || c.target_path==cgltf_animation_path_type_scale,
                "Morph animation is not supported.");
            XMStoreFloat4(&value,Sample(*c.sampler,time,rotation?4:3,rotation));
            if (rotation) {
                const float length=value.x*value.x+value.y*value.y+value.z*value.z+value.w*value.w;
                Require(std::abs(length-1)<.001f,"Invalid animation quaternion.");
                std::memcpy(node.rotation,&value,sizeof(value)); node.has_rotation=1;
            } else {
                const float* expected=c.target_path==cgltf_animation_path_type_translation ? rest[index].translation : rest[index].scale;
                const float actual[]{value.x,value.y,value.z};
                for (int axis=0;axis<3;++axis) Require(std::abs(actual[axis]-expected[axis])<.0001f,
                    "Physical rig requires fixed animation translations and scales.");
            }
        }
        std::array<XMFLOAT4X4,HumanoidPartCount> rotations;
        for (std::size_t part=0;part<HumanoidPartCount;++part) {
            const auto animated=World(bones[part]);
            XMVECTOR scale,rotation,translation;
            Require(XMMatrixDecompose(&scale,&rotation,&translation,XMLoadFloat4x4(&inverseBind[part])*animated),"Invalid animated transform.");
            XMStoreFloat4x4(&rotations[part],XMMatrixRotationQuaternion(rotation));
        }
        Pose pose;
        for (std::size_t part=0;part<HumanoidPartCount;++part) {
            const int parent=HumanoidParts[part].parent;
            const auto local=XMLoadFloat4x4(&rotations[part])*(parent<0 ? XMMatrixIdentity() : XMMatrixTranspose(XMLoadFloat4x4(&rotations[parent])));
            XMFLOAT4 q; XMStoreFloat4(&q,XMQuaternionNormalize(XMQuaternionRotationMatrix(local)));
            pose[part].rotation={q.x,q.y,q.z,q.w};
        }
        clip.keys.push_back({frame/60.0,pose});
    }
    for (std::size_t i=0;i<data.nodes_count;++i) data.nodes[i]=rest[i];
}
}

std::shared_ptr<const CharacterAsset> CharacterAsset::Load(const std::filesystem::path& path) {
    Require(std::filesystem::file_size(path)<32*1024*1024,"Character file exceeds size limit.");
    cgltf_options options{};
    cgltf_data* raw=nullptr;
    Require(cgltf_parse_file(&options,path.string().c_str(),&raw)==cgltf_result_success,"Could not parse character glTF.");
    const std::unique_ptr<cgltf_data,decltype(&cgltf_free)> data(raw,cgltf_free);
    Require(raw->file_type==cgltf_file_type_glb && raw->buffers_count==1 && !raw->buffers[0].uri,
        "Character must be a self-contained GLB.");
    Require(cgltf_load_buffers(&options,raw,path.string().c_str())==cgltf_result_success
        && cgltf_validate(raw)==cgltf_result_success,"Invalid character buffers.");
    Require(raw->skins_count==1 && raw->skins[0].joints_count==HumanoidPartCount
        && raw->nodes_count<=64 && raw->meshes_count==1 && raw->animations_count==5,
        "Character must contain one mesh, 11 joints and Idle / Punch / Guard / Walk / Kick clips.");
    auto asset=std::make_shared<CharacterAsset>();
    std::array<cgltf_node*,HumanoidPartCount> bones{};
    const auto& skin=raw->skins[0];
    for (std::size_t joint=0;joint<HumanoidPartCount;++joint) {
        const auto* node=skin.joints[joint];
        Require(node && node->name,"Unnamed skin joint.");
        std::size_t part=0;
        while (part<HumanoidPartCount && std::strcmp(node->name,HumanoidParts[part].name)!=0) ++part;
        Require(part<HumanoidPartCount && !bones[part],"Unknown or duplicate physics bone.");
        bones[part]=skin.joints[joint]; asset->bodyForJoint[joint]=part;
        XMFLOAT4X4 bindWorld;
        XMStoreFloat4x4(&bindWorld,World(node));
        const auto& pivot=part==0 ? HumanoidParts[part].position : HumanoidParts[part].pivot;
        Require(std::abs(bindWorld._41-(pivot[0]-.9f))<.001f && std::abs(bindWorld._42-pivot[1])<.001f
            && std::abs(bindWorld._43-pivot[2])<.001f,"Skeleton pivots do not match the physical rig.");
        float inverse[16]; Read(skin.inverse_bind_matrices,joint,inverse,16);
        Require(Near(Matrix(inverse)*World(node),XMMatrixIdentity()),"Unsupported mesh bind transform.");
        const auto& p=HumanoidParts[part].position;
        XMStoreFloat4x4(&asset->skinFromBody[joint],Matrix(inverse)*World(node)*XMMatrixTranslation(.9f-p[0],-p[1],-p[2]));
    }
    for (std::size_t part=1;part<HumanoidPartCount;++part)
        Require(bones[part]->parent==bones[HumanoidParts[part].parent],"Incompatible skeleton hierarchy.");
    std::size_t meshNodes=0;
    for (std::size_t i=0;i<raw->nodes_count;++i) if (raw->nodes[i].mesh) {
        ++meshNodes;
        Require(raw->nodes[i].skin==&skin && Near(World(&raw->nodes[i]),XMMatrixIdentity()),"Unsupported mesh node transform.");
    }
    Require(meshNodes==1 && raw->meshes[0].primitives_count<=64,"Unsupported character mesh.");
    for (std::size_t index=0;index<raw->meshes[0].primitives_count;++index) {
        const auto& p=raw->meshes[0].primitives[index];
        Require(p.type==cgltf_primitive_type_triangles && !p.has_draco_mesh_compression && !p.targets_count,"Unsupported mesh primitive.");
        const auto* positions=cgltf_find_accessor(&p,cgltf_attribute_type_position,0);
        const auto* normals=cgltf_find_accessor(&p,cgltf_attribute_type_normal,0);
        const auto* joints=cgltf_find_accessor(&p,cgltf_attribute_type_joints,0);
        const auto* weights=cgltf_find_accessor(&p,cgltf_attribute_type_weights,0);
        Require(positions && normals && joints && weights && p.indices && positions->count==normals->count
            && positions->count==joints->count && positions->count==weights->count
            && positions->count>0 && positions->count<100000 && p.indices->count%3==0 && p.indices->count<300000,
            "Incomplete character vertex data.");
        Require(positions->type==cgltf_type_vec3 && normals->type==cgltf_type_vec3
            && joints->type==cgltf_type_vec4 && weights->type==cgltf_type_vec4
            && p.indices->type==cgltf_type_scalar && !p.indices->is_sparse && p.indices->count>0,
            "Unsupported vertex accessor shape.");
        MeshPrimitive primitive{static_cast<std::uint32_t>(asset->indices.size()),static_cast<std::uint32_t>(p.indices->count)};
        if (p.material) {
            Require(p.material->has_pbr_metallic_roughness && !p.material->pbr_metallic_roughness.base_color_texture.texture,
                "Character materials must use flat base colors.");
            const auto* c=p.material->pbr_metallic_roughness.base_color_factor;
            primitive.color={c[0],c[1],c[2],c[3]};
        }
        const auto offset=static_cast<std::uint32_t>(asset->vertices.size());
        for (std::size_t vertex=0;vertex<positions->count;++vertex) {
            SkinVertex v;
            Read(positions,vertex,&v.position.x,3); Read(normals,vertex,&v.normal.x,3);
            Read(weights,vertex,v.weights.data(),4);
            Require(!joints->is_sparse && cgltf_accessor_read_uint(joints,vertex,v.joints.data(),4),"Invalid skin joints.");
            float total=0;
            for (std::size_t i=0;i<4;++i) { Require(v.joints[i]<HumanoidPartCount && v.weights[i]>=0,"Invalid skin influence."); total+=v.weights[i]; }
            Require(total>.999f && total<1.001f,"Skin weights must sum to one.");
            for (auto& weight:v.weights) weight/=total;
            asset->vertices.push_back(v);
        }
        for (std::size_t i=0;i<p.indices->count;++i) {
            const auto vertex=cgltf_accessor_read_index(p.indices,i);
            Require(vertex<positions->count,"Mesh index out of range.");
            asset->indices.push_back(offset+static_cast<std::uint32_t>(vertex));
        }
        asset->primitives.push_back(primitive);
    }
    auto animations=std::make_shared<CharacterAnimations>();
    for (std::size_t i=0;i<raw->animations_count;++i) {
        const auto& animation=raw->animations[i];
        Require(animation.name,"Unnamed animation.");
        auto* clip=std::strcmp(animation.name,"Idle")==0 ? &animations->idle
            : std::strcmp(animation.name,"Punch")==0 ? &animations->punch
            : std::strcmp(animation.name,"Guard")==0 ? &animations->guard
            : std::strcmp(animation.name,"Walk")==0 ? &animations->walk
            : std::strcmp(animation.name,"Kick")==0 ? &animations->kick : nullptr;
        Require(clip && clip->keys.empty(),"Unknown or duplicate animation.");
        LoadClip(*raw,animation,*clip,bones);
    }
    asset->animations=animations;
    return asset;
}

std::array<DirectX::XMFLOAT4X4,HumanoidPartCount> CharacterAsset::SkinMatrices(const std::array<BodyPose,HumanoidPartCount>& poses) const {
    using namespace DirectX;
    std::array<XMFLOAT4X4,HumanoidPartCount> result;
    for (std::size_t joint=0;joint<HumanoidPartCount;++joint) {
        const auto& pose=poses[bodyForJoint[joint]];
        const auto& q=pose.rotation; const auto& p=pose.position;
        XMStoreFloat4x4(&result[joint],XMLoadFloat4x4(&skinFromBody[joint])
            * XMMatrixRotationQuaternion(XMVectorSet(q[0],q[1],q[2],q[3])) * XMMatrixTranslation(p[0],p[1],p[2]));
    }
    return result;
}
}
