#include "stdafx.h"
#include "VoiceChat/SteamDiagnostics.h"
#include "VoiceChat/SteamPeerTrace.h"
#include "VoiceChat/SteamProfileTrace.h"
#include "helper.h"
#include <filesystem>

static bool TryInitDX12()
{
    if (!GetModuleHandleA("d3d12.dll") && !GetModuleHandleA("dxgi.dll"))
        return false;

    DebugLog("[DllMain] Attempting DX12 initialization.\n");
    if (!hooks::Init()) return false;
    // Installing hooks and rendering the first frame are separate milestones.
    // Atlas generation/loading can take arbitrarily long. Never tear down the
    // render thread's resources just because it has not finished yet.
    globals::activeBackend = globals::Backend::DX12;
    DebugLog("[DllMain] DX12 hooks installed; waiting for Present.\n");
    return true;
}

static DWORD WINAPI UninjectThread(LPVOID)
{
    DebugLog("[DllMain] Uninject thread starting.\n");
    if (SteamDiagnostics::IsSteamShuttingDown() || SteamDiagnostics::PeerTrace::IsPinned() || SteamDiagnostics::ProfileTrace::IsPinned()) {
        DebugLog("[DllMain] Steam shutdown or pinned trace is active; exit the game to unload.\n");
        return 0;
    }
    SteamDiagnostics::Stop();
    if (SteamDiagnostics::IsSteamShuttingDown() || SteamDiagnostics::PeerTrace::IsPinned() || SteamDiagnostics::ProfileTrace::IsPinned()) {
        DebugLog("[DllMain] Steam shutdown or pinned trace is active; exit the game to unload.\n");
        return 0;
    }
    if (globals::activeBackend == globals::Backend::DX12)
        d3d12hook::release();
    mousehooks::Remove();
    MH_DisableHook(MH_ALL_HOOKS);
    MH_RemoveHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    DebugLog("[DllMain] Unloading module and exiting thread.\n");
    FreeLibraryAndExitThread(globals::mainModule, 0);
    return 0;
}

void Uninject()
{
    HANDLE hThread = CreateThread(nullptr, 0, UninjectThread, nullptr, 0, nullptr);
    if (hThread)
        CloseHandle(hThread);
}

static DWORD WINAPI onAttach(LPVOID)
{
    DebugLog("[DllMain] onAttach starting.\n");
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK) {
        DebugLog("[DllMain] MinHook initialization failed: %s\n", MH_StatusToString(mhStatus));
        return 1;
    }

    const auto options = std::filesystem::path(helper::GetCurrentDllDirectory()) /
        "T8FrameOfDestruction" / "config" / "launch_options.ini";
    globals::voiceOnly = GetPrivateProfileIntW(L"Launch", L"VoiceOnly", 0, options.c_str()) == 1;
    globals::hideOnlineCombatHints = GetPrivateProfileIntW(L"Launch", L"HideOnlineCombatHints", 0, options.c_str()) != 0;
    SteamDiagnostics::Start();
    mousehooks::Init();
    // Retry on this worker only, including when DX12 loads after injection.
    // LoadLibrary callbacks can re-enter initialization on arbitrary threads.
    while (!TryInitDX12()) Sleep(1000);
    DebugLog("[DllMain] Hook initialization completed.\n");
    return 0;
}

BOOL WINAPI DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DebugLog("[DllMain] DLL_PROCESS_ATTACH: hModule=%p\n", hModule);
        globals::mainModule = hModule;
        {
            HANDLE thread = CreateThread(nullptr, 0, onAttach, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
            else DebugLog("[DllMain] Failed to create hook thread: %d\n", GetLastError());
        }
        break;
    case DLL_PROCESS_DETACH:
        // Process termination has already stopped other threads. GPU/Steam and
        // CRT locks may no longer be usable; let the OS reclaim these resources.
        // Explicit F11 unload performs its cleanup on UninjectThread instead.
        if (lpReserved) return TRUE;
        DebugLog("[DllMain] DLL_PROCESS_DETACH. Releasing hooks and uninitializing MinHook.\n");
        if (globals::activeBackend == globals::Backend::DX12)
            d3d12hook::release();
        mousehooks::Remove();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        break;
    }
    return TRUE;
}
