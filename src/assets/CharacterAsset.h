#pragma once
#include "animation/AnimationPlayer.h"
#include <DirectXMath.h>
#include <filesystem>
#include <cstdint>
#include <memory>

namespace pf {
struct SkinVertex {
    DirectX::XMFLOAT3 position, normal;
    std::array<std::uint32_t,4> joints{};
    std::array<float,4> weights{};
};
struct MeshPrimitive {
    std::uint32_t firstIndex = 0, indexCount = 0;
    DirectX::XMFLOAT4 color{1,1,1,1};
};
struct CharacterAsset {
    std::vector<SkinVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<MeshPrimitive> primitives;
    std::array<std::size_t,HumanoidPartCount> bodyForJoint{};
    std::array<DirectX::XMFLOAT4X4,HumanoidPartCount> skinFromBody{};
    std::shared_ptr<const CharacterAnimations> animations;
    [[nodiscard]] std::array<DirectX::XMFLOAT4X4,HumanoidPartCount> SkinMatrices(
        const std::array<BodyPose,HumanoidPartCount>& poses) const;
    static std::shared_ptr<const CharacterAsset> Load(const std::filesystem::path& path);
};
}
