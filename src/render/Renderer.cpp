#include "render/Renderer.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <imgui.h>
#include <imgui_impl_dx12.h>
#include <imgui_impl_win32.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

namespace pf {
namespace {
constexpr UINT FrameCount = 2;
constexpr DXGI_FORMAT ColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

void Check(HRESULT result, const char* action) {
    if (FAILED(result)) {
        std::ostringstream message;
        message << action << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
        throw std::runtime_error(message.str());
    }
}

struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Event() { if (!handle) throw std::runtime_error("CreateEvent failed."); }
    ~Event() { CloseHandle(handle); }
    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;
};

D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES p{};
    p.Type = type;
    p.CreationNodeMask = p.VisibleNodeMask = 1;
    return p;
}

D3D12_RESOURCE_DESC BufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

struct Vertex { XMFLOAT3 position; XMFLOAT3 normal; };
struct ObjectData { XMFLOAT4X4 world; XMFLOAT4X4 viewProjection; XMFLOAT4 color; XMFLOAT4 options; };
constexpr UINT ConstantStride = (sizeof(ObjectData) + 255) & ~255u;
constexpr UINT SkinStride = (sizeof(XMFLOAT4X4) * HumanoidPartCount + 255) & ~255u;
}

struct Renderer::Impl {
    ComPtr<IDXGIFactory6> factory;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12InfoQueue> infoQueue;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
    std::array<ComPtr<ID3D12Resource>, FrameCount> targets;
    ComPtr<ID3D12Resource> depth, vertices, indices, constants;
    std::array<ComPtr<ID3D12CommandAllocator>, FrameCount> allocators;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline, debugPipeline, wirePipeline;
    ComPtr<ID3D12PipelineState> skinPipeline;
    ComPtr<ID3D12Resource> skinConstants;
    std::byte* mappedSkin = nullptr;
    struct CharacterMesh {
        std::shared_ptr<const CharacterAsset> asset;
        ComPtr<ID3D12Resource> vertices, indices;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        D3D12_INDEX_BUFFER_VIEW indexView{};
    };
    std::array<CharacterMesh,2> characters;
    ComPtr<ID3D12Resource> sphereVertices, sphereIndices;
    D3D12_VERTEX_BUFFER_VIEW sphereVertexView{};
    D3D12_INDEX_BUFFER_VIEW sphereIndexView{};
    UINT sphereIndexCount = 0;
    ComPtr<ID3D12Fence> fence;
    Event fenceEvent;
    UINT64 fenceValue = 0;
    UINT rtvStride = 0;
    UINT width = 1280, height = 800;
    UINT indexCount = 0;
    std::byte* mappedConstants = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    D3D12_INDEX_BUFFER_VIEW indexView{};
    std::string adapterName;
    bool debugLayer = false;
    bool imguiContext = false, imguiWin32 = false, imguiDx12 = false;

    ~Impl() {
        try { if (queue && fence) WaitIdle(); } catch (...) {}
        if (imguiDx12) ImGui_ImplDX12_Shutdown();
        if (imguiWin32) ImGui_ImplWin32_Shutdown();
        if (imguiContext) ImGui::DestroyContext();
        if (mappedConstants) constants->Unmap(0, nullptr);
        if (mappedSkin) skinConstants->Unmap(0,nullptr);
    }

    void WaitIdle() {
        Check(queue->Signal(fence.Get(), ++fenceValue), "Signal fence");
        if (fence->GetCompletedValue() < fenceValue) {
            Check(fence->SetEventOnCompletion(fenceValue, fenceEvent.handle), "Set fence event");
            if (WaitForSingleObject(fenceEvent.handle, 10000) != WAIT_OBJECT_0)
                throw std::runtime_error("GPU fence wait failed or timed out.");
        }
    }

