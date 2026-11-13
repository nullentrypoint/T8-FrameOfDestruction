#pragma once

#include <Windows.h>
#include <tlhelp32.h>
#include "ConfigParser.h"
#include "GameSnapshot.h"
#include "../namespaces.h"
#include <format>
#include <iomanip>
#include <cstring>
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
    bool reading_disabled = false;
    void disable_reading(const std::string& reason) {
        if (reading_disabled) return;
        reading_disabled = true;
        in_match = false;
        console::AddLogError("Game memory read failed. The mod may be incompatible with the updated game. "
            "Update T8FrameOfDestruction and memory_address.ini. Game data reading is disabled until restart. " + reason);
    }
    std::string action_frame_path;
    struct ThrowCache {
        uintptr_t pointer = 0;
        MoveInfoEnums::CharacterCodes character{};
        std::vector<ThrowBreakRule> rules;
    };
    ThrowCache throw_cache[2];
    int throw_frame = -1;
    std::map<std::string, std::map<std::string, std::vector<uintptr_t>>> c;

public:
    GameReader() {
        pid = DWORD(0);
        process_handle = nullptr;
        in_match = false;
        std::string path_to_config = helper::GetCurrentDllDirectory() + "\\T8FrameOfDestruction\\config\\memory_address.ini";
        action_frame_path = path_to_config.substr(0, path_to_config.find_last_of("\\/")) + "\\actionable_frames.json";
        if (!config_parser.load(path_to_config)) {
            throw std::runtime_error(std::format("Failed to load memory_address.ini path: {}", path_to_config));
        }
        c = config_parser.data();
        module_address = (uintptr_t)GetModuleHandle("Polaris-Win64-Shipping.exe");
    }

    const std::string& get_action_frame_path() const { return action_frame_path; }

    // Same side mapping used to normalize the frame-data player snapshots.
    // Do not assume P1 when the game has not populated this field yet.
    std::optional<bool> read_local_is_p1() {
        if (reading_disabled) return std::nullopt;
        try {
            const auto side = get_8_bytes_at_end_of_pointer_trail(c.at("NonPlayerDataAddresses").at("opponent_side"));
            if (side <= 1) return side == 1;
        } catch (...) {}
        return std::nullopt;
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

        if (reading_disabled) return nullptr;
        try {
            return get_game_snapshot(rollback_frame);
        } catch (const std::exception& e) {
            disable_reading(e.what());
            return nullptr;
        }
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
        if (reading_disabled) return nullptr;
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

        if (!player_data_base_address) {
            if (in_match) finish_match();
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
        if (frame_count < throw_frame) {
            throw_cache[0] = {};
            throw_cache[1] = {};
        }
        throw_frame = frame_count;
        read_throw_player(player_data_frame, 0, *p1_snapshot, throw_cache[0]);
        read_throw_player(player_data_frame, p2_offset, *p2_snapshot, throw_cache[1]);
        read_spatial_player(player_data_frame, 0, *p1_snapshot);
        read_spatial_player(player_data_frame, p2_offset, *p2_snapshot);

        bool is_player_player_one;
        try {
            is_player_player_one = get_8_bytes_at_end_of_pointer_trail(
                c["NonPlayerDataAddresses"]["opponent_side"]) == 1;
        }
        catch (...) {
            is_player_player_one = true;
        }
        if (reading_disabled) return nullptr;

        if (!is_player_player_one) {
            std::swap(p1_snapshot, p2_snapshot);
        }

        int raw_facing = get_4_bytes_from_data_block(
            player_data_frame, c["GameDataAddress"]["facing"][0]);
        bool facing_bool = raw_facing ^ is_player_player_one;

        auto snapshot = std::make_shared<GameSnapshot>(p1_snapshot, p2_snapshot, frame_count, facing_bool);
        snapshot->camera = read_camera();
        return snapshot;
    }

    static std::optional<WorldPoint> decode_position(const std::vector<uint8_t>& data, size_t offset) {
        if (offset > data.size() || data.size() - offset < sizeof(WorldPoint)) return std::nullopt;
        WorldPoint point;
        std::memcpy(&point, data.data() + offset, sizeof(point));
        return point.valid() ? std::optional<WorldPoint>(point) : std::nullopt;
    }

    void read_throw_player(const std::vector<uint8_t>& data, size_t base,
        PlayerSnapshot& player, ThrowCache& cache) {
        const auto section = c.find("ThrowPlayerData");
        if (section == c.end()) return;
        auto field = [&](const char* name, auto& value) {
            const auto entry = section->second.find(name);
            if (entry == section->second.end() || entry->second.empty()) return false;
            const auto offset = entry->second.front();
            if (base > data.size() || offset > data.size() - base ||
                sizeof(value) > data.size() - base - offset) return false;
            std::memcpy(&value, data.data() + base + offset, sizeof(value));
            return true;
        };
        uintptr_t pointer = 0;
        uint8_t accepted = 0, counter = 0;
        if (!field("move_pointer", pointer) || !field("animation", player.throw_animation) ||
            !field("accepted", accepted) || !field("counter", counter) ||
            !field("input", player.raw_throw_input) || !field("tag", player.raw_throw_tag)) return;
        player.throw_fields_valid = true;
        player.throw_accepted = accepted != 0;
        player.throw_counter = counter != 0;
        player.ground_buffer_valid = field("ground_buffer", player.ground_buffer);
        // Ground continuations retain the idle tag. Inspect each changed move
        // pointer, including untagged moves; never scan the same list every tick.
        if (cache.pointer != pointer || cache.character != player.char_id) {
            cache.pointer = pointer;
            cache.character = player.char_id;
            cache.rules = ReadThrowRules(pointer, [](uintptr_t address, void* out, size_t size) {
                SIZE_T bytes = 0;
                return address && ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<LPCVOID>(address), out, size, &bytes) && bytes == size;
            });
        }
        player.throw_rules = cache.rules;
    }

    void read_spatial_player(const std::vector<uint8_t>& data, size_t player_offset, PlayerSnapshot& player) const {
        const auto section = c.find("SpatialPlayerData");
        if (section == c.end()) return; // Older INIs retain the center fallback.
        const auto read = [&](const char* key) -> std::optional<WorldPoint> {
            auto setting = section->second.find(key);
            if (setting == section->second.end() || setting->second.empty()) return std::nullopt;
            const auto offset = setting->second.front();
            if (player_offset > data.size() || offset > data.size() - player_offset) return std::nullopt;
            return decode_position(data, player_offset + offset);
        };
        player.contact_position = read("contact_position");
        player.attack_hitbox_position = read("attack_hitbox_position");
    }

    std::optional<CameraSnapshot> read_camera() const {
        if (reading_disabled) return std::nullopt;
        const auto section = c.find("SpatialCamera");
        if (section == c.end()) return std::nullopt;
        const auto trail = section->second.find("view_state_pointer");
        const auto view = section->second.find("view_matrix_offset");
        const auto projection = section->second.find("projection_matrix_offset");
        if (trail == section->second.end() || trail->second.empty() ||
            view == section->second.end() || view->second.empty() ||
            projection == section->second.end() || projection->second.empty()) return std::nullopt;
        // The optional camera may disappear during stage transitions. Unlike
        // the required frame data, failure here must not interrupt frame hints.
        const auto read = [](uintptr_t address, void* output, SIZE_T size) {
            SIZE_T bytes = 0;
            return address && ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<LPCVOID>(address), output, size, &bytes) && bytes == size;
        };
        uintptr_t camera = module_address;
        for (auto offset : trail->second) {
            if (!read(camera + offset, &camera, sizeof(camera)) || !camera) return std::nullopt;
        }
        CameraSnapshot result;
        if (!read(camera + view->second.front(), result.view, sizeof(result.view)) ||
            !read(camera + projection->second.front(), result.projection, sizeof(result.projection))) {
            return std::nullopt;
        }
        return result;
    }

    std::pair<int, std::vector<uint8_t>> get_frame(uintptr_t player_data_base_address, int rollback_frame) {
        if (rollback_frame < 0 || rollback_frame >= 32)
            throw std::runtime_error("Invalid rollback frame index");
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
        if (reading_disabled) throw ReadProcessMemoryException("Game data reading is disabled");
        if (size_of_block == 0) return {};
        if (size_of_block < 0) {
            disable_reading("Invalid memory block size: " + std::to_string(size_of_block));
            throw ReadProcessMemoryException("Invalid memory block size");
        }
        std::vector<uint8_t> buffer(size_of_block);
        SIZE_T bytes_read = 0;
        // Unlike memcpy, this safely reports inaccessible memory, including
        // ranges crossing a page boundary and memory unmapped during the read.
        const BOOL success = ReadProcessMemory(GetCurrentProcess(),
            reinterpret_cast<LPCVOID>(address), buffer.data(), buffer.size(), &bytes_read);
        const DWORD error = success ? ERROR_PARTIAL_COPY : GetLastError();
        if (!success || bytes_read != buffer.size()) {
            const auto reason = std::format("Address: 0x{:X}, size: {}, bytes read: {}, Win32 error: {}",
                address, size_of_block, bytes_read, error);
            disable_reading(reason);
            throw ReadProcessMemoryException(reason);
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
            // A null chain is normal while loading or outside a match.
            if (!address) return 0;
            if (trail[i] > UINTPTR_MAX - address) {
                disable_reading("Pointer trail address overflow");
                throw ReadProcessMemoryException("Pointer trail address overflow");
            }
            address = get_int_from_address(address + trail[i], 8);
        }
        return address;
    }

    static int get_4_bytes_from_data_block(const std::vector<uint8_t>& frame, int offset) {
        if (offset < 0 || static_cast<size_t>(offset) > frame.size() ||
            frame.size() - static_cast<size_t>(offset) < 4)
            throw std::runtime_error("Frame field is outside the memory block; check memory_address.ini");
        std::vector<uint8_t> bytes(frame.begin() + offset, frame.begin() + offset + 4);
        std::reverse(bytes.begin(), bytes.end());
        // Preserve signed DWORD fields such as actionable_frame and Heat FA.
        const auto value = bytes_to_int(bytes);
        return static_cast<int>(value >= 0x80000000ULL
            ? static_cast<int64_t>(value) - 0x100000000LL : static_cast<int64_t>(value));
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
            // ���������� ��� �������� (ANSI ������)
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

        // ������������ ��� ������ � ������ ������� ��� ���������
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
        // ��������� ������� � ������������ ������� �������
        HANDLE hProcess = OpenProcess(
            PROCESS_VM_READ | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION,
            FALSE,  // �� ��������� ����������
            pid
        );

        if (hProcess == NULL) {
            DWORD error = GetLastError();
            std::cerr << "Failed to open process (PID: " << pid
                << "). Error code: " << error << std::endl;

            // ��������� � ������������ �������, ���� ������ ������ �� ������
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
