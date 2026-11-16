#pragma once
#include "LoungeReader.h"
#include "CustomLobbyReader.h"
#include "LoungeScene.h"
#include <Windows.h>
#include <charconv>
#include <filesystem>
#include <string_view>
#include <sstream>

namespace VoiceChat {
inline LoungeCameraLayout LoadLoungeCameraLayout(const std::filesystem::path& path) {
    LoungeCameraLayout result;
    const auto absolute = std::filesystem::absolute(path);
    const auto values = [&](const wchar_t* key) {
        wchar_t buffer[512]{};
        const auto count = GetPrivateProfileStringW(L"SpatialCamera", key, L"", buffer, 512, absolute.c_str());
        std::vector<uintptr_t> offsets;
        if (count >= 511) return offsets;
        std::wstring text(buffer, count);
        text.resize(text.find_first_of(L";#") == text.npos ? text.size() : text.find_first_of(L";#"));
        std::wistringstream stream(text);
        uintptr_t value;
        while (stream >> std::hex >> value) { if (value > 0x10000000 || offsets.size() >= 10) return std::vector<uintptr_t>{}; offsets.push_back(value); }
        if (!stream.eof()) offsets.clear();
        return offsets;
    };
    result.chain = values(L"view_state_pointer");
    const auto view = values(L"view_matrix_offset"), projection = values(L"projection_matrix_offset");
    if (view.size() == 1) result.viewOffset = view.front();
    if (projection.size() == 1) result.projectionOffset = projection.front();
    return result;
}
// Separate diagnostic configuration: missing lounge keys do not disable combat voice.
template<size_t N>
inline bool LoadRoomLayoutValues(const std::filesystem::path& path, const wchar_t* section,
    const wchar_t* const (&keys)[N], uint32_t (&values)[N], std::string& error) {
    error.clear();
    const auto absolute = std::filesystem::absolute(path);
    for (size_t i = 0; i < N; ++i) {
        wchar_t buffer[128]{};
        const auto length = GetPrivateProfileStringW(section, keys[i], L"", buffer, 128, absolute.c_str());
        std::wstring_view text(buffer, length);
        text = text.substr(0, text.find_first_of(L";#"));
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first != text.npos) text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
        else text = {};
        if (text.starts_with(L"0x") || text.starts_with(L"0X")) text.remove_prefix(2);
        std::string hex;
        for (const auto ch : text) hex += ch <= 127 ? static_cast<char>(ch) : '?';
        const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), values[i], 16);
        if (length >= 127 || parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size() || !values[i]) {
            error = "Missing or invalid memory_address.ini [";
            for (const auto ch : std::wstring_view(section)) error += static_cast<char>(ch);
            error += "] ";
            for (const auto ch : std::wstring_view(keys[i])) error += static_cast<char>(ch);
            return false;
        }
    }
    return true;
}
inline bool LoadLoungeLayout(const std::filesystem::path& path, LoungeLayout& output, std::string& error) {
    output = {};
    uint32_t values[5]{};
    const wchar_t* keys[] = {L"world_rva", L"name_blocks_rva", L"subsystem_rva", L"pe_timestamp", L"image_size"};
    if (!LoadRoomLayoutValues(path, L"VoiceLounge", keys, values, error)) return false;
    output = {values[0], values[1], values[2], values[3], values[4]};
    return true;
}
inline bool LoadCustomLobbyLayout(const std::filesystem::path& path, CustomLobbyLayout& output, std::string& error) {
    output = {};
    uint32_t v[7]{};
    const wchar_t* keys[] = {L"root_rva", L"owner_vtable_rva", L"session_vtable_rva", L"id_vtable_rva",
        L"member_vtable_rva", L"pe_timestamp", L"image_size"};
    if (!LoadRoomLayoutValues(path, L"VoiceCustomLobby", keys, v, error)) return false;
    output = {v[0], v[1], v[2], v[3], v[4], v[5], v[6]};
    return true;
}
}
