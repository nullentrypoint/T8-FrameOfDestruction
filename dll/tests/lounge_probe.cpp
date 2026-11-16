// External, read-only diagnostic harness using the production LoungeReader.
#include "VoiceChat/LoungeLayout.h"
#include <TlHelp32.h>
#include <iomanip>
#include <iostream>

static bool Read(void* context, uintptr_t address, void* output, size_t size) {
    SIZE_T copied = 0;
    return ReadProcessMemory(static_cast<HANDLE>(context), reinterpret_cast<const void*>(address),
        output, size, &copied) && copied == size;
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::cerr << "Usage: lounge_probe PID memory_address.ini\n"; return 2; }
    wchar_t* end = nullptr;
    const auto pid = wcstoul(argv[1], &end, 10);
    if (!pid || !end || *end) return 2;
    VoiceChat::LoungeLayout layout;
    std::string error;
    if (!VoiceChat::LoadLoungeLayout(argv[2], layout, error)) { std::cerr << error << '\n'; return 2; }
    const auto modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (modules == INVALID_HANDLE_VALUE) { std::cerr << "Cannot enumerate modules\n"; return 2; }
    MODULEENTRY32W module{}; module.dwSize = sizeof(module);
    uintptr_t base = 0;
    if (Module32FirstW(modules, &module)) do {
        if (_wcsicmp(module.szModule, L"Polaris-Win64-Shipping.exe") == 0) {
            base = reinterpret_cast<uintptr_t>(module.modBaseAddr); break;
        }
    } while (Module32NextW(modules, &module));
    CloseHandle(modules);
    if (!base) { std::cerr << "Game module not found\n"; return 2; }
    const auto process = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) { std::cerr << "Cannot open process for reading\n"; return 2; }
    const auto s = VoiceChat::LoungeReader(base, layout, Read, process).Read();
    const auto camera = s.status == VoiceChat::LoungeReadStatus::Ok ?
        VoiceChat::ReadLoungeCamera(base, s.world, VoiceChat::LoadLoungeCameraLayout(argv[2]), Read, process) :
        std::optional<VoiceChat::LoungeCamera>{};
    CloseHandle(process);
    std::cout << "status=" << VoiceChat::LoungeStatusName(s.status) << '\n';
    if (s.status == VoiceChat::LoungeReadStatus::NotInLounge) return 0;
    if (s.status != VoiceChat::LoungeReadStatus::Ok) return 1;
    std::cout << "room=" << s.roomKey << " participants=" << s.participants.size() << '\n';
    size_t visible = 0;
    if (camera) for (const auto& p : s.participants) {
        auto head = p.position; head.z += 110;
        if (camera->Project(head)) ++visible;
    }
    std::cout << "camera=" << (camera ? "readable" : "unavailable")
        << " projectedHeads=" << visible << '\n';
    std::cout << std::fixed << std::setprecision(1);
    for (const auto& p : s.participants)
        std::cout << "platform=" << p.platform << " steamCandidate=" << (p.steamIdCandidate != 0)
            << " distance=" << p.distance << " name=" << p.name << '\n';
    return 0;
}
