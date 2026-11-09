#pragma once
#include <Windows.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <algorithm>
#include <charconv>

namespace VoiceChat {
struct PeerPreference { int volume = 100; bool is_mute = false; };
class PeerPreferences {
    std::mutex mutex;
    std::filesystem::path path;
    std::map<uint64_t, PeerPreference> cache;
    std::string error;
public:
    void Configure(const std::filesystem::path& userSettings) {
        std::lock_guard lock(mutex);
        const auto next = userSettings.parent_path() / "voice_peers.ini";
        if (path != next) { path = next; cache.clear(); error.clear(); }
    }
    PeerPreference Get(uint64_t id) {
        std::lock_guard lock(mutex);
        if (!id || path.empty()) return {};
        if (const auto found = cache.find(id); found != cache.end()) return found->second;
        const auto section = std::to_wstring(id);
        wchar_t volume[32]{}, mute[16]{};
        GetPrivateProfileStringW(section.c_str(), L"volume", L"100", volume, 32, path.c_str());
        GetPrivateProfileStringW(section.c_str(), L"is_mute", L"false", mute, 16, path.c_str());
        wchar_t* end = nullptr;
        const long parsed = wcstol(volume, &end, 10);
        PeerPreference result;
        if (end != volume && !*end && parsed >= 0 && parsed <= 100) result.volume = static_cast<int>(parsed);
        result.is_mute = wcscmp(mute, L"true") == 0;
        cache[id] = result;
        return result;
    }
    bool Set(uint64_t id, PeerPreference value) {
        std::lock_guard lock(mutex);
        if (!id || path.empty()) { error = "Opponent settings file is not initialized"; return false; }
        value.volume = std::clamp(value.volume, 0, 100);
        cache[id] = value; // Apply immediately even if saving fails.
        const auto section = std::to_wstring(id);
        std::wstring values = L"volume=" + std::to_wstring(value.volume);
        values.push_back(0);
        values += value.is_mute ? L"is_mute=true" : L"is_mute=false";
        values.push_back(0); values.push_back(0);
        const bool ok = WritePrivateProfileSectionW(section.c_str(), values.c_str(), path.c_str()) != FALSE;
        error = ok ? "" : "Could not save voice_peers.ini";
        return ok;
    }
    std::string Error() { std::lock_guard lock(mutex); return error; }
};
inline PeerPreferences peerPreferences;
}
