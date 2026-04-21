#pragma once

#include <Windows.h>
#include <tlhelp32.h>
#include "ConfigParser.h"
#include "GameSnapshot.h"
#include "../namespaces.h"
#include <format>
#include <iomanip>
#include "../helper.h"

class ReadProcessMemoryException : public std::runtime_error {
public:
    explicit ReadProcessMemoryException(const std::string& message)
        : std::runtime_error(message) {}

    explicit ReadProcessMemoryException(DWORD error_code)
        : std::runtime_error("ReadProcessMemory failed with error: " + std::to_string(error_code)) {}
};

class GameReader {
private:
    ConfigParser config_parser;
    DWORD pid;
    uintptr_t module_address;
    HANDLE process_handle;
    bool in_match;
    std::map<std::string, std::map<std::string, std::vector<uintptr_t>>> c;

public:
    GameReader() {
        pid = DWORD(0);
        process_handle = nullptr;
        in_match = false;
        std::string path_to_config = helper::GetCurrentDllDirectory() + "/config/memory_address.ini";
        if (!config_parser.load(path_to_config)) {
            throw std::runtime_error(std::format("Failed to load memory_address.ini path: {}", path_to_config));
        }
        c = config_parser.data();
        module_address = (uintptr_t)GetModuleHandle("Polaris-Win64-Shipping.exe");
    }

    static uint64_t bytes_to_int(const std::vector<uint8_t>& b) {
        uint64_t result = 0;
        for (auto byte : b) {
            result = (result << 8) | byte;
        }
        return result;
    }

    static std::vector<uint8_t> int_to_bytes(uint64_t i, int n) {
        std::vector<uint8_t> result(n);
        for (int j = n - 1; j >= 0; j--) {
            result[j] = i & 0xFF;
            i >>= 8;
        }
        return result;
    }

    std::shared_ptr<GameSnapshot> get_updated_state(int rollback_frame) {
        if (!is_valid()) {
            throw std::runtime_error("Not windows - cannot update state");
        }
        //if (!pid) {
        //    reacquire_module();
        //}

        //if (process_handle == nullptr) {
        //    return nullptr;
        //}

        return get_game_snapshot(rollback_frame);
    }

    template< typename T >
    std::string int_to_hex(T i)
    {
        std::stringstream stream;
        stream << "0x"
            << std::setfill('0') << std::setw(sizeof(T) * 2)
            << std::hex << i;
        return stream.str();
    }

    //void reacquire_module() {
    //    module_address = c["MemoryAddressOffsets"]["expected_module_address"][0];
    //    uintptr_t moduleBase = (uintptr_t)GetModuleHandle("Polaris-Win64-Shipping.exe");

    //    //console::AddLog(std::format("reacquire_module module_address: {} {}", int_to_hex(module_address), int_to_hex(moduleBase)));
    //    module_address = moduleBase;
    //}

    std::shared_ptr<GameSnapshot> get_game_snapshot(int rollback_frame) {
        auto pointers = c["MemoryAddressOffsets"]["player_data_pointer_offset"];
        uintptr_t player_data_base_address;
        try {
            player_data_base_address = get_8_bytes_at_end_of_pointer_trail(pointers);
        }
        catch (...) {
            if (in_match) {
                finish_match();
            }
            in_match = false;
            return nullptr;
        }

        if (!in_match) {
            start_match();
            in_match = true;
        }

        auto frame_data = get_frame(player_data_base_address, rollback_frame);
        int frame_count = frame_data.first;
        std::vector<uint8_t> player_data_frame = frame_data.second;

        std::map<std::string, int> p1_dict;
        std::map<std::string, int> p2_dict;

        int p2_offset = c["MemoryAddressOffsets"]["p2_data_offset"][0];
        for (const auto& player_data_entry : c["PlayerDataAddress"]) {
            const std::string& name = player_data_entry.first;
            const std::vector<uintptr_t>& values = player_data_entry.second;

            int p1_value = get_4_bytes_from_data_block(player_data_frame, values[0]);
            int p2_value = get_4_bytes_from_data_block(player_data_frame, values[0] + p2_offset);
            p1_dict[name] = p1_value;
            p2_dict[name] = p2_value;
        }

        auto p1_snapshot = std::make_shared<PlayerSnapshot>(p1_dict);
        auto p2_snapshot = std::make_shared<PlayerSnapshot>(p2_dict);

        bool is_player_player_one;
        try {
            is_player_player_one = get_8_bytes_at_end_of_pointer_trail(
                c["NonPlayerDataAddresses"]["opponent_side"]) == 1;
        }
        catch (...) {
            is_player_player_one = true;
        }

        if (!is_player_player_one) {
            std::swap(p1_snapshot, p2_snapshot);
        }

        int raw_facing = get_4_bytes_from_data_block(
            player_data_frame, c["GameDataAddress"]["facing"][0]);
        bool facing_bool = raw_facing ^ is_player_player_one;

        return std::make_shared<GameSnapshot>(p1_snapshot, p2_snapshot, frame_count, facing_bool);
    }

