// Run headlessly: exercise the production memory reader against real page protections.
#include "stdafx.h"
#include "TekkenBot/GameReader.h"
#include <cassert>
#include <filesystem>

static std::string test_directory;
static std::vector<std::string> errors;
namespace helper {
    std::string GetCurrentDllDirectory() { return test_directory; }
}
namespace console {
    void AddLogError(const std::string& text) { errors.push_back(text); }
}

static void expect_failure(uintptr_t address, int size) {
    GameReader reader;
    const auto before = errors.size();
    bool caught = false;
    try { reader.get_block_of_data(address, size); }
    catch (const ReadProcessMemoryException&) { caught = true; }
    assert(caught);
    assert(errors.size() == before + 1);
    assert(errors.back().find("Update T8FrameOfDestruction and memory_address.ini") != std::string::npos);
    // Subsequent render ticks must not retry reads or flood the console.
    assert(!reader.get_updated_state(0));
    assert(!reader.read_local_is_p1());
    assert(!reader.read_camera());
    try { reader.get_block_of_data(address, size); }
    catch (const ReadProcessMemoryException&) {}
    assert(errors.size() == before + 1);
}

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("t8_reader_memory_" + std::to_string(GetCurrentProcessId()));
    test_directory = root.string();
    std::filesystem::create_directories(root / "T8FrameOfDestruction/config");
    { std::ofstream config(root / "T8FrameOfDestruction/config/memory_address.ini");
      config << "[MemoryAddressOffsets]\nplayer_data_pointer_offset = 0\n"; }

    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = info.dwPageSize;
    auto memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, page * 2,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    assert(memory);
    memory[0] = 42;
    GameReader reader;
    assert(reader.get_block_of_data(reinterpret_cast<uintptr_t>(memory), 1)[0] == 42);
    DWORD previous = 0;
    assert(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous));
    expect_failure(UINTPTR_MAX, 8); // Address from the reported crash.
    expect_failure(reinterpret_cast<uintptr_t>(memory + page - 4), 8);
    expect_failure(reinterpret_cast<uintptr_t>(memory + page), 8);
    assert(VirtualFree(memory, 0, MEM_RELEASE));
    expect_failure(reinterpret_cast<uintptr_t>(memory), 8);
    bool caught = false;
    try { GameReader::get_4_bytes_from_data_block({1, 2, 3}, 0); }
    catch (const std::runtime_error&) { caught = true; }
    assert(caught);
    std::filesystem::remove_all(root);
    std::cout << "Reader memory checks passed\n";
}
