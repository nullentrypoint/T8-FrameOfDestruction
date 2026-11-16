#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "LoungeScene.h"

namespace VoiceChat {
struct Avatar { int resolution = 32; std::vector<uint8_t> rgba = std::vector<uint8_t>(32 * 32 * 4); };
struct PeerView {
    uint64_t steamId = 0;
    std::string name = "Opponent";
    std::string gameName;
    std::shared_ptr<const Avatar> avatar;
    bool connected = false;
    int ping = -1;
    float qualityLocal = -1, qualityRemote = -1, outgoing = 0, incoming = 0;
    int pendingBytes = 0;
    uint64_t sent = 0, received = 0, failed = 0, connectedSeconds = 0;
    float level = 0;
    uint64_t levelAt = 0, spokeAt = 0, micStateAt = 0;
    int micState = -1;
};
struct LoungeMemberView {
    PeerView peer;
    uintptr_t actor = 0;
    uint64_t cosmosId = 0;
    LoungePosition position;
    double distance = 0;
    int platform = -1;
    bool self = false;
};
struct LoungeView {
    LoungeReadStatus status = LoungeReadStatus::NotReady;
    uint64_t capturedAt = 0;
    uintptr_t world = 0;
    std::string roomKey;
    LoungeCameraLayout camera;
    std::vector<LoungeMemberView> members;
    bool customRoom = false;
};
struct VoiceNotice { PeerView peer; bool connected; };
inline std::mutex presentationMutex;
inline PeerView peerView;
inline PeerView localView;
inline LoungeView loungeView;
struct MatchVoicePair { uint64_t local = 0, remote = 0, capturedAt = 0; };
inline MatchVoicePair matchVoicePair;
inline std::atomic<bool> voiceDebugLog{false};
inline std::atomic<uint64_t> avatarPreviewUntil{0};
inline std::atomic<uint64_t> openProfileRequest{0};
inline std::atomic<int> localPlayerSlot{-1}; // 0=P1, 1=P2, -1=unknown
inline bool IsLocalHudSlot(int slot, int localSlot) { return slot == localSlot; }
inline std::vector<VoiceNotice> voiceNotices;
inline void PublishPeer(const PeerView& peer) {
    std::lock_guard lock(presentationMutex); peerView = peer;
}
inline void NotifyVoice(const PeerView& peer, bool connected) {
    std::lock_guard lock(presentationMutex);
    if (voiceNotices.size() == 8) voiceNotices.erase(voiceNotices.begin());
    voiceNotices.push_back({peer, connected});
}
}
