#pragma once
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <string>
#include "physics/PhysicsWorld.h"
#include "render/DebugView.h"
#include "render/RenderScene.h"
#include "assets/CharacterAsset.h"

namespace pf {
class Renderer {
public:
    Renderer(HWND window, const std::filesystem::path& shaderDirectory, bool warp);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void Resize(unsigned width, unsigned height);
    void NewFrame();
    void SetCharacters(const std::array<std::shared_ptr<const CharacterAsset>,2>& characters);
    void Draw(const RenderScene& scene, const DebugView& debug,
        const std::filesystem::path& capture = {});
    void WaitIdle();
    [[nodiscard]] const std::string& AdapterName() const;
    [[nodiscard]] bool DebugLayerEnabled() const;
    [[nodiscard]] unsigned ValidationErrors() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
