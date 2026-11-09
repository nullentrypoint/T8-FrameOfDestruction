#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace VoiceChat {
struct Avatar { int resolution = 32; std::vector<uint8_t> rgba = std::vector<uint8_t>(32 * 32 * 4); };
struct PeerView {
    uint64_t steamId = 0;
    std::string name = "Opponent";
    std::shared_ptr<const Avatar> avatar;
    bool connected = false;
    int ping = -1;
    float qualityLocal = -1, qualityRemote = -1, outgoing = 0, incoming = 0;
    int pendingBytes = 0;
    uint64_t sent = 0, received = 0, failed = 0, connectedSeconds = 0;
};
struct VoiceNotice { PeerView peer; bool connected; };
inline std::mutex presentationMutex;
inline PeerView peerView;
inline PeerView localView;
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