    ComPtr<ID3D12DescriptorHeap> CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible = false) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type;
        desc.NumDescriptors = count;
        desc.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ComPtr<ID3D12DescriptorHeap> heap;
        Check(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&heap)), "Create descriptor heap");
        return heap;
    }

    ComPtr<ID3D12Resource> CreateBuffer(UINT64 bytes, D3D12_HEAP_TYPE type,
        D3D12_RESOURCE_STATES state, const void* data = nullptr) {
        const auto heap = Heap(type);
        const auto desc = BufferDesc(bytes);
        ComPtr<ID3D12Resource> resource;
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
            IID_PPV_ARGS(&resource)), "Create buffer");
        if (data) {
            void* mapped = nullptr;
            const D3D12_RANGE noRead{0, 0};
            Check(resource->Map(0, &noRead, &mapped), "Map upload buffer");
            std::memcpy(mapped, data, static_cast<size_t>(bytes));
            resource->Unmap(0, nullptr);
        }
        return resource;
    }

    void CreateTargets() {
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < FrameCount; ++i) {
            Check(swap->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "Get back buffer");
            device->CreateRenderTargetView(targets[i].Get(), nullptr, rtv);
            rtv.ptr += rtvStride;
        }
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DepthFormat;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{};
        clear.Format = DepthFormat;
        clear.DepthStencil.Depth = 1.0f;
        const auto heap = Heap(D3D12_HEAP_TYPE_DEFAULT);
        Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth)), "Create depth buffer");
        device->CreateDepthStencilView(depth.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    void Initialize(HWND window, const std::filesystem::path& shaderDirectory, bool warp) {
#ifdef _DEBUG
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
            debug->EnableDebugLayer();
            debugLayer = true;
        }
#endif
        Check(CreateDXGIFactory2(debugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;
        if (!warp) {
            for (UINT i = 0; ; ++i) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT result = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&candidate));
                if (result == DXGI_ERROR_NOT_FOUND) break;
                Check(result, "Enumerate GPU");
                DXGI_ADAPTER_DESC1 desc{};
                Check(candidate->GetDesc1(&desc), "Read GPU description");
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(candidate.Get(),
                    D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
                    adapter = candidate;
                    break;
                }
            }
        }
        if (!device) {
            Check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "Get WARP adapter");
            Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)), "Create WARP device");
        }
        DXGI_ADAPTER_DESC1 adapterDesc{};
        Check(adapter->GetDesc1(&adapterDesc), "Read adapter");
        char adapterText[256]{};
        WideCharToMultiByte(CP_UTF8, 0, adapterDesc.Description, -1, adapterText, sizeof(adapterText), nullptr, nullptr);
        adapterName = adapterText;
        if (debugLayer) Check(device.As(&infoQueue), "Get DX12 validation queue");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)), "Create command queue");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Create fence");
        RECT client{};
        GetClientRect(window, &client);
        width = static_cast<UINT>(client.right);
        height = static_cast<UINT>(client.bottom);
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = width;
        swapDesc.Height = height;
        swapDesc.Format = ColorFormat;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = FrameCount;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initialSwap;
        Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &swapDesc, nullptr, nullptr, &initialSwap), "Create swap chain");
        Check(initialSwap.As(&swap), "Get swap chain interface");
        Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER), "Configure window association");
        rtvHeap = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FrameCount);
        dsvHeap = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
        srvHeap = CreateHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);
        rtvStride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CreateTargets();
        for (auto& allocator : allocators)
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)), "Create command allocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[0].Get(), nullptr,
            IID_PPV_ARGS(&commands)), "Create command list");
        Check(commands->Close(), "Close initial command list");
        CreatePipeline(shaderDirectory / "Primitive.hlsl");
        CreateGeometry();
        constants = CreateBuffer(ConstantStride * 160, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        const D3D12_RANGE noRead{0, 0};
        Check(constants->Map(0, &noRead, reinterpret_cast<void**>(&mappedConstants)), "Map constants");
        skinConstants = CreateBuffer(SkinStride * 2,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        Check(skinConstants->Map(0,&noRead,reinterpret_cast<void**>(&mappedSkin)),"Map skin constants");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContext = true;
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        auto& style = ImGui::GetStyle();
        style.WindowRounding = 8;
        style.FrameRounding = 4;
        style.WindowPadding = ImVec2(16, 16);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.075f, 0.10f, 0.97f);
        imguiWin32 = ImGui_ImplWin32_Init(window);
        if (!imguiWin32) throw std::runtime_error("ImGui Win32 initialization failed.");
        ImGui_ImplDX12_InitInfo info;
        info.Device = device.Get();
        info.CommandQueue = queue.Get();
        info.NumFramesInFlight = FrameCount;
        info.RTVFormat = ColorFormat;
        info.DSVFormat = DepthFormat;
        info.SrvDescriptorHeap = srvHeap.Get();
        info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* init, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
            D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            *cpu = init->SrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
            *gpu = init->SrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
        };
        info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
        imguiDx12 = ImGui_ImplDX12_Init(&info);
        if (!imguiDx12) throw std::runtime_error("ImGui DX12 initialization failed.");
    }

    void CreatePipeline(const std::filesystem::path& shaderPath) {
        D3D12_ROOT_PARAMETER parameters[2]{};
        for (UINT i=0;i<2;++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].Descriptor.ShaderRegister = i;
            parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_ROOT_SIGNATURE_DESC rootDesc{};
        rootDesc.NumParameters = 2;
        rootDesc.pParameters = parameters;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> serialized, errors;
        Check(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "Serialize root signature");
        Check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&root)), "Create root signature");
        auto compile = [&](const char* entry, const char* profile) {
            ComPtr<ID3DBlob> shader, diagnostics;
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
#ifdef _DEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            const HRESULT result = D3DCompileFromFile(shaderPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                entry, profile, flags, 0, &shader, &diagnostics);
            if (FAILED(result) && diagnostics)
                throw std::runtime_error(std::string(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize()));
            Check(result, "Compile primitive shader");
            return shader;
        };
        const auto vs = compile("VSMain", "vs_5_0");
        const auto ps = compile("PSMain", "ps_5_0");
        const D3D12_INPUT_ELEMENT_DESC layout[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
        };
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        desc.InputLayout = {layout, 2};
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.DepthClipEnable = TRUE;
        auto& blend = desc.BlendState.RenderTarget[0];
        blend.SrcBlend = D3D12_BLEND_ONE;
        blend.DestBlend = D3D12_BLEND_ZERO;
        blend.BlendOp = D3D12_BLEND_OP_ADD;
        blend.SrcBlendAlpha = D3D12_BLEND_ONE;
        blend.DestBlendAlpha = D3D12_BLEND_ZERO;
        blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blend.LogicOp = D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        desc.DepthStencilState.DepthEnable = TRUE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        desc.DepthStencilState.FrontFace = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
        desc.DepthStencilState.BackFace = desc.DepthStencilState.FrontFace;
        desc.SampleMask = UINT_MAX;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = ColorFormat;
        desc.DSVFormat = DepthFormat;
        desc.SampleDesc.Count = 1;
        Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)), "Create graphics pipeline");
        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&debugPipeline)), "Create skeleton pipeline");
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        Check(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&wirePipeline)), "Create hitbox pipeline");
        const auto skinVS=compile("VSSkin","vs_5_0");
        const D3D12_INPUT_ELEMENT_DESC skinLayout[] = {
            {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"BLENDINDICES",0,DXGI_FORMAT_R32G32B32A32_UINT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
            {"BLENDWEIGHT",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
        };
        static_assert(sizeof(SkinVertex)==56);
        desc.VS={skinVS->GetBufferPointer(),skinVS->GetBufferSize()};
        desc.InputLayout={skinLayout,4};
        desc.DepthStencilState.DepthEnable=TRUE;
        desc.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
        desc.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
        Check(device->CreateGraphicsPipelineState(&desc,IID_PPV_ARGS(&skinPipeline)),"Create skin pipeline");
    }

    void SetCharacters(const std::array<std::shared_ptr<const CharacterAsset>,2>& assets) {
        WaitIdle();
        for (std::size_t i=0;i<2;++i) {
            if (!assets[i] || assets[i]->vertices.empty() || assets[i]->indices.empty())
                throw std::invalid_argument("Missing character mesh.");
            auto& mesh=characters[i];
            mesh.asset=assets[i];
            const auto vertexBytes=static_cast<UINT>(mesh.asset->vertices.size()*sizeof(SkinVertex));
            const auto indexBytes=static_cast<UINT>(mesh.asset->indices.size()*sizeof(std::uint32_t));
            mesh.vertices=CreateBuffer(vertexBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,mesh.asset->vertices.data());
            mesh.indices=CreateBuffer(indexBytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ,mesh.asset->indices.data());
            mesh.vertexView={mesh.vertices->GetGPUVirtualAddress(),vertexBytes,sizeof(SkinVertex)};
            mesh.indexView={mesh.indices->GetGPUVirtualAddress(),indexBytes,DXGI_FORMAT_R32_UINT};
        }
    }

    void CreateGeometry() {
        const std::array<XMFLOAT3, 8> corners{{{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},
            {0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},{-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},
            {0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}}};
        const unsigned faces[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},{1,5,6,2},{3,2,6,7},{4,5,1,0}};
        const XMFLOAT3 normals[] = {{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}};
        std::vector<Vertex> mesh;
        std::vector<std::uint16_t> elements;
        for (unsigned face = 0; face < 6; ++face) {
            const auto offset = static_cast<std::uint16_t>(mesh.size());
            for (unsigned corner : faces[face]) mesh.push_back({corners[corner], normals[face]});
            for (unsigned index : {0u, 1u, 2u, 0u, 2u, 3u}) elements.push_back(static_cast<std::uint16_t>(offset + index));
        }
        const UINT vbBytes = static_cast<UINT>(mesh.size() * sizeof(Vertex));
        const UINT ibBytes = static_cast<UINT>(elements.size() * sizeof(std::uint16_t));
        vertices = CreateBuffer(vbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, mesh.data());
        indices = CreateBuffer(ibBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, elements.data());
        vertexView = {vertices->GetGPUVirtualAddress(), vbBytes, sizeof(Vertex)};
        indexView = {indices->GetGPUVirtualAddress(), ibBytes, DXGI_FORMAT_R16_UINT};
        indexCount = static_cast<UINT>(elements.size());
        mesh.clear();
        elements.clear();
        constexpr unsigned slices = 12, rings = 8;
        for (unsigned y = 0; y <= rings; ++y) {
            const float phi = XM_PI * y / rings;
            for (unsigned x = 0; x <= slices; ++x) {
                const float theta = XM_2PI * x / slices;
                const XMFLOAT3 normal{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
                mesh.push_back({{normal.x * .5f, normal.y * .5f, normal.z * .5f}, normal});
            }
        }
        for (unsigned y = 0; y < rings; ++y) for (unsigned x = 0; x < slices; ++x) {
            const unsigned a = y * (slices + 1) + x;
            const unsigned b = a + slices + 1;
            for (unsigned index : {a,b,a+1,a+1,b,b+1}) elements.push_back(static_cast<std::uint16_t>(index));
        }
        const UINT sphereVbBytes = static_cast<UINT>(mesh.size() * sizeof(Vertex));
        const UINT sphereIbBytes = static_cast<UINT>(elements.size() * sizeof(std::uint16_t));
        sphereVertices = CreateBuffer(sphereVbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, mesh.data());
        sphereIndices = CreateBuffer(sphereIbBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, elements.data());
        sphereVertexView = {sphereVertices->GetGPUVirtualAddress(), sphereVbBytes, sizeof(Vertex)};
        sphereIndexView = {sphereIndices->GetGPUVirtualAddress(), sphereIbBytes, DXGI_FORMAT_R16_UINT};
        sphereIndexCount = static_cast<UINT>(elements.size());
    }

    void Draw(const RenderScene& scene, const DebugView& debug, const std::filesystem::path& capture) {
        const UINT frame = swap->GetCurrentBackBufferIndex();
        Check(allocators[frame]->Reset(), "Reset command allocator");
        Check(commands->Reset(allocators[frame].Get(), pipeline.Get()), "Reset command list");
        auto barrier = Transition(targets[frame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        commands->ResourceBarrier(1, &barrier);
        auto rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += static_cast<SIZE_T>(frame) * rtvStride;
        const auto dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        const float background[] = {0.035f, 0.05f, 0.075f, 1};
        commands->ClearRenderTargetView(rtv, background, 0, nullptr);
        commands->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0, nullptr);
        commands->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width), static_cast<float>(height), 0, 1};
        const D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        commands->RSSetViewports(1, &viewport);
        commands->RSSetScissorRects(1, &scissor);
        commands->SetGraphicsRootSignature(root.Get());
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0, 1, &vertexView);
        commands->IASetIndexBuffer(&indexView);
        const XMMATRIX view = scene.fighterCount == 2
            ? XMMatrixLookAtLH(XMVectorSet(6,3,1.5f,1), XMVectorSet(.9f,1.1f,-.5f,1), XMVectorSet(0,1,0,0))
            : XMMatrixLookAtLH(XMVectorSet(4,3.2f,-6,1), XMVectorSet(0,1.1f,0,1), XMVectorSet(0,1,0,0));
        const XMMATRIX projection = XMMatrixPerspectiveFovLH(XMConvertToRadians(48), static_cast<float>(width) / height, 0.1f, 100);
        ObjectData data{};
        XMStoreFloat4x4(&data.viewProjection, view * projection);
        auto drawObject = [&](UINT index, FXMMATRIX world, XMFLOAT4 color, bool ground, bool unlit = false, bool sphere = false) {
            if (index >= 160) throw std::runtime_error("Scene constant buffer capacity exceeded.");
            XMStoreFloat4x4(&data.world, world);
            data.color = color;
            data.options = {ground ? 1.0f : 0.0f, unlit ? 1.0f : 0.0f, 0, 0};
            std::memcpy(mappedConstants + index * ConstantStride, &data, sizeof(data));
            commands->SetGraphicsRootConstantBufferView(0, constants->GetGPUVirtualAddress() + index * ConstantStride);
            commands->IASetVertexBuffers(0, 1, sphere ? &sphereVertexView : &vertexView);
            commands->IASetIndexBuffer(sphere ? &sphereIndexView : &indexView);
            commands->DrawIndexedInstanced(sphere ? sphereIndexCount : indexCount, 1, 0, 0, 0);
        };
        drawObject(0, XMMatrixScaling(20, 0.5f, 20) * XMMatrixTranslation(0, -0.25f, 0), {0.16f, 0.21f, 0.26f, 1}, true);
        const auto& q = scene.box.rotation;
        const auto& p = scene.box.position;
        if (scene.fighterCount == 1)
            drawObject(1, XMMatrixRotationQuaternion(XMVectorSet(q[0], q[1], q[2], q[3])) * XMMatrixTranslation(p[0], p[1], p[2]),
                {0.96f, 0.48f, 0.19f, 1}, false);
        UINT debugIndex = 2;
        for (std::size_t fighter = 0; fighter < scene.fighterCount; ++fighter)
        for (UINT i = 0; debug.bodies && !debug.models && i < HumanoidPartCount; ++i) {
            const auto& pose = scene.bodies[fighter][i];
            const auto& size = HumanoidParts[i].halfExtent;
            const auto& rotation = pose.rotation;
            const auto& position = pose.position;
            const XMFLOAT4 color = fighter == 1 && i >= 1 && i <= 6 && i != 2 ? XMFLOAT4{.8f,.19f,.16f,1}
                : i == 2 ? XMFLOAT4{.88f,.67f,.45f,1}
                : i == 0 || i >= 7 ? XMFLOAT4{.16f,.27f,.36f,1} : XMFLOAT4{.82f,.87f,.85f,1};
            drawObject(debugIndex++, XMMatrixScaling(2 * size[0], 2 * size[1], 2 * size[2])
                * XMMatrixRotationQuaternion(XMVectorSet(rotation[0],rotation[1],rotation[2],rotation[3]))
                * XMMatrixTranslation(position[0],position[1],position[2]), color, false);
        }
        if (debug.bodies && debug.models) {
            commands->SetPipelineState(skinPipeline.Get());
            for (std::size_t fighter=0;fighter<scene.fighterCount;++fighter) {
                const auto& mesh=characters[fighter];
                if (!mesh.asset) throw std::runtime_error("Character mesh was not initialized.");
                const auto matrices=mesh.asset->SkinMatrices(scene.bodies[fighter]);
                std::memcpy(mappedSkin+fighter*SkinStride,matrices.data(),sizeof(matrices));
                commands->SetGraphicsRootConstantBufferView(1,skinConstants->GetGPUVirtualAddress()+fighter*SkinStride);
                commands->IASetVertexBuffers(0,1,&mesh.vertexView);
                commands->IASetIndexBuffer(&mesh.indexView);
                for (const auto& primitive:mesh.asset->primitives) {
                    if (debugIndex>=160) throw std::runtime_error("Object constant capacity exceeded.");
                    ObjectData skinData{};
                    XMStoreFloat4x4(&skinData.world,XMMatrixIdentity());
                    XMStoreFloat4x4(&skinData.viewProjection,view * projection);
                    skinData.color=primitive.color;
                    skinData.options.z=1;
                    std::memcpy(mappedConstants+debugIndex*ConstantStride,&skinData,sizeof(skinData));
                    commands->SetGraphicsRootConstantBufferView(0,constants->GetGPUVirtualAddress()+debugIndex*ConstantStride);
                    ++debugIndex;
                    commands->DrawIndexedInstanced(primitive.indexCount,1,primitive.firstIndex,0,0);
                }
            }
        }
        commands->SetPipelineState(debugPipeline.Get());
        auto skeleton = [&](const std::array<BodyPose, HumanoidPartCount>& poses, float offset, XMFLOAT4 color) {
            for (std::size_t i = 0; i < HumanoidPartCount; ++i) {
                const auto& p = poses[i].position;
                const XMVECTOR end = XMVectorSet(p[0] + offset, p[1], p[2], 0);
                drawObject(debugIndex++, XMMatrixScaling(.045f,.045f,.045f) * XMMatrixTranslationFromVector(end), color, false, true);
                if (i == 0) continue;
                const auto& parent = poses[HumanoidParts[i].parent].position;
                const XMVECTOR start = XMVectorSet(parent[0] + offset, parent[1], parent[2], 0);
                const XMVECTOR direction = XMVectorSubtract(end, start);
                const float length = XMVectorGetX(XMVector3Length(direction));
                if (length < .0001f) continue;
                const XMVECTOR normal = XMVectorScale(direction, 1.0f / length);
                const float dot = XMVectorGetY(normal);
                const XMVECTOR rotation = dot < -.9999f ? XMVectorSet(1,0,0,0)
                    : XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(XMVectorSet(0,1,0,0), normal), 1 + dot));
                drawObject(debugIndex++, XMMatrixScaling(.015f,length,.015f) * XMMatrixRotationQuaternion(rotation)
                    * XMMatrixTranslationFromVector(XMVectorScale(XMVectorAdd(start,end),.5f)), color, false, true);
            }
        };
        for (std::size_t fighter = 0; fighter < scene.fighterCount; ++fighter) {
            if (debug.physicalSkeleton) skeleton(scene.bodies[fighter], 0, {.15f,.8f,1,1});
            if (debug.targetSkeleton) skeleton(scene.targets[fighter], debug.offsetTarget ? 1.8f : 0, {.55f,1,.25f,1});
        }
        commands->SetPipelineState(wirePipeline.Get());
        auto sphere = [&](const Sphere& shape, XMFLOAT4 color) {
            const auto& center = shape.center;
            const float diameter = shape.radius * 2;
            drawObject(debugIndex++, XMMatrixScaling(diameter,diameter,diameter)
                * XMMatrixTranslation(center[0],center[1],center[2]), color, false, true, true);
        };
        for (std::size_t fighter = 0; fighter < scene.fighterCount; ++fighter) {
            if (debug.hurtboxes)
                for (const auto& hurt : CombatSystem::HurtSpheres(scene.bodies[fighter])) sphere(hurt, {.4f,.7f,1,1});
            if (debug.hitboxes && scene.hitboxActive[fighter]) sphere(CombatSystem::PunchSphere(scene.bodies[fighter]), {1,.3f,.1f,1});
        }
        ID3D12DescriptorHeap* heaps[] = {srvHeap.Get()};
        commands->SetPipelineState(debugPipeline.Get());
        for (const auto& reaction : scene.reactions) {
            if (!debug.impacts || reaction.visibleFrames <= 0) continue;
            const auto& impactPosition = reaction.hit.position;
            const auto& impulse = reaction.impulse;
            const XMFLOAT4 color = reaction.hit.guarded ? XMFLOAT4{.2f,1,.8f,1} : XMFLOAT4{1,1,.1f,1};
            const auto start = XMVectorSet(impactPosition[0],impactPosition[1],impactPosition[2],0);
            drawObject(debugIndex++, XMMatrixScaling(.065f,.065f,.065f) * XMMatrixTranslationFromVector(start), color, false, true);
            const auto vector = XMVectorSet(impulse[0],impulse[1],impulse[2],0);
            const float length = XMVectorGetX(XMVector3Length(vector)) * .025f;
            if (length < .0001f) continue;
            const auto normal = XMVector3Normalize(vector);
            const float dot = XMVectorGetY(normal);
            const auto rotation = dot < -.9999f ? XMVectorSet(1,0,0,0)
                : XMQuaternionNormalize(XMVectorSetW(XMVector3Cross(XMVectorSet(0,1,0,0),normal),1+dot));
            drawObject(debugIndex++, XMMatrixScaling(.025f,length,.025f) * XMMatrixRotationQuaternion(rotation)
                * XMMatrixTranslationFromVector(XMVectorAdd(start,XMVectorScale(normal,length*.5f))), color, false, true);
            drawObject(debugIndex++, XMMatrixScaling(.045f,.045f,.045f)
                * XMMatrixTranslationFromVector(XMVectorAdd(start,XMVectorScale(normal,length))), color, false, true);
        }
        commands->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.Get());

        ComPtr<ID3D12Resource> readback;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT64 captureBytes = 0;
        if (!capture.empty()) {
            const auto desc = targets[frame]->GetDesc();
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &captureBytes);
            readback = CreateBuffer(captureBytes, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
            barrier = Transition(targets[frame].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION source{}, dest{};
            source.pResource = targets[frame].Get();
            source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dest.pResource = readback.Get();
            dest.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dest.PlacedFootprint = footprint;
            commands->CopyTextureRegion(&dest, 0, 0, 0, &source, nullptr);
        }
        barrier = Transition(targets[frame].Get(), capture.empty() ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_PRESENT);
        commands->ResourceBarrier(1, &barrier);
        Check(commands->Close(), "Close draw commands");
        ID3D12CommandList* lists[] = {commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        Check(swap->Present(1, 0), "Present frame");
        // 初期版は1フレームごとに待ち、共有定数バッファの上書きを防ぐ。
        WaitIdle();
        if (readback) SaveCapture(readback.Get(), footprint, captureBytes, capture);
    }

    void SaveCapture(ID3D12Resource* readback, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& footprint,
        UINT64 bytes, const std::filesystem::path& path) {
        std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4);
        void* mapped = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
        Check(readback->Map(0, &range, &mapped), "Map screenshot");
        const auto* source = static_cast<const unsigned char*>(mapped) + footprint.Offset;
        for (UINT y = 0; y < height; ++y) {
            const auto* row = source + static_cast<size_t>(y) * footprint.Footprint.RowPitch;
            for (UINT x = 0; x < width; ++x) {
                const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
                pixels[offset] = row[x * 4 + 2];
                pixels[offset + 1] = row[x * 4 + 1];
                pixels[offset + 2] = row[x * 4];
                pixels[offset + 3] = 255;
            }
        }
        const D3D12_RANGE noWrite{0, 0};
        readback->Unmap(0, &noWrite);
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42;
        file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
        BITMAPINFOHEADER info{};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(width);
        info.biHeight = -static_cast<LONG>(height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        info.biCompression = BI_RGB;
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(&file), sizeof(file));
        output.write(reinterpret_cast<const char*>(&info), sizeof(info));
        output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
        if (!output) throw std::runtime_error("Could not save screenshot.");
    }
};