    std::pair<int, std::vector<uint8_t>> get_frame(uintptr_t player_data_base_address, int rollback_frame) {
        int rollback_frame_offset = c["MemoryAddressOffsets"]["rollback_frame_offset"][0];

        std::vector<std::pair<int, uintptr_t>> frame_chunk;

        int frame_count_offset = c["GameDataAddress"]["frame_count"][0];
        for (int i = 0; i < 32; i++) {
            uintptr_t potential_second_address = player_data_base_address + (i * rollback_frame_offset);
            int potential_frame_count = get_int_from_address(
                potential_second_address + frame_count_offset, 4);
            frame_chunk.emplace_back(potential_frame_count, potential_second_address);
        }

        std::sort(frame_chunk.begin(), frame_chunk.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
        auto best_frame_count = frame_chunk[rollback_frame].first;
        auto player_data_second_address = frame_chunk[rollback_frame].second;

        auto player_data_frame = get_block_of_data(player_data_second_address, rollback_frame_offset);

        return { best_frame_count, player_data_frame };
    }

    std::vector<uint8_t> get_block_of_data_external(uintptr_t address, int size_of_block) {
        std::vector<uint8_t> buffer(size_of_block);
        SIZE_T bytes_read = 0;

        if (!ReadProcessMemory(process_handle,
            reinterpret_cast<LPCVOID>(address),
            buffer.data(),
            size_of_block,
            &bytes_read)) {
            throw ReadProcessMemoryException(GetLastError());
        }

        if (bytes_read != size_of_block) {
            throw ReadProcessMemoryException(
                "Failed to read full block. Requested: " +
                std::to_string(size_of_block) +
                ", read: " +
                std::to_string(bytes_read));
        }

        return buffer;
    }

    std::vector<uint8_t> get_block_of_data(uintptr_t address, int size_of_block) {
        //console::AddLog(std::format("get_block_of_data: {} {} {}", address, int_to_hex(address), size_of_block));
        if (address == 0) {
            //console::AddLog("Null pointer access attempt");
            throw std::runtime_error("Null pointer access attempt");
        }

        if (size_of_block == 0) {
            return std::vector<uint8_t>();
        }

        // Check if the memory region is accessible
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi))) {
            if (mbi.Protect == PAGE_NOACCESS || mbi.Protect & PAGE_GUARD) {
                //console::AddLog("Attempt to read from protected memory");
                throw std::runtime_error("Attempt to read from protected memory");
            }
        }

        std::vector<uint8_t> buffer(size_of_block);

        // For internal DLL, we can just do direct memory copy
        try {
            memcpy_s(buffer.data(), buffer.size(), reinterpret_cast<const void*>(address), size_of_block);
        }
        catch (...) {
            //console::AddLog("Memory access violation");
            throw std::runtime_error("Memory access violation");
        }

        return buffer;
    }

    uint64_t get_int_from_address(uintptr_t address, int num_bytes) {
        auto data = get_block_of_data(address, num_bytes);
        std::reverse(data.begin(), data.end());
        return bytes_to_int(data);
    }

    uint64_t get_8_bytes_at_end_of_pointer_trail(const std::vector<uintptr_t>& trail) {
        uintptr_t address = module_address;
        for (size_t i = 0; i < trail.size(); i++) {
            address = get_int_from_address(address + trail[i], 8);
        }
        return address;
    }

    int get_4_bytes_from_data_block(const std::vector<uint8_t>& frame, int offset) {
        std::vector<uint8_t> bytes(frame.begin() + offset, frame.begin() + offset + 4);
        std::reverse(bytes.begin(), bytes.end());
        return bytes_to_int(bytes);
    }

    //bool is_foreground_pid() {
    //    DWORD pid = get_foreground_pid();
    //    return pid == this->pid;
    //}

    //RECT get_window_rect() {
    //    if (in_match && is_foreground_pid()) {
    //        return get_window_rect_impl();
    //    }
    //    return { 0, 0, 0, 0 };
    //}

    int get_update_wait_ms(float elapsed_ms) {
        return in_match ? std::max(2, 8 - static_cast<int>(std::round(elapsed_ms))) : 1000;
    }

