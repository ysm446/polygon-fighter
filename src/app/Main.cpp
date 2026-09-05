#include <Windows.h>
#include <shellapi.h>
#include "app/FixedClock.h"
#include "physics/PhysicsWorld.h"
#include "render/Renderer.h"
#include "debug/DebugUI.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
struct WindowState {
    unsigned width = 1280, height = 800;
    bool resized = false;
    bool minimized = false;
    bool pause = false, reset = false, step = false, impulse = false;
    bool pushHumanoid = false;
    bool punch = false;
    bool opponentPunch = false;
    std::array<bool,2> kick{};
    std::array<bool, 2> guardHeld{};
    std::array<bool,4> moveHeld{};
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state && message == WM_KEYUP) {
        if (wParam == 'G') state->guardHeld[0] = false;
        if (wParam == 'O') state->guardHeld[1] = false;
        if (wParam == 'A') state->moveHeld[0] = false;
        if (wParam == 'D') state->moveHeld[1] = false;
        if (wParam == VK_LEFT) state->moveHeld[2] = false;
        if (wParam == VK_RIGHT) state->moveHeld[3] = false;
    }
    if (state && message == WM_KILLFOCUS) { state->guardHeld = {}; state->moveHeld = {}; }
    if (ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return 1;
    switch (message) {
    case WM_SIZE:
        if (state) {
            state->width = LOWORD(lParam);
            state->height = HIWORD(lParam);
            state->minimized = wParam == SIZE_MINIMIZED;
            state->resized = true;
        }
        return 0;
    case WM_DPICHANGED: {
        const auto* rect = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_KEYDOWN:
        if (state && !(lParam & (1LL << 30)) && (!ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureKeyboard)) {
            if (wParam == VK_SPACE) state->pause = true;
            if (wParam == 'R') state->reset = true;
            if (wParam == 'N') state->step = true;
            if (wParam == 'I') state->impulse = true;
            if (wParam == 'H') state->pushHumanoid = true;
            if (wParam == 'P') state->punch = true;
            if (wParam == 'K') state->opponentPunch = true;
            if (wParam == 'F') state->kick[0] = true;
            if (wParam == 'L') state->kick[1] = true;
            if (wParam == 'G') state->guardHeld[0] = true;
            if (wParam == 'O') state->guardHeld[1] = true;
            if (wParam == 'A') state->moveHeld[0] = true;
            if (wParam == 'D') state->moveHeld[1] = true;
            if (wParam == VK_LEFT) state->moveHeld[2] = true;
            if (wParam == VK_RIGHT) state->moveHeld[3] = true;
            if (wParam == VK_ESCAPE) PostMessageW(window, WM_CLOSE, 0, 0);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct Window {
    HINSTANCE instance;
    HWND handle = nullptr;
    static constexpr wchar_t ClassName[] = L"PolygonFighterWindow";
    explicit Window(HINSTANCE appInstance) : instance(appInstance) {
        WNDCLASSEXW desc{};
        desc.cbSize = sizeof(desc);
        desc.style = CS_HREDRAW | CS_VREDRAW;
        desc.lpfnWndProc = WindowProc;
        desc.hInstance = instance;
        desc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        desc.lpszClassName = ClassName;
        if (!RegisterClassExW(&desc)) throw std::runtime_error("Could not register window class.");
    }
    ~Window() {
        if (handle && IsWindow(handle)) DestroyWindow(handle);
        UnregisterClassW(ClassName, instance);
    }
    void Create(WindowState& state) {
        RECT rect{0, 0, 1280, 800};
        if (!AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0)) throw std::runtime_error("AdjustWindowRectEx failed.");
        handle = CreateWindowExW(0, ClassName, L"Polygon Fighter | Phase 7 - Kick",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            nullptr, nullptr, instance, &state);
        if (!handle) throw std::runtime_error("Could not create application window.");
    }
};

std::filesystem::path ExecutableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) throw std::runtime_error("Could not find executable directory.");
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

bool HasArgument(std::wstring_view argument) {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) throw std::runtime_error("Could not read command line.");
    bool found = false;
    for (int i = 1; i < count; ++i) if (argument == args[i]) found = true;
    LocalFree(args);
    return found;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    bool smoke = false;
    std::filesystem::path outputDirectory;
    try {
        const bool kickSmoke=HasArgument(L"--smoke-kick");
        const bool movementSmoke=HasArgument(L"--smoke-movement");
        smoke = HasArgument(L"--smoke-test") || movementSmoke || kickSmoke;
        const bool warp = HasArgument(L"--warp");
        outputDirectory = ExecutableDirectory();
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WindowState windowState;
        Window window(instance);
        window.Create(windowState);
        pf::PhysicsWorld physics(!HasArgument(L"--physics-lab"));
        pf::Renderer renderer(window.handle, outputDirectory / "shaders", warp);
        const std::array<std::shared_ptr<const pf::CharacterAsset>,2> characters{
            pf::CharacterAsset::Load(outputDirectory / "assets/characters/male-fighter.glb"),
            pf::CharacterAsset::Load(outputDirectory / "assets/characters/female-fighter.glb")};
        renderer.SetCharacters(characters);
        for (std::size_t i=0;i<(physics.IsCombatScene()?2u:1u);++i) physics.SetAnimations(i,characters[i]->animations);
        pf::DebugState debug;
        if (physics.IsCombatScene()) { debug.view.targetSkeleton = false; debug.view.physicalSkeleton = false; }
        pf::FixedClock clock;
        ShowWindow(window.handle, smoke ? SW_SHOWNOACTIVATE : showCommand);
        using Clock = std::chrono::steady_clock;
        auto previous = Clock::now();
        const auto start = previous;
        unsigned frames = 0;
        unsigned resizeCount = 0;
        bool smokePunchStarted = false, smokePunchCaptured = false;
        bool smokeAnticipationCaptured = false, smokeRecoilCaptured = false;
        bool movementCaptured=false, rangeCaptured=false;
        std::uint64_t movementSteps=0;
        bool running = true;
        while (running) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) { running = false; break; }
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            if (!running) break;
            if (windowState.minimized) {
                clock.Reset();
                previous = Clock::now();
                WaitMessage();
                continue;
            }
            if (windowState.resized) {
                renderer.Resize(windowState.width, windowState.height);
                windowState.resized = false;
                ++resizeCount;
            }
            const auto now = Clock::now();
            const double elapsed = std::chrono::duration<double>(now - previous).count();
            previous = now;
            if (windowState.pause) { debug.paused = !debug.paused; windowState.pause = false; }
            if (windowState.reset) { debug.reset = true; windowState.reset = false; }
            if (windowState.step) { debug.singleStep = true; windowState.step = false; }
            if (windowState.impulse) { physics.PushBox(); windowState.impulse = false; }
            if (windowState.pushHumanoid) { physics.PushHumanoid(); windowState.pushHumanoid = false; }
            if (windowState.punch) { physics.RequestPunch(); windowState.punch = false; }
            if (windowState.opponentPunch) { physics.RequestPunch(1); windowState.opponentPunch = false; }
            for (std::size_t i=0;i<2;++i) if (windowState.kick[i]) {
                physics.RequestAttack(i,pf::AttackKind::Kick); windowState.kick[i]=false;
            }
            if (smoke && !movementSmoke && !smokePunchStarted && debug.steps >= 60) {
                physics.RequestAttack(0,kickSmoke ? pf::AttackKind::Kick : pf::AttackKind::Punch);
                smokePunchStarted = true;
            }

            renderer.NewFrame();
            pf::DrawDebugUI(physics, debug, renderer.AdapterName().c_str(), renderer.DebugLayerEnabled());
            if (physics.IsCombatScene())
                for (std::size_t i = 0; i < 2; ++i) {
                    physics.SetGuard(i, windowState.guardHeld[i] || debug.guard[i]);
                    const bool captured=ImGui::GetIO().WantCaptureKeyboard;
                    physics.SetMove(i,captured ? 0.f : static_cast<float>(windowState.moveHeld[i*2+1])-static_cast<float>(windowState.moveHeld[i*2]));
                }
            if (debug.reset) {
                physics.ResetBox();
                physics.ResetHumanoid();
                clock.Reset();
                debug.steps = 0;
                debug.reset = false;
            } else {
                debug.steps += clock.Advance(elapsed, debug.paused, debug.singleStep, [&] {
                    if (movementSmoke) {
                        physics.SetMove(0,movementSteps<90 ? -1.f : movementSteps>=240 && movementSteps<360 ? 1.f : 0.f);
                        if (movementSteps==120 || movementSteps==480) physics.RequestPunch();
                        if (movementSteps==240 && physics.Combat().Fighter(1).hp!=100)
                            throw std::runtime_error("Retreat smoke punch must miss.");
                        ++movementSteps;
                    }
                    physics.Step();
                });
            }
            debug.singleStep = false;
            ImGui::Render();
            const bool finished = smoke && frames >= 120 && debug.steps >= (movementSmoke ? 660u : 240u);
            const bool captureAttack=smoke && (!movementSmoke || movementSteps>480);
            const bool capturePunch = captureAttack && !smokePunchCaptured && physics.Attacking() && physics.AnimationTime() >= (kickSmoke ? .55 : .4);
            const bool captureAnticipation = captureAttack && !smokeAnticipationCaptured && physics.Attacking() && physics.AnimationTime() >= (kickSmoke ? .32 : .20);
            const bool captureRecoil = captureAttack && !smokeRecoilCaptured && physics.Attacking() && physics.AnimationTime() >= (kickSmoke ? .78 : .65);
            const bool captureMovement=movementSmoke && !movementCaptured && movementSteps>=45;
            const bool captureRange=movementSmoke && !rangeCaptured && movementSteps>=180;
            pf::RenderScene scene;
            scene.box = physics.BoxPose();
            scene.reactions = physics.HitReactions();
            scene.fighterCount = physics.IsCombatScene() ? 2 : 1;
            for (std::size_t i = 0; i < scene.fighterCount; ++i) {
                scene.bodies[i] = physics.HumanoidPoses(i);
                scene.targets[i] = physics.TargetPoses(i);
                scene.attacks[i] = physics.Combat().Fighter(i).attack;
                scene.hitboxActive[i] = physics.IsCombatScene() && physics.LastCombatFrame().active[i];
            }
            renderer.Draw(scene, debug.view,
                captureMovement ? outputDirectory / "smoke-movement.bmp"
                : captureRange ? outputDirectory / "smoke-range.bmp"
                : captureAnticipation ? outputDirectory / (kickSmoke ? "smoke-kick-anticipation.bmp" : "smoke-anticipation.bmp")
                : capturePunch ? outputDirectory / (kickSmoke ? "smoke-kick-impact.bmp" : "smoke-punch.bmp")
                : captureRecoil ? outputDirectory / (kickSmoke ? "smoke-kick-recoil.bmp" : "smoke-recoil.bmp")
                : finished ? outputDirectory / "smoke.bmp" : std::filesystem::path{});
            if (captureAnticipation) smokeAnticipationCaptured = true;
            if (captureMovement) movementCaptured=true;
            if (captureRange) rangeCaptured=true;
            if (capturePunch) smokePunchCaptured = true;
            if (captureRecoil) smokeRecoilCaptured = true;
            ++frames;
            if (smoke && (frames == 30 || frames == 60)) {
                const bool small = frames == 30;
                if (!SetWindowPos(window.handle, nullptr, 0, 0, small ? 1024 : 1296, small ? 720 : 839,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) throw std::runtime_error("Smoke resize failed.");
            }
            if (finished) {
                renderer.WaitIdle();
                const auto errors = renderer.ValidationErrors();
                const float height = physics.BoxPose().position[1];
                std::ofstream log(outputDirectory / "smoke.log");
                log << "version=" << APP_VERSION << "\nadapter=" << renderer.AdapterName()
                    << "\ndebug_layer=" << renderer.DebugLayerEnabled() << "\nframes=" << frames
                    << "\nphysics_steps=" << debug.steps << "\nbox_y=" << height
                    << "\nhumanoid_head_y=" << physics.HumanoidPoses()[2].position[1]
                    << "\nhumanoid_joint_error_deg=" << physics.HumanoidPoseError()
                    << "\npunch_captured=" << smokePunchCaptured << "\nreturned_to_idle=" << !physics.Attacking()
                    << "\np2_hp=" << physics.Combat().Fighter(1).hp
                    << "\nblender_models=" << debug.view.models
                    << "\nanticipation_captured=" << smokeAnticipationCaptured
                    << "\nrecoil_captured=" << smokeRecoilCaptured
                    << "\nkick_smoke=" << kickSmoke
                    << "\nmovement_smoke=" << movementSmoke << "\nmovement_captured=" << movementCaptured
                    << "\nfighter_distance=" << physics.FighterDistance()
                    << "\nmale_triangles=" << characters[0]->indices.size()/3
                    << "\nfemale_triangles=" << characters[1]->indices.size()/3
                    << "\nresizes=" << resizeCount << "\nvalidation_warnings_or_errors=" << errors << '\n';
                if (errors || (physics.IsCombatScene() && physics.Combat().Fighter(1).hp != (kickSmoke ? 82 : 88))
                    || !smokePunchCaptured || physics.Attacking() || std::abs(height - 0.5f) > 0.05f || resizeCount < 2
                    || physics.HumanoidPoses()[2].position[1] < 1.7f || physics.HumanoidPoseError() > 5.0f)
                    throw std::runtime_error("Smoke validation failed; see smoke.log.");
                return 0;
            }
            if (smoke && std::chrono::duration<double>(now - start).count() > 30)
                throw std::runtime_error("Smoke test did not finish within 30 seconds.");
        }
        return smoke ? 1 : 0;
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        if (!outputDirectory.empty()) {
            std::ofstream log(outputDirectory / "error.log");
            log << error.what() << '\n';
        }
        if (!smoke) MessageBoxA(nullptr, error.what(), "Polygon Fighter - Error", MB_OK | MB_ICONERROR);
        return 1;
    }
}