Renderer::Renderer(HWND window, const std::filesystem::path& shaderDirectory, bool warp) : impl_(std::make_unique<Impl>()) {
    impl_->Initialize(window, shaderDirectory, warp);
}
Renderer::~Renderer() = default;
void Renderer::WaitIdle() { impl_->WaitIdle(); }
void Renderer::Resize(unsigned width, unsigned height) {
    if (!width || !height || (width == impl_->width && height == impl_->height)) return;
    impl_->WaitIdle();
    for (auto& target : impl_->targets) target.Reset();
    impl_->depth.Reset();
    Check(impl_->swap->ResizeBuffers(FrameCount, width, height, ColorFormat, 0), "Resize swap chain");
    impl_->width = width;
    impl_->height = height;
    impl_->CreateTargets();
}
void Renderer::NewFrame() { ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); }
void Renderer::SetCharacters(const std::array<std::shared_ptr<const CharacterAsset>,2>& characters) { impl_->SetCharacters(characters); }
void Renderer::Draw(const RenderScene& scene, const DebugView& debug,
    const std::filesystem::path& capture) { impl_->Draw(scene, debug, capture); }
const std::string& Renderer::AdapterName() const { return impl_->adapterName; }
bool Renderer::DebugLayerEnabled() const { return impl_->debugLayer; }
unsigned Renderer::ValidationErrors() const {
    if (!impl_->infoQueue) return 0;
    unsigned count = 0;
    for (UINT64 i = 0; i < impl_->infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0;
        Check(impl_->infoQueue->GetMessage(i, nullptr, &size), "Read validation message size");
        std::vector<std::byte> storage(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        Check(impl_->infoQueue->GetMessage(i, message, &size), "Read validation message");
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            ++count;
            OutputDebugStringA(message->pDescription);
        }
    }
    return count;
}
}
