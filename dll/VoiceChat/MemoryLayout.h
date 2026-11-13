#pragma once
#include "PlayerIdentityReader.h"
#include <Windows.h>
#include <charconv>
#include <filesystem>
#include <string>
#include <string_view>

namespace VoiceChat {
struct MemoryLayout {
    IdentityLayout identity;
    uintptr_t userInitializer = 0, userContext = 0;
    uintptr_t networkingInitializer = 0, networkingContext = 0;
    uintptr_t matchmakingInitializer = 0, matchmakingContext = 0;
    uintptr_t peerCallA = 0, peerCallB = 0, peerMethod = 0;
    uintptr_t profileEntry = 0, profileOverlayCall = 0, profileDialog = 0;
};

// Read once at worker startup. No compiled fallback addresses: a missing or
// malformed voice layout leaves the whole layout disabled, never partly loaded.
inline bool LoadMemoryLayout(const std::filesystem::path& path, MemoryLayout& output, std::string& error) {
    output = {};
    error.clear();
    MemoryLayout candidate;
    struct Field { const wchar_t* section; const wchar_t* key; uintptr_t* value; };
    const Field fields[] = {
        {L"VoiceIdentity", L"root_rva", &candidate.identity.rootRva},
        {L"VoiceIdentity", L"vtable_rva", &candidate.identity.vtableRva},
        {L"VoiceIdentity", L"destructor_rva", &candidate.identity.destructorRva},
        {L"VoiceSteam", L"user_initializer_rva", &candidate.userInitializer},
        {L"VoiceSteam", L"user_context_rva", &candidate.userContext},
        {L"VoiceSteam", L"networking_initializer_rva", &candidate.networkingInitializer},
        {L"VoiceSteam", L"networking_context_rva", &candidate.networkingContext},
        {L"VoiceSteam", L"matchmaking_initializer_rva", &candidate.matchmakingInitializer},
        {L"VoiceSteam", L"matchmaking_context_rva", &candidate.matchmakingContext},
        {L"VoicePeerTrace", L"call_a_rva", &candidate.peerCallA},
        {L"VoicePeerTrace", L"call_b_rva", &candidate.peerCallB},
        {L"VoicePeerTrace", L"steamclient_method_rva", &candidate.peerMethod},
        {L"VoiceProfileTrace", L"entry_rva", &candidate.profileEntry},
        {L"VoiceProfileTrace", L"overlay_call_rva", &candidate.profileOverlayCall},
        {L"VoiceProfileTrace", L"dialog_rva", &candidate.profileDialog},
    };
    const auto absolute = std::filesystem::absolute(path);
    for (const auto& field : fields) {
        wchar_t buffer[128]{};
        const auto length = GetPrivateProfileStringW(field.section, field.key, L"", buffer, 128, absolute.c_str());
        std::wstring_view text(buffer, length);
        text = text.substr(0, text.find_first_of(L";#"));
        const auto first = text.find_first_not_of(L" \t\r\n");
        if (first != text.npos) text = text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
        else text = {};
        if (text.starts_with(L"0x") || text.starts_with(L"0X")) text.remove_prefix(2);
        std::string hex;
        for (const auto ch : text) hex.push_back(ch <= 127 ? static_cast<char>(ch) : '?');
        uint32_t value = 0;
        const auto parsed = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
        if (length >= 127 || parsed.ec != std::errc{} || parsed.ptr != hex.data() + hex.size() || !value) {
            error = "Missing or invalid memory_address.ini [";
            // Section/key names above are ASCII literals, independent of locale.
            for (const auto ch : std::wstring_view(field.section)) error += static_cast<char>(ch);
            error += "] ";
            for (const auto ch : std::wstring_view(field.key)) error += static_cast<char>(ch);
            return false;
        }
        *field.value = value;
    }
    output = candidate;
    return true;
}
}
