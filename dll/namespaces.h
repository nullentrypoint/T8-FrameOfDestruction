#pragma once
#include <string>
#include "TekkenBot/GameSnapshot.h"
#include "TekkenBot/ThrowTimeline.h"

namespace globals {
        extern HMODULE mainModule;
        extern HWND mainWindow;
        extern int uninjectKey;
        extern int openMenuKey;

        // Rendering backend currently in use
        enum class Backend {
                None,
                DX9,
                DX10,
                DX11,
                DX12,
                Vulkan
        };
        extern Backend activeBackend;
        // Preferred backend to hook. None means auto with fallback order
        extern Backend preferredBackend;
        extern bool enableDebugLog;
        extern const std::string appName;
        extern const std::string appVersion;
        void SetDebugLogging(bool enable);
}

namespace hooks {
        extern void Init();
}

namespace inputhook {
        extern void Init(HWND hWindow);
        extern void Remove(HWND hWindow);
        static LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}

namespace mousehooks {
        void Init();
        void Remove();
}

namespace d3d12hook {
        typedef HRESULT(STDMETHODCALLTYPE* PresentD3D12)(
                IDXGISwapChain3 * pSwapChain, UINT SyncInterval, UINT Flags);
        typedef HRESULT(STDMETHODCALLTYPE* Present1Fn)(
                IDXGISwapChain3 * pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);
        extern PresentD3D12 oPresentD3D12;
        extern Present1Fn   oPresent1D3D12;

	typedef void(STDMETHODCALLTYPE* ExecuteCommandListsFn)(
		ID3D12CommandQueue * _this, UINT NumCommandLists, ID3D12CommandList* const* ppCommandLists);
        extern ExecuteCommandListsFn oExecuteCommandListsD3D12;

        typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffersFn)(
                IDXGISwapChain3* pSwapChain,
                UINT BufferCount,
                UINT Width,
                UINT Height,
                DXGI_FORMAT NewFormat,
                UINT SwapChainFlags);
        extern ResizeBuffersFn oResizeBuffersD3D12;

        extern HRESULT STDMETHODCALLTYPE hookResizeBuffersD3D12(
                IDXGISwapChain3* pSwapChain,
                UINT BufferCount,
                UINT Width,
                UINT Height,
                DXGI_FORMAT NewFormat,
                UINT SwapChainFlags);

        extern long __fastcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
        extern long __fastcall hookPresent1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams);
        extern void STDMETHODCALLTYPE hookExecuteCommandListsD3D12(
                ID3D12CommandQueue* _this,
                UINT                          NumCommandLists,
                ID3D12CommandList* const* ppCommandLists);

        extern void release();
        bool IsInitialized();
}

// Forward declarations for other rendering backends
namespace d3d9hook {
    using EndSceneFn = HRESULT(__stdcall*)(IDirect3DDevice9*);
    using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);

    extern EndSceneFn oEndScene;
    extern ResetFn    oReset;

    HRESULT __stdcall hookEndScene(IDirect3DDevice9* device);
    HRESULT __stdcall hookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params);

    void Init();
    void release();
    bool IsInitialized();
}

namespace hooks_dx10 {
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    extern PresentFn       oPresentD3D10;
    extern Present1Fn      oPresent1D3D10;
    extern ResizeBuffersFn oResizeBuffersD3D10;

