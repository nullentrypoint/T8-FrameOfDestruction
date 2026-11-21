#include "stdafx.h"
#include "VoiceChat/VoicePresentation.h"
#include "VoiceChat/MemoryLayout.h"
#include "TekkenBot/GameLog.h"
#include "TekkenBot/GameReader.h"
#include "helper.h"
#include "msdf_text.h"
#include <mutex>

namespace hooks { void Remove(); }

namespace d3d12hook {
    std::shared_ptr<GameReader> gameReader = nullptr;
    GameLog gameLog;
    ImFont* overlayFont = NULL;
    ImFont* menuFont = nullptr;

    static bool HideCombatHints() {
        static const auto config = std::filesystem::path(helper::GetCurrentDllDirectory()) /
            "T8FrameOfDestruction" / "config";
        static const bool restrictOnline = GetPrivateProfileIntW(L"Launch", L"HideOnlineCombatHints",
            0, (config / "launch_options.ini").c_str()) != 0;
        if (!restrictOnline) return false;

        static const auto identities = [] {
            VoiceChat::MemoryLayout layout;
            std::string error;
            VoiceChat::LoadMemoryLayout(config / "memory_address.ini", layout, error);
            return VoiceChat::PlayerIdentityReader(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)), layout.identity);
        }();
        const auto snapshot = identities.Read();
        if (snapshot.status != VoiceChat::IdentityReadStatus::Ok) return true;
        // Role 0 is a placeholder, 1 is local. Hide for remote or unknown roles.
        return std::any_of(snapshot.players.begin(), snapshot.players.end(),
            [](const auto& player) { return player.rawWords[0] > 1; });
    }

    static void DrawCombatOverlay(ImGuiIO& io) {
        if (!overlayFont) return;
        const bool hidden = HideCombatHints();
        static bool previouslyHidden = false;
        if (hidden && !previouslyHidden) {
            gameLog = GameLog{};
            console::p1.reset();
            console::p2.reset();
        }
        previouslyHidden = hidden;
        overlay::SetCamera(std::nullopt);
        if (gameReader) {
            try {
                if (!hidden) gameLog.update(gameReader);
                const auto localP1 = gameReader->read_local_is_p1();
                VoiceChat::localPlayerSlot = localP1 ? (*localP1 ? 0 : 1) : -1;
                overlay::SetCamera(gameReader->read_camera());
            } catch (const std::runtime_error& e) {
                console::AddLogError(std::format("gameReader update: {}", e.what()));
                VoiceChat::localPlayerSlot = -1;
            }
        }
        overlay::Init(io, overlayFont, hidden);
    }

    PresentD3D12            oPresentD3D12 = nullptr;
    Present1Fn              oPresent1D3D12 = nullptr;
    ExecuteCommandListsFn   oExecuteCommandListsD3D12 = nullptr;
    ResizeBuffersFn         oResizeBuffersD3D12 = nullptr;

    static ID3D12Device* gDevice = nullptr;
    static ID3D12CommandQueue* gCommandQueue = nullptr;
    static ID3D12DescriptorHeap* gHeapRTV = nullptr;
    static ID3D12DescriptorHeap* gHeapSRV = nullptr;
    static ID3D12GraphicsCommandList* gCommandList = nullptr;
    static ID3D12Fence* gOverlayFence = nullptr;
    static HANDLE                   gFenceEvent = nullptr;
    static UINT64                  gOverlayFenceValue = 0;
    static uintx_t                 gBufferCount = 0;

    struct FrameContext {
        ID3D12CommandAllocator* allocator;
        ID3D12Resource* renderTarget;
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
    };
    static FrameContext* gFrameContexts = nullptr;
    static bool                   gInitialized = false;
    static bool                   gShutdown = false;
    static std::mutex gQueueMutex;

    static bool HasCommandQueue(IDXGISwapChain3* swapChain) {
        std::lock_guard lock(gQueueMutex);
        if (!gDevice) {
            const HRESULT hr = swapChain->GetDevice(IID_PPV_ARGS(&gDevice));
            if (FAILED(hr)) DebugLog("[d3d12hook] GetDevice: hr=0x%08X\n", hr);
        }
        return gCommandQueue != nullptr;
    }

    void release();

    // MSDF upload/vertex resources are shared across frames and must outlive submissions.
    bool WaitForOverlayGPU() {
        if (!gOverlayFence || gOverlayFence->GetCompletedValue() >= gOverlayFenceValue) return true;
        if (gDevice && FAILED(gDevice->GetDeviceRemovedReason())) return true;
        if (!gFenceEvent || FAILED(gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent))) return false;
        return WaitForSingleObject(gFenceEvent, INFINITE) == WAIT_OBJECT_0;
    }

    // Utility to log HRESULTs
    inline void LogHRESULT(const char* label, HRESULT hr) {
        DebugLog("[d3d12hook] %s: hr=0x%08X\n", label, hr);
    }

    long __fastcall hookPresentD3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (GetAsyncKeyState(globals::openMenuKey) & 1) {
            menu::isOpen = !menu::isOpen;
            console::isOpen = !console::isOpen;
            DebugLog("[d3d12hook] Toggle menu: isOpen=%d\n", menu::isOpen);
        }

        //if (GetAsyncKeyState(globals::uninjectKey) & 1) {
        //    Uninject();
        //    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        //}

        if (!HasCommandQueue(pSwapChain)) {
            return oPresentD3D12(pSwapChain, SyncInterval, Flags);
        }

        if (!gInitialized) {
            DebugLog("[d3d12hook] Initializing ImGui on first Present.\n");

            // Swap Chain description
            DXGI_SWAP_CHAIN_DESC desc = {};
            pSwapChain->GetDesc(&desc);
            gBufferCount = desc.BufferCount;
            DebugLog("[d3d12hook] BufferCount=%u\n", gBufferCount);

            // Create descriptor heaps
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heapDesc.NumDescriptors = gBufferCount;
            if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapRTV)))) {
                LogHRESULT("CreateDescriptorHeap RTV", E_FAIL);
                return oPresentD3D12(pSwapChain, SyncInterval, Flags);
            }

            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapSRV)))) {
                LogHRESULT("CreateDescriptorHeap SRV", E_FAIL);
                return oPresentD3D12(pSwapChain, SyncInterval, Flags);
            }

            // Allocate frame contexts
            gFrameContexts = new FrameContext[gBufferCount];
            ZeroMemory(gFrameContexts, sizeof(FrameContext) * gBufferCount);

            // Create command allocator for each frame
            for (UINT i = 0; i < gBufferCount; ++i) {
                if (FAILED(gDevice->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(&gFrameContexts[i].allocator)))) {
                    LogHRESULT("CreateCommandAllocator", E_FAIL);
                    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
                }
            }

            // Create RTVs for each back buffer
            UINT rtvSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            auto rtvHandle = gHeapRTV->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < gBufferCount; ++i) {
                ID3D12Resource* back;
                pSwapChain->GetBuffer(i, IID_PPV_ARGS(&back));
                gDevice->CreateRenderTargetView(back, nullptr, rtvHandle);
                gFrameContexts[i].renderTarget = back;
                gFrameContexts[i].rtvHandle = rtvHandle;
                rtvHandle.ptr += rtvSize;
            }

            // ImGui setup
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(desc.OutputWindow);
            ImGui_ImplDX12_Init(gDevice, gBufferCount,
                desc.BufferDesc.Format,
                gHeapSRV,
                gHeapSRV->GetCPUDescriptorHandleForHeapStart(),
                gHeapSRV->GetGPUDescriptorHandleForHeapStart());

            io.Fonts->AddFontDefault();
            std::string fontPath = helper::GetWindowsDir() + "\\Fonts\\consolab.ttf";
            overlayFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 100.0f, NULL);
            if (!overlayFont) overlayFont = io.Fonts->Fonts[0];
            const std::string menuFontPath = helper::GetWindowsDir() + "\\Fonts\\consola.ttf";
            menuFont = io.Fonts->AddFontFromFileTTF(menuFontPath.c_str(), 100.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (!menuFont) menuFont = io.Fonts->Fonts[0];
            msdftext::Initialize(gDevice, desc.BufferDesc.Format, fontPath);

            try {
                gameReader = std::make_shared<GameReader>();
            }
            catch (const std::runtime_error& e) {
                console::AddLogError(std::format("gameReader init: {}", e.what()));
            }

            try {
                kv::ReadAll();
                menu::LoadSettings();
            }
            catch (const std::runtime_error& e) {
                console::AddLogError(std::format("kv::ReadAll init: {}", e.what()));
            }

            DebugLog("[d3d12hook] ImGui initialized.\n");

            inputhook::Init(desc.OutputWindow);

            if (!gOverlayFence) {
                if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence)))) {
                    LogHRESULT("CreateFence", E_FAIL);
                    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
                }
            }

            if (!gFenceEvent) {
                gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!gFenceEvent) {
                    DebugLog("[d3d12hook] Failed to create fence event: %lu\n", GetLastError());
                }
            }

            // Hook CommandQueue and Fence are already captured by minhook
            gInitialized = true;
        }

        if (!gShutdown) {
            // Render ImGui
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& io = ImGui::GetIO();

            io.MouseDrawCursor = false;
            menu::Init(io, menuFont);
            DrawCombatOverlay(io);

            UINT frameIdx = pSwapChain->GetCurrentBackBufferIndex();
            FrameContext& ctx = gFrameContexts[frameIdx];

            // Wait for the GPU to finish with the previous frame
            bool canRender = true;
            if (!gOverlayFence || !gFenceEvent) {
                canRender = false;
            } else if (gOverlayFence->GetCompletedValue() < gOverlayFenceValue) {
                HRESULT hr = gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);
                if (SUCCEEDED(hr)) {
                    const DWORD waitTimeoutMs = 2000; // Extended timeout
                    DWORD waitRes = WaitForSingleObject(gFenceEvent, waitTimeoutMs);
                    if (waitRes == WAIT_TIMEOUT) {
                        DebugLog("[d3d12hook] WaitForSingleObject timeout\n");
                        canRender = false;
                    } else if (waitRes != WAIT_OBJECT_0) {
                        DebugLog("[d3d12hook] WaitForSingleObject failed: %lu\n", GetLastError());
                        canRender = false;
                    }
                } else {
                    LogHRESULT("SetEventOnCompletion", hr);
                    canRender = false;
                }
            }

            if (!canRender) {
                DebugLog("[d3d12hook] Skipping ImGui render for this frame\n");
                ImGui::EndFrame();
                return oPresentD3D12(pSwapChain, SyncInterval, Flags);
            }

            // Reset allocator and command list using frame-specific allocator
            HRESULT hr = ctx.allocator->Reset();
            if (FAILED(hr)) {
                LogHRESULT("CommandAllocator->Reset", hr);
                return oPresentD3D12(pSwapChain, SyncInterval, Flags);
            }

            if (!gCommandList) {
                hr = gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    ctx.allocator, nullptr, IID_PPV_ARGS(&gCommandList));
                if (FAILED(hr)) {
                    LogHRESULT("CreateCommandList", hr);
                    return oPresentD3D12(pSwapChain, SyncInterval, Flags);
                }
                gCommandList->Close();
            }
            hr = gCommandList->Reset(ctx.allocator, nullptr);
            if (FAILED(hr)) {
                LogHRESULT("CommandList->Reset", hr);
                return oPresentD3D12(pSwapChain, SyncInterval, Flags);
            }

            // Transition to render target
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = ctx.renderTarget;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            gCommandList->ResourceBarrier(1, &barrier);

            gCommandList->OMSetRenderTargets(1, &ctx.rtvHandle, FALSE, nullptr);
            ID3D12DescriptorHeap* heaps[] = { gHeapSRV };
            gCommandList->SetDescriptorHeaps(1, heaps);

            ImGui::Render();
            msdftext::Render(gCommandList, io.DisplaySize);
            gCommandList->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);

            // Transition back to present
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            gCommandList->ResourceBarrier(1, &barrier);
            gCommandList->Close();

            // Execute
            if (!gCommandQueue) {
                DebugLog("[d3d12hook] CommandQueue not set, skipping ExecuteCommandLists.\n");
            }
            else {
                oExecuteCommandListsD3D12(gCommandQueue, 1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));
                if (gOverlayFence) {
                    // Call Signal directly on the command queue to synchronize the internal overlay.
                    HRESULT hr = gCommandQueue->Signal(gOverlayFence, ++gOverlayFenceValue);
                    if (FAILED(hr)) {
                        LogHRESULT("Signal", hr);
                        if (gDevice) {
                            HRESULT reason = gDevice->GetDeviceRemovedReason();
                            DebugLog("[d3d12hook] DeviceRemovedReason=0x%08X\n", reason);
                            if (reason != S_OK) {
                                DebugLog("[d3d12hook] Device lost. Releasing resources.\n");
                                release();
                            }
                        }
                    }
                }
            }
        }

        return oPresentD3D12(pSwapChain, SyncInterval, Flags);
    }

    long __fastcall hookPresent1D3D12(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* pParams) {
        if (GetAsyncKeyState(globals::openMenuKey) & 1) {
            menu::isOpen = !menu::isOpen;
            console::isOpen = !console::isOpen;
            DebugLog("[d3d12hook] Toggle menu: isOpen=%d\n", menu::isOpen);
        }

        if (GetAsyncKeyState(globals::uninjectKey) & 1) {
            Uninject();
            return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
        }

        if (!HasCommandQueue(pSwapChain)) {
            return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
        }

        if (!gInitialized) {
            try {
                gameReader = std::make_shared<GameReader>();
            }
            catch (const std::runtime_error& e) {
                console::AddLogError(std::format("gameReader init: {}", e.what()));
            }

            DebugLog("[d3d12hook] Initializing ImGui on first Present1.\n");

            // Swap Chain description
            DXGI_SWAP_CHAIN_DESC desc = {};
            pSwapChain->GetDesc(&desc);
            gBufferCount = desc.BufferCount;
            DebugLog("[d3d12hook] BufferCount=%u\n", gBufferCount);

            // Create descriptor heaps
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            heapDesc.NumDescriptors = gBufferCount;
            if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapRTV)))) {
                LogHRESULT("CreateDescriptorHeap RTV", E_FAIL);
                return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
            }

            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(gDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&gHeapSRV)))) {
                LogHRESULT("CreateDescriptorHeap SRV", E_FAIL);
                return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
            }

            // Allocate frame contexts
            gFrameContexts = new FrameContext[gBufferCount];
            ZeroMemory(gFrameContexts, sizeof(FrameContext) * gBufferCount);

            // Create command allocator for each frame
            for (UINT i = 0; i < gBufferCount; ++i) {
                if (FAILED(gDevice->CreateCommandAllocator(
                        D3D12_COMMAND_LIST_TYPE_DIRECT,
                        IID_PPV_ARGS(&gFrameContexts[i].allocator)))) {
                    LogHRESULT("CreateCommandAllocator", E_FAIL);
                    return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
                }
            }

            // Create RTVs for each back buffer
            UINT rtvSize = gDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
            auto rtvHandle = gHeapRTV->GetCPUDescriptorHandleForHeapStart();
            for (UINT i = 0; i < gBufferCount; ++i) {
                ID3D12Resource* back;
                pSwapChain->GetBuffer(i, IID_PPV_ARGS(&back));
                gDevice->CreateRenderTargetView(back, nullptr, rtvHandle);
                gFrameContexts[i].renderTarget = back;
                gFrameContexts[i].rtvHandle = rtvHandle;
                rtvHandle.ptr += rtvSize;
            }

            // ImGui setup
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO(); (void)io;
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(desc.OutputWindow);
            ImGui_ImplDX12_Init(gDevice, gBufferCount,
                desc.BufferDesc.Format,
                gHeapSRV,
                gHeapSRV->GetCPUDescriptorHandleForHeapStart(),
                gHeapSRV->GetGPUDescriptorHandleForHeapStart());
            DebugLog("[d3d12hook] ImGui initialized.\n");

            io.Fonts->AddFontDefault();
            const std::string fontPath = helper::GetWindowsDir() + "\\Fonts\\consolab.ttf";
            overlayFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 100.0f, NULL);
            if (!overlayFont) overlayFont = io.Fonts->Fonts[0];
            const std::string menuFontPath = helper::GetWindowsDir() + "\\Fonts\\consola.ttf";
            menuFont = io.Fonts->AddFontFromFileTTF(menuFontPath.c_str(), 100.0f, nullptr, io.Fonts->GetGlyphRangesCyrillic());
            if (!menuFont) menuFont = io.Fonts->Fonts[0];
            msdftext::Initialize(gDevice, desc.BufferDesc.Format, fontPath);

            inputhook::Init(desc.OutputWindow);

            if (!gOverlayFence) {
                if (FAILED(gDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gOverlayFence)))) {
                    LogHRESULT("CreateFence", E_FAIL);
                    return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
                }
            }

            if (!gFenceEvent) {
                gFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (!gFenceEvent) {
                    DebugLog("[d3d12hook] Failed to create fence event: %lu\n", GetLastError());
                }
            }

            // Hook CommandQueue and Fence are already captured by minhook
            gInitialized = true;
        }

        if (!gShutdown) {
            // Render ImGui
            ImGui_ImplDX12_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& io = ImGui::GetIO();

            //if (menu::isOpen) menu::Init(io);
            io.MouseDrawCursor = false;
            if (console::isOpen) console::Init(io);
            DrawCombatOverlay(io);

            UINT frameIdx = pSwapChain->GetCurrentBackBufferIndex();
            FrameContext& ctx = gFrameContexts[frameIdx];

            // Wait for the GPU to finish with the previous frame
            bool canRender = true;
            if (!gOverlayFence || !gFenceEvent) {
                canRender = false;
            } else if (gOverlayFence->GetCompletedValue() < gOverlayFenceValue) {
                HRESULT hr = gOverlayFence->SetEventOnCompletion(gOverlayFenceValue, gFenceEvent);
                if (SUCCEEDED(hr)) {
                    const DWORD waitTimeoutMs = 2000; // Extended timeout
                    DWORD waitRes = WaitForSingleObject(gFenceEvent, waitTimeoutMs);
                    if (waitRes == WAIT_TIMEOUT) {
                        DebugLog("[d3d12hook] WaitForSingleObject timeout\n");
                        canRender = false;
                    } else if (waitRes != WAIT_OBJECT_0) {
                        DebugLog("[d3d12hook] WaitForSingleObject failed: %lu\n", GetLastError());
                        canRender = false;
                    }
                } else {
                    LogHRESULT("SetEventOnCompletion", hr);
                    canRender = false;
                }
            }

            if (!canRender) {
                DebugLog("[d3d12hook] Skipping ImGui render for this frame\n");
                ImGui::EndFrame();
                return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
            }

            // Reset allocator and command list using frame-specific allocator
            HRESULT hr = ctx.allocator->Reset();
            if (FAILED(hr)) {
                LogHRESULT("CommandAllocator->Reset", hr);
                return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
            }

            if (!gCommandList) {
                hr = gDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                    ctx.allocator, nullptr, IID_PPV_ARGS(&gCommandList));
                if (FAILED(hr)) {
                    LogHRESULT("CreateCommandList", hr);
                    return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
                }
                gCommandList->Close();
            }
            hr = gCommandList->Reset(ctx.allocator, nullptr);
            if (FAILED(hr)) {
                LogHRESULT("CommandList->Reset", hr);
                return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
            }

            // Transition to render target
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = ctx.renderTarget;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            gCommandList->ResourceBarrier(1, &barrier);

            gCommandList->OMSetRenderTargets(1, &ctx.rtvHandle, FALSE, nullptr);
            ID3D12DescriptorHeap* heaps[] = { gHeapSRV };
            gCommandList->SetDescriptorHeaps(1, heaps);

            ImGui::Render();
            msdftext::Render(gCommandList, io.DisplaySize);
            gCommandList->SetDescriptorHeaps(1, heaps);
            ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), gCommandList);

            // Transition back to present
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
            gCommandList->ResourceBarrier(1, &barrier);
            gCommandList->Close();

            // Execute
            if (!gCommandQueue) {
                DebugLog("[d3d12hook] CommandQueue not set, skipping ExecuteCommandLists.\n");
            }
            else {
                oExecuteCommandListsD3D12(gCommandQueue, 1, reinterpret_cast<ID3D12CommandList* const*>(&gCommandList));
                if (gOverlayFence) {
                    // Call Signal directly on the command queue to synchronize the internal overlay.
                    HRESULT hr = gCommandQueue->Signal(gOverlayFence, ++gOverlayFenceValue);
                    if (FAILED(hr)) {
                        LogHRESULT("Signal", hr);
                        if (gDevice) {
                            HRESULT reason = gDevice->GetDeviceRemovedReason();
                            DebugLog("[d3d12hook] DeviceRemovedReason=0x%08X\n", reason);
                            if (reason != S_OK) {
                                DebugLog("[d3d12hook] Device lost. Releasing resources.\n");
                                release();
                            }
                        }
                    }
                }
            }
        }

        return oPresent1D3D12(pSwapChain, SyncInterval, Flags, pParams);
    }

    void STDMETHODCALLTYPE hookExecuteCommandListsD3D12(
        ID3D12CommandQueue* _this,
        UINT                          NumCommandLists,
        ID3D12CommandList* const* ppCommandLists) {
        {
            std::lock_guard lock(gQueueMutex);
            // Compute/copy submissions must not consume the capture opportunity:
            // keep looking until a direct queue for the presenting device appears.
            if (!gCommandQueue && gDevice && _this->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                ID3D12Device* queueDevice = nullptr;
                if (SUCCEEDED(_this->GetDevice(IID_PPV_ARGS(&queueDevice)))) {
                    if (queueDevice == gDevice) {
                        _this->AddRef();
                        gCommandQueue = _this;
                        DebugLog("[d3d12hook] Captured CommandQueue=%p\n", _this);
                    }
                    queueDevice->Release();
                }
            }
        }
        oExecuteCommandListsD3D12(_this, NumCommandLists, ppCommandLists);
    }

    HRESULT STDMETHODCALLTYPE hookResizeBuffersD3D12(
        IDXGISwapChain3* pSwapChain,
        UINT BufferCount,
        UINT Width,
        UINT Height,
        DXGI_FORMAT NewFormat,
        UINT SwapChainFlags)
    {
        DebugLog("[d3d12hook] ResizeBuffers called: %ux%u Buffers=%u\n",
            Width, Height, BufferCount);

        if (gInitialized)
        {
            DebugLog("[d3d12hook] Releasing resources for resize\n");

            if (!WaitForOverlayGPU()) return DXGI_ERROR_WAS_STILL_DRAWING;
            msdftext::Shutdown();
            overlayFont = nullptr;
            menuFont = nullptr;

            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            inputhook::Remove(globals::mainWindow);

            if (gCommandList)
            {
                gCommandList->Release();
                gCommandList = nullptr;
            }
            if (gHeapRTV)
            {
                gHeapRTV->Release();
                gHeapRTV = nullptr;
            }
            if (gHeapSRV)
            {
                gHeapSRV->Release();
                gHeapSRV = nullptr;
            }

            for (UINT i = 0; i < gBufferCount; ++i)
            {
                if (gFrameContexts[i].renderTarget)
                {
                    gFrameContexts[i].renderTarget->Release();
                    gFrameContexts[i].renderTarget = nullptr;
                }
                if (gFrameContexts[i].allocator)
                {
                    gFrameContexts[i].allocator->Release();
                    gFrameContexts[i].allocator = nullptr;
                }
            }

            delete[] gFrameContexts;
            gFrameContexts = nullptr;
            gBufferCount = 0;

            gInitialized = false;
        }

        return oResizeBuffersD3D12(
            pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    void release() {
        DebugLog("[d3d12hook] Releasing resources and hooks.\n");
        gShutdown = true;
        if (!WaitForOverlayGPU()) {
            DebugLog("[d3d12hook] Cannot synchronize overlay shutdown\n");
            return;
        }
        msdftext::Shutdown();
        overlayFont = nullptr;
        menuFont = nullptr;
        if (globals::mainWindow) {
            inputhook::Remove(globals::mainWindow);
        }

        // Shutdown ImGui before releasing any D3D resources
        if (gInitialized && ImGui::GetCurrentContext())
        {
            ImGui_ImplDX12_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            gInitialized = false;
        }

        if (gCommandList) gCommandList->Release();
        if (gHeapRTV) gHeapRTV->Release();
        if (gHeapSRV) gHeapSRV->Release();

        for (UINT i = 0; i < gBufferCount; ++i) {
            if (gFrameContexts[i].renderTarget) gFrameContexts[i].renderTarget->Release();
        }
        if (gOverlayFence)
        {
            gOverlayFence->Release();
            gOverlayFence = nullptr;
        }

        if (gFenceEvent)
        {
            CloseHandle(gFenceEvent);
            gFenceEvent = nullptr;
        }

        if (gCommandQueue)
        {
            gCommandQueue->Release();
            gCommandQueue = nullptr;
        }

        if (gDevice) gDevice->Release();
        delete[] gFrameContexts;

        // Disable hooks installed for D3D12
        hooks::Remove();
    }

    bool IsInitialized()
    {
        return gInitialized;
    }
}
