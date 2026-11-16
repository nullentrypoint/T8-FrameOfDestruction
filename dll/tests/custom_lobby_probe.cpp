// External read-only validation using the production custom-room reader.
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include "VoiceChat/LoungeLayout.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::cerr << "Usage: custom_lobby_probe PID memory_address.ini\n"; return 1; }
    const DWORD pid = wcstoul(argv[1], nullptr, 10);
    VoiceChat::CustomLobbyLayout layout; std::string error;
    if (!VoiceChat::LoadCustomLobbyLayout(argv[2], layout, error)) { std::cerr << error; return 2; }
    const HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    uintptr_t base = 0;
    if (modules != INVALID_HANDLE_VALUE) {
        if (Module32FirstW(modules, &entry)) do {
            if (!_wcsicmp(entry.szModule, L"Polaris-Win64-Shipping.exe")) base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
        } while (Module32NextW(modules, &entry));
        CloseHandle(modules);
    }
    HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process || !base) { if (process) CloseHandle(process); return 3; }
    VoiceChat::CustomLobbyReader reader(base, layout, [](void* ctx, uintptr_t at, void* out, size_t size) {
        SIZE_T read = 0;
        return ReadProcessMemory(static_cast<HANDLE>(ctx), reinterpret_cast<const void*>(at), out, size, &read) && read == size;
    }, process);
    const auto snapshot = reader.Read();
    std::cout << "status=" << static_cast<int>(snapshot.status) << " count=" << snapshot.participants.size()
        << " current=" << reader.StillCurrent(snapshot) << '\n';
    for (const auto& member : snapshot.participants) std::cout << member.name << " " << member.steamIdCandidate << '\n';
    CloseHandle(process);
    return snapshot.status == VoiceChat::LoungeReadStatus::Ok ? 0 : 4;
}
