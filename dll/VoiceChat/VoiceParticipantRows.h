#pragma once
#include "VoicePresentation.h"
#include <algorithm>
#include <Windows.h>

namespace VoiceChat {
inline bool FreshLounge(const LoungeView& view, uint64_t now) {
    return view.status == LoungeReadStatus::Ok && view.capturedAt && now >= view.capturedAt &&
        now - view.capturedAt < 1000;
}
inline std::wstring VoiceNameWide(const std::string& name) {
    if (name.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (!size) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), result.data(), size);
    return result;
}
inline int CompareVoiceGameNames(const std::string& a, const std::string& b) {
    if (a.empty() != b.empty()) return a.empty() ? 1 : -1;
    const auto left = VoiceNameWide(a), right = VoiceNameWide(b);
    if (!left.empty() && !right.empty()) {
        const int order = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_STRINGSORT,
            left.data(), static_cast<int>(left.size()), right.data(), static_cast<int>(right.size()), nullptr, nullptr, 0);
        if (order) return order - CSTR_EQUAL;
    }
    return a.compare(b);
}
inline void SortVoiceParticipants(std::vector<LoungeMemberView>& rows) {
    std::erase_if(rows, [](const auto& row) { return !row.self && !row.peer.steamId; });
    const auto group = [](const auto& row) { return row.self ? 0 : row.peer.connected ? 1 : 2; };
    std::stable_sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
        if (group(a) != group(b)) return group(a) < group(b);
        const auto order = CompareVoiceGameNames(a.peer.gameName, b.peer.gameName);
        if (order) return order < 0;
        if (a.peer.steamId != b.peer.steamId) return a.peer.steamId < b.peer.steamId;
        return a.cosmosId < b.cosmosId;
    });
}
inline std::vector<LoungeMemberView> VoiceParticipantRows(const LoungeView& lounge,
    const PeerView& peer, const PeerView& local, uint64_t now) {
    std::vector<LoungeMemberView> result;
    if (lounge.status == LoungeReadStatus::Ok) {
        if (FreshLounge(lounge, now)) result = lounge.members;
    } else {
        if (local.steamId) { LoungeMemberView row; row.peer = local; row.self = true; result.push_back(row); }
        if (peer.steamId) { LoungeMemberView row; row.peer = peer; result.push_back(row); }
    }
    SortVoiceParticipants(result);
    return result;
}
inline float FreshVoiceLevel(const PeerView& peer, uint64_t now) {
    return peer.connected && peer.levelAt && now >= peer.levelAt && now - peer.levelAt < 250 ?
        (std::isfinite(peer.level) ? std::clamp(peer.level, 0.0f, 1.0f) : 0.0f) : 0.0f;
}
inline bool FreshMatchVoicePair(const MatchVoicePair& pair, uint64_t now) {
    return pair.local && pair.remote && now >= pair.capturedAt && now - pair.capturedAt < 500;
}
inline std::vector<LoungeMemberView> CustomRoomHudRows(const LoungeView& view, uint64_t now,
    const MatchVoicePair& fighters = {}) {
    if (!view.customRoom || !FreshLounge(view, now)) return {};
    auto rows = view.members;
    std::erase_if(rows, [](const auto& row) { return !row.peer.steamId || !row.peer.connected; });
    if (FreshMatchVoicePair(fighters, now))
        std::erase_if(rows, [&](const auto& row) { return row.peer.steamId != fighters.local && row.peer.steamId != fighters.remote; });
    SortVoiceParticipants(rows);
    return rows;
}
struct RoomHudTile { float x, y, size; };
inline RoomHudTile CustomRoomHudTile(float width, float height, size_t count, size_t index) {
    const float scale = std::clamp(height / 1080.0f, .75f, 1.5f);
    const float margin = 16 * scale;
    const float pitch = std::min(54 * scale, std::max(0.0f, width - margin * 2) / std::max(size_t{1}, count));
    const float size = pitch * (42.0f / 54.0f);
    return {width - margin - size - static_cast<float>(index) * pitch, margin, size};
}
}