private:
    // Windows-specific implementations would go here
    DWORD get_pid(const std::string& process_name) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create process snapshot. Error: " << GetLastError() << std::endl;
            return 0;
        }

        PROCESSENTRY32 pe32;
        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (!Process32First(hSnapshot, &pe32)) {
            std::cerr << "Failed to get first process. Error: " << GetLastError() << std::endl;
            CloseHandle(hSnapshot);
            return 0;
        }

        do {
            // Сравниваем имя процесса (ANSI версия)
            if (_stricmp(pe32.szExeFile, process_name.c_str()) == 0) {
                DWORD pid = pe32.th32ProcessID;
                CloseHandle(hSnapshot);
                return pid;
            }
        } while (Process32Next(hSnapshot, &pe32));

        CloseHandle(hSnapshot);
        std::cerr << "Process '" << process_name << "' not found." << std::endl;
        return 0;
    };
    uintptr_t get_module_address(DWORD pid, const std::string& module_name) {
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to create module snapshot. Error: " << GetLastError() << std::endl;
            return 0;
        }

        MODULEENTRY32 me32;
        me32.dwSize = sizeof(MODULEENTRY32);

        if (!Module32First(hSnapshot, &me32)) {
            std::cerr << "Failed to get first module. Error: " << GetLastError() << std::endl;
            CloseHandle(hSnapshot);
            return 0;
        }

        // Конвертируем имя модуля в нижний регистр для сравнения
        std::string target_name = module_name;
        std::transform(target_name.begin(), target_name.end(), target_name.begin(),
            [](unsigned char c) { return std::tolower(c); });

        do {
            std::string current_name = me32.szModule;
            std::transform(current_name.begin(), current_name.end(), current_name.begin(),
                [](unsigned char c) { return std::tolower(c); });

            if (current_name == target_name) {
                uintptr_t base_addr = reinterpret_cast<uintptr_t>(me32.modBaseAddr);
                CloseHandle(hSnapshot);
                return base_addr;
            }
        } while (Module32Next(hSnapshot, &me32));

        CloseHandle(hSnapshot);
        std::cerr << "Module '" << module_name << "' not found in process " << pid << std::endl;
        return 0;
    };
    HANDLE get_process_handle(DWORD pid) {
        // Открываем процесс с необходимыми правами доступа
        HANDLE hProcess = OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
            FALSE,  // Не наследуем дескриптор
            pid
        );

        if (hProcess == NULL) {
            DWORD error = GetLastError();
            std::cerr << "Failed to open process (PID: " << pid
                << "). Error code: " << error << std::endl;

            // Попробуем с минимальными правами, если первый запрос не удался
            hProcess = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION,
                FALSE,
                pid
            );

            if (hProcess == NULL) {
                error = GetLastError();
                std::cerr << "Failed to open process with limited rights. Error: "
                    << error << std::endl;
                return NULL;
            }
        }

        return hProcess;
    };
    //DWORD get_foreground_pid() {
    //};
    //RECT get_window_rect_impl() {
    //};
    void start_match() {
    
    };
    void finish_match() {
    
    };
    bool is_valid() { return true; }  // Implementation would check Windows version
};