    HRESULT __stdcall hookPresentD3D10(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    HRESULT __stdcall hookPresent1D3D10(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
    HRESULT __stdcall hookResizeBuffersD3D10(
        IDXGISwapChain* pSwapChain,
        UINT BufferCount,
        UINT Width,
        UINT Height,
        DXGI_FORMAT NewFormat,
        UINT SwapChainFlags);

    void Init();
    void release();
    bool IsInitialized();
}

namespace hooks_dx11 {
    using PresentFn   = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using Present1Fn  = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    extern PresentFn       oPresentD3D11;
    extern Present1Fn      oPresent1D3D11;
    extern ResizeBuffersFn oResizeBuffersD3D11;

    HRESULT __stdcall hookPresentD3D11(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    HRESULT __stdcall hookPresent1D3D11(IDXGISwapChain1* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pPresentParameters);
    HRESULT __stdcall hookResizeBuffersD3D11(
        IDXGISwapChain* pSwapChain,
        UINT BufferCount,
        UINT Width,
        UINT Height,
        DXGI_FORMAT NewFormat,
        UINT SwapChainFlags);

    void Init();
    void release();
    bool IsInitialized();
}

namespace hooks_vk {
    extern PFN_vkCreateInstance        oCreateInstance;
    extern PFN_vkCreateDevice          oCreateDevice;
    extern PFN_vkQueuePresentKHR       oQueuePresentKHR;

    VkResult VKAPI_PTR hook_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkInstance* pInstance);
    VkResult VKAPI_PTR hook_vkCreateDevice(VkPhysicalDevice physicalDevice,
        const VkDeviceCreateInfo* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDevice* pDevice);
    VkResult VKAPI_PTR hook_vkQueuePresentKHR(VkQueue queue,
        const VkPresentInfoKHR* pPresentInfo);

    void Init();
    void release();
    bool IsInitialized();
}

struct HitRingSettings {
    static constexpr const char* types[5] = { "S.MID", "S.LOW", "MID", "HIGH", "LOW" };
    bool enabled = true;
    float thickness = 3.0f;
    float colors[5][3] = {
        { 102 / 255.f, 255 / 255.f, 153 / 255.f }, // Neon #66FF99
        { 57 / 255.f, 233 / 255.f, 255 / 255.f },  // #39E9FF
        { 255 / 255.f, 228 / 255.f, 92 / 255.f },  // #FFE45C
        { 255 / 255.f, 73 / 255.f, 118 / 255.f },  // #FF4976
        { 102 / 255.f, 133 / 255.f, 255 / 255.f }  // #6685FF
    };
};

struct FrameAdvantageHints {
    bool throwTimeline = true;
    int throwCardStyle = 0; // Detailed, Compact, Minimal
    float throwCardDuration = 5.0f;
    float throwCardFade = 1.0f;
    bool enabled = true;
    int startPositionMode = 1; // 0: random screen center, 1: contact / attack hitbox
    bool lockToWorld = true;
    float fontSize = 60.0f;
    bool fontShadow = true;
    float flySpeed = 1.0f;
    float fadeSpeed = 0.015f;
    float color0[3] = { 1.0f, 1.0f, 1.0f };                           // Neon #FFFFFF
    float colorLargePlus[3] = { 132 / 255.f, 149 / 255.f, 255 / 255.f }; // #8495FF
    float colorMediumPlus[3] = { 64 / 255.f, 222 / 255.f, 255 / 255.f }; // #40DEFF
    float colorSmallPlus[3] = { 122 / 255.f, 255 / 255.f, 213 / 255.f }; // #7AFFD5
    float colorLargeMinus[3] = { 255 / 255.f, 82 / 255.f, 122 / 255.f }; // #FF527A
    float colorMediumMinus[3] = { 255 / 255.f, 121 / 255.f, 46 / 255.f }; // #FF792E: punishable (-10..-14)
    float colorSmallMinus[3] = { 255 / 255.f, 224 / 255.f, 173 / 255.f }; // #FFE0AD: -1..-9
    bool debugInfo = false;
    HitRingSettings hitRing;
};

namespace menu {
    extern bool isOpen;
    extern void Init(ImGuiIO& io);
    extern void LoadSettings();
    extern FrameAdvantageHints frameAdvantageHints;
}

namespace overlay {
    void UpdateThrowTimeline(int side, const ThrowTimeline& timeline);
    void ClearThrowTimelines();
    extern bool isOpen;
    extern void Init(ImGuiIO& io, ImFont* font);
    extern uint64_t AddFlyingText(const std::string& text,
        const std::optional<ScreenAnchor>& anchor = std::nullopt,
        const std::string& hitType = "",
        const std::optional<WorldPoint>& worldOrigin = std::nullopt);
    extern void SetCamera(const std::optional<CameraSnapshot>& camera);
    extern void UpdateFlyingText(uint64_t id, const std::string& text);
}

namespace console {
    extern bool isOpen;
    extern void Init(ImGuiIO& io);
    extern void AddLog(const std::string& str);
    extern void AddLogError(const std::string& str);
    extern void DrawConsoleContent();

    extern std::shared_ptr<PlayerSnapshot> p1;
    extern std::shared_ptr<PlayerSnapshot> p2;
}

namespace ini {
    class Config {
    private:
        std::string filepath;
        std::map<std::string, std::string> data;

        std::string trim(const std::string& str);

    public:
        Config(const std::string& file);

        void Read();
        void SetKey(const std::string& key, const std::string& value, const std::string& section);
        bool Save();
        bool RemoveKey(const std::string& key, const std::string& section);
        std::string GetValue(const std::string& key, const std::string& section);
        bool HasKey(const std::string& key, const std::string& section);
        void Print();
        std::vector<std::string> GetKeys();
        std::vector<std::string> GetKeysInSection(const std::string& section);
        void Clear();
        size_t Size();
        std::string GetPath();
    };
}

namespace kv {
    extern ini::Config* userSettings;
    extern void ReadAll();
}


// Helper to unload the DLL and remove all hooks
void Uninject();
