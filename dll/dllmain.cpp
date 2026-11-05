#include "stdafx.h"

static bool WaitForInitialization(int attempts = 50, int sleepMs = 100)
{
    for (int i = 0; i < attempts; ++i)
    {
        if (d3d12hook::IsInitialized())
            return true;
        Sleep(sleepMs);
    }
    return false;
}

static bool TryInitDX12()
{
    if (!GetModuleHandleA("d3d12.dll") && !GetModuleHandleA("dxgi.dll"))
        return false;

    DebugLog("[DllMain] Attempting DX12 initialization.\n");
    hooks::Init();
    if (WaitForInitialization())
    {
        DebugLog("[DllMain] DX12 initialization succeeded.\n");
        globals::activeBackend = globals::Backend::DX12;
        return true;
    }
    DebugLog("[DllMain] DX12 initialization failed.\n");
    d3d12hook::release();
    return false;
}

using LoadLibraryA_t = HMODULE(WINAPI*)(LPCSTR);
using LoadLibraryW_t = HMODULE(WINAPI*)(LPCWSTR);
static LoadLibraryA_t oLoadLibraryA = nullptr;
static LoadLibraryW_t oLoadLibraryW = nullptr;

// Support DX12 loaded after injection.
static void InitForModule(const char* name)
{
    if (!name || globals::activeBackend == globals::Backend::DX12)
        return;
    const char* base = strrchr(name, '\\');
    base = base ? base + 1 : name;
    if (_stricmp(base, "d3d12.dll") == 0 || _stricmp(base, "dxgi.dll") == 0)
        TryInitDX12();
}

static HMODULE WINAPI hookLoadLibraryA(LPCSTR lpLibFileName)
{
    HMODULE mod = oLoadLibraryA(lpLibFileName);
    if (mod)
        InitForModule(lpLibFileName);
    return mod;
}

static HMODULE WINAPI hookLoadLibraryW(LPCWSTR lpLibFileName)
{
    HMODULE mod = oLoadLibraryW(lpLibFileName);
    if (mod && lpLibFileName)
    {
        char name[MAX_PATH];
        if (WideCharToMultiByte(CP_ACP, 0, lpLibFileName, -1, name, MAX_PATH, nullptr, nullptr))
            InitForModule(name);
    }
    return mod;
}

static DWORD WINAPI UninjectThread(LPVOID)
{
    DebugLog("[DllMain] Uninject thread starting.\n");
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

    TryInitDX12();

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (k32) {
        LPVOID addrA = GetProcAddress(k32, "LoadLibraryA");
        LPVOID addrW = GetProcAddress(k32, "LoadLibraryW");
        if (addrA) {
            MH_CreateHook(addrA, reinterpret_cast<LPVOID>(hookLoadLibraryA), reinterpret_cast<LPVOID*>(&oLoadLibraryA));
            MH_EnableHook(addrA);
            DebugLog("[DllMain] Hooked LoadLibraryA@%p\n", addrA);
        }
        if (addrW) {
            MH_CreateHook(addrW, reinterpret_cast<LPVOID>(hookLoadLibraryW), reinterpret_cast<LPVOID*>(&oLoadLibraryW));
            MH_EnableHook(addrW);
            DebugLog("[DllMain] Hooked LoadLibraryW@%p\n", addrW);
        }
    }
    mousehooks::Init();
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
