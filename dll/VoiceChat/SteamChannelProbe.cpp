#include "../stdafx.h"
#include "SteamChannelProbe.h"
#include "SteamDiagnostics.h"
#include "SteamProbeProtocol.h"
#include "VoiceAudio.h"
#include "SteamPeerProfile.h"
#include "PeerPreferences.h"
#include "../third_party/GameNetworkingSocketsHeaders/isteamnetworkingmessages.h"
#include "../third_party/GameNetworkingSocketsHeaders/isteamnetworkingsockets.h"
#include <bcrypt.h>
#include <charconv>
#include <utility>
#ifdef T8_PEER_TRACE_TEST
#include <cassert>
#endif
#pragma comment(lib, "bcrypt.lib")

namespace VoiceChat {
namespace {
    using namespace ProbeProtocol;
    static_assert(sizeof(SteamNetworkingIdentity) == 136);
    static_assert(offsetof(SteamNetworkingMessage_t, m_identityPeer) == 16);

    bool Read(uintptr_t address, void* output, size_t size) {
        SIZE_T copied = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &copied) && copied == size;
    }
    bool SteamCandidate(const std::array<std::string, 4>& ids, uint64_t& value) {
        if (ids[2].empty() || ids[2] != ids[3]) return false;
        const auto& text = ids[2];
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        // Public individual account, desktop instance, nonzero account ID.
        return result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
            (value >> 32) == 0x01100001ULL && uint32_t(value) != 0 && std::to_string(value) == text;
    }
    bool Tekken(const std::string& value, TekkenId& output) {
        if (value.size() != output.size()) return false;
        for (const unsigned char c : value)
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return false;
        memcpy(output.data(), value.data(), output.size());
        return true;
    }
    std::optional<Pair> SelectedPair(const IdentitySessionState& state) {
        Pair pair;
        if (!SteamCandidate(state.localIds, pair.localSteam) || !SteamCandidate(state.remoteIds, pair.remoteSteam) ||
            pair.localSteam == pair.remoteSteam || !Tekken(state.localIds[1], pair.localTekken) ||
            !Tekken(state.remoteIds[1], pair.remoteTekken)) return {};
        return pair;
    }
    SteamNetworkingIdentity Identity(uint64_t steamId) {
        SteamNetworkingIdentity result{};
        result.SetSteamID64(steamId);
        return result;
    }
}

struct SteamChannelProbe::Impl {
    Log log;
    VoiceAudio audio;
    SteamPeerProfile profile;
    PeerView peer;
    PeerView localPeer;
    uint64_t nextMicState = 0;
    uint32_t micSequence = 0, receivedMicSequence = 0;
    bool haveMicSequence = false;
    int lastMicState = -1;
    void ResetRemoteAudioState() {
        remoteMicState = -1; remoteMicStateAt = 0;
        opponentLevel = 0; remoteSpokeAt = 0; remoteLevelAt = 0;
        haveMicSequence = false; nextMicState = 0;
    }
    bool announced = false;
    uint64_t connectedAt = 0, nextStats = 0;
    void Disconnected() {
        ResetRemoteAudioState();
        if (announced) NotifyVoice(peer, false);
        announced = false;
        peer.connected = false;
        peer.ping = -1;
        peer.qualityLocal = peer.qualityRemote = -1;
        peer.incoming = peer.outgoing = 0;
        peer.pendingBytes = 0;
        PublishPeer(peer);
    }
    void RefreshPeer() {
        const auto now = GetTickCount64();
        if (now < nextStats) return;
        nextStats = now + 500;
        if (localPeer.steamId) {
            profile.Refresh(localPeer);
            std::lock_guard lock(presentationMutex); localView = localPeer;
        }
        if (!selected) return;
        profile.Refresh(peer);
        peer.connected = handshake.Ready();
        peer.sent = sentAudio; peer.received = receivedAudio; peer.failed = failedAudio;
        peer.connectedSeconds = peer.connected ? (now - connectedAt) / 1000 : 0;
        SteamNetConnectionRealTimeStatus_t stats{};
        const auto state = messages->GetSessionConnectionInfo(Identity(selected->remoteSteam), nullptr, &stats);
        if (state == k_ESteamNetworkingConnectionState_Connected) {
            peer.ping = stats.m_nPing;
            peer.qualityLocal = stats.m_flConnectionQualityLocal;
            peer.qualityRemote = stats.m_flConnectionQualityRemote;
            peer.outgoing = stats.m_flOutBytesPerSec;
            peer.incoming = stats.m_flInBytesPerSec;
            peer.pendingBytes = stats.m_cbPendingUnreliable + stats.m_cbPendingReliable;
        } else {
            peer.ping = -1; peer.qualityLocal = peer.qualityRemote = -1;
            peer.incoming = peer.outgoing = 0; peer.pendingBytes = 0;
        }
        PublishPeer(peer);
        if (peer.connected && !announced && (peer.avatar || now - connectedAt >= 3000)) {
            NotifyVoice(peer, true); announced = true;
        }
    }
    uint32_t sendSequence = 0, receivedSequence = 0;
    bool haveSequence = false;
    uint64_t sentAudio = 0, receivedAudio = 0, failedAudio = 0;
    ISteamNetworkingMessages* messages = nullptr;
    ISteamNetworkingSockets* sockets = nullptr;
    HMODULE factoryModule = nullptr, userModule = nullptr;
    std::optional<ProbeProtocol::Pair> selected;
    ProbeProtocol::Handshake handshake;
    uint64_t generation = 0, reportedGeneration = 0;
    uint64_t startedAt = 0, nextHello = 0, lastAck = 0, nextBind = 0;
    bool noReplyShown = false;
    int lastSendResult = -1;
    std::string lastBindError;

    uintptr_t userInitializerRva = 0;
    Impl(Log logger, uintptr_t initializer) : log(logger), userInitializerRva(initializer) {}
    ~Impl() {
        Stop();
        if (factoryModule) FreeLibrary(factoryModule);
        if (userModule) FreeLibrary(userModule);
    }
    void BindError(const char* error) {
        if (lastBindError == error) return;
        lastBindError = error;
        SetVoiceStatus(error);
        log("steam-probe binding=%s", error);
    }
    bool Bind(uint64_t expectedSteamId) {
        if (messages && sockets) {
            SteamNetworkingIdentity local{};
            if (sockets->GetIdentity(&local) && local.GetSteamID64() == expectedSteamId) return true;
            BindError("Steam identity does not match the local player.");
            return false;
        }
        // Recover the same two resolved import targets used by the game's
        // SteamUser021 initializer. No basename-based choice among SDK copies.
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (!userInitializerRva || userInitializerRva > UINTPTR_MAX - base) {
            BindError("Missing or invalid Steam initializer RVA in memory_address.ini."); return false;
        }
        const uintptr_t init = base + userInitializerRva;
        unsigned char code[33]{};
        const unsigned char prefix[] = {0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xFF,0x15};
        if (!Read(init, code, sizeof(code)) || memcmp(code, prefix, sizeof(prefix)) ||
            code[15] != 0x8B || code[16] != 0xC8 || code[17] != 0x48 || code[18] != 0x8D || code[19] != 0x15 ||
            code[24] != 0xFF || code[25] != 0x15 || code[30] != 0x48 || code[31] != 0x89 || code[32] != 0x03) {
            BindError("Unsupported Steam initializer layout."); return false;
        }
        int32_t relative = 0;
        memcpy(&relative, code + 20, 4);
        char version[sizeof("SteamUser021")]{};
        if (!Read(init + 24 + relative, version, sizeof(version)) || memcmp(version, "SteamUser021", sizeof(version))) {
            BindError("Steam user interface version did not match."); return false;
        }
        uintptr_t userTarget = 0, factoryTarget = 0;
        memcpy(&relative, code + 11, 4);
        if (!Read(init + 15 + relative, &userTarget, sizeof(userTarget))) return false;
        memcpy(&relative, code + 26, 4);
        if (!Read(init + 30 + relative, &factoryTarget, sizeof(factoryTarget))) return false;
        HMODULE userOwner = nullptr, factoryOwner = nullptr;
        const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS;
        if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(userTarget), &userOwner)) return false;
        if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(factoryTarget), &factoryOwner)) {
            FreeLibrary(userOwner); return false;
        }
        const auto releaseOwners = [&] { FreeLibrary(userOwner); FreeLibrary(factoryOwner); };
        if (userOwner == GetModuleHandleW(nullptr) || factoryOwner == GetModuleHandleW(nullptr)) {
            releaseOwners(); BindError("Waiting for the game to initialize Steam."); return false;
        }
        if (GetProcAddress(userOwner, "SteamAPI_GetHSteamUser") != reinterpret_cast<FARPROC>(userTarget) ||
            GetProcAddress(factoryOwner, "SteamInternal_FindOrCreateUserInterface") != reinterpret_cast<FARPROC>(factoryTarget)) {
            releaseOwners(); BindError("Resolved Steam imports could not be verified."); return false;
        }
        if (!SteamDiagnostics::GuardSteamShutdown(factoryOwner)) {
            releaseOwners(); BindError("Could not guard Steam SDK shutdown."); return false;
        }
        using GetUser = int32_t(__cdecl*)();
        using GetInterface = void*(__cdecl*)(int32_t, const char*);
        const auto user = reinterpret_cast<GetUser>(userTarget)();
        const auto factory = reinterpret_cast<GetInterface>(factoryTarget);
        if (!user) { releaseOwners(); BindError("The game has no active Steam user."); return false; }
        auto* candidateMessages = static_cast<ISteamNetworkingMessages*>(factory(user, STEAMNETWORKINGMESSAGES_INTERFACE_VERSION));
        auto* candidateSockets = static_cast<ISteamNetworkingSockets*>(factory(user, STEAMNETWORKINGSOCKETS_INTERFACE_VERSION));
        if (!candidateMessages || !candidateSockets) {
            releaseOwners(); BindError("Steam networking interfaces are unavailable in this game context."); return false;
        }
        SteamNetworkingIdentity local{};
        if (!candidateSockets->GetIdentity(&local) || local.GetSteamID64() != expectedSteamId) {
            releaseOwners(); BindError("Steam networking returned a different local identity."); return false;
        }
        messages = candidateMessages;
        sockets = candidateSockets;
        factoryModule = factoryOwner;
        userModule = userOwner;
        profile.Bind(factoryModule, user);
        localPeer.steamId = expectedSteamId;
        profile.Refresh(localPeer);
        { std::lock_guard lock(presentationMutex); localView = localPeer; }
        log("voice-profile flat-Steam-API available=%d", profile.Available());
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(factoryModule, path, MAX_PATH);
        log("steam-probe bound sdk=%ls localSteam=%llu messages=%p sockets=%p channel=%d",
            path, static_cast<unsigned long long>(expectedSteamId), messages, sockets, ProbeProtocol::Channel);
        lastBindError.clear();
        return true;
    }
    void Send(const ProbeProtocol::Wire& packet) {
        if (!selected || !messages) return;
        const auto peer = Identity(selected->remoteSteam);
        const auto result = messages->SendMessageToUser(peer, packet.data(), static_cast<uint32>(packet.size()),
            k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle | k_nSteamNetworkingSend_AutoRestartBrokenSession,
            ProbeProtocol::Channel);
        if (lastSendResult != static_cast<int>(result)) {
            lastSendResult = static_cast<int>(result);
            log("steam-probe send-result=%d peer=%llu (API acceptance, not delivery)", lastSendResult,
                static_cast<unsigned long long>(selected->remoteSteam));
        }
    }
    void Stop() {
        if (!selected) return;
        audio.Stop();
        Disconnected();
        SetVoiceStatus("Waiting for opponent");
        log("voice-audio summary sent=%llu received=%llu send-failed=%llu",
            static_cast<unsigned long long>(sentAudio), static_cast<unsigned long long>(receivedAudio),
            static_cast<unsigned long long>(failedAudio));
        if (const auto bye = handshake.End()) Send(*bye);
        const auto peer = Identity(selected->remoteSteam);
        if (messages) messages->CloseChannelWithUser(peer, ProbeProtocol::Channel);
        log("steam-probe stopped generation=%llu peer=%llu", static_cast<unsigned long long>(generation),
            static_cast<unsigned long long>(selected->remoteSteam));
        selected.reset();
        generation = 0;
        lastAck = 0;
    }
    void Prepare(const IdentitySnapshot& snapshot) {
        // Check SDK availability in the menu, before arranging the two-player test.
        if (messages || snapshot.status != IdentityReadStatus::Ok || GetTickCount64() < nextBind) return;
        const PlayerIdentity* local = nullptr;
        for (const auto& player : snapshot.players) {
            if (player.rawWords[0] != 1) continue;
            if (local) return;
            local = &player;
        }
        uint64_t steamId = 0;
        if (!local || !SteamCandidate(local->ids, steamId)) return;
        nextBind = GetTickCount64() + 5000;
        if (Bind(steamId)) {
            SetVoiceStatus("Steam ready; waiting for opponent");
        }
    }
    void Update(const IdentitySession& identities) {
        if (!identities.HasVerifiedIdentityPair() || !identities.Current()) { Stop(); return; }
        const auto& session = *identities.Current();
        const auto pair = SelectedPair(session);
        if (!pair) {
            Stop();
            if (reportedGeneration != session.generation) {
                reportedGeneration = session.generation;
                SetVoiceStatus("Unsupported opponent (Steam players only)");
                log("steam-probe skipped generation=%llu: pair is not a canonical Steam candidate",
                    static_cast<unsigned long long>(session.generation));
            }
            return;
        }
        if (selected && (generation != session.generation || *selected != *pair)) Stop();
        const uint64_t now = GetTickCount64();
        if (!selected) {
            if (!messages && now < nextBind) return;
            nextBind = now + 5000;
            if (!Bind(pair->localSteam)) return;
            ProbeProtocol::Nonce nonce{};
            if (BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
                ProbeProtocol::Empty(nonce)) { BindError("Could not generate a fresh handshake nonce."); return; }
            selected = *pair;
            peer = {}; peer.steamId = pair->remoteSteam;
            ResetRemoteAudioState();
            profile.Refresh(peer); PublishPeer(peer);
            nextStats = 0;
            generation = session.generation;
            handshake.Begin(*pair, nonce);
            sendSequence = receivedSequence = 0; haveSequence = false;
            sentAudio = receivedAudio = failedAudio = 0;
            SetVoiceStatus("Connecting to SteamID " + std::to_string(pair->remoteSteam));
            startedAt = now;
            lastAck = 0;
            nextHello = 0;
            lastSendResult = -1;
            noReplyShown = false;
            log("steam-probe started generation=%llu local=%llu remote=%llu channel=%d protocol=1",
                static_cast<unsigned long long>(generation), static_cast<unsigned long long>(pair->localSteam),
                static_cast<unsigned long long>(pair->remoteSteam), ProbeProtocol::Channel);
        }
        if (now >= nextHello) {
            if (const auto hello = handshake.Hello()) Send(*hello);
            nextHello = now + (handshake.Ready() ? 2000 : 1000);
        }
        const auto preference = peerPreferences.Get(selected->remoteSteam);
        audio.SetReceive(preference.volume, preference.is_mute);
        // Own channel only, bounded work. Sending HELLO in both directions
        // implicitly accepts each other's Steam message session requests.
        for (int batch = 0; batch < 4; ++batch) {
            SteamNetworkingMessage_t* incoming[8]{};
            const int count = messages->ReceiveMessagesOnChannel(ProbeProtocol::Channel, incoming, 8);
            if (count <= 0) break;
            for (int i = 0; i < count && i < 8; ++i) {
                auto* message = incoming[i];
                if (!message) continue;
                if (message->m_nChannel == ProbeProtocol::Channel && message->m_pData && message->m_cbSize == 85) {
                    const auto* bytes = static_cast<const uint8_t*>(message->m_pData);
                    if (bytes[84] <= 1 && handshake.AcceptData(message->m_identityPeer.GetSteamID64(), {bytes, 80}, ProbeProtocol::Type::MicState)) {
                        uint32_t sequence = 0;
                        for (int n = 0; n < 4; ++n) sequence |= uint32_t(bytes[80+n]) << (n*8);
                        if (!haveMicSequence || (sequence != receivedMicSequence && uint32_t(sequence - receivedMicSequence) < 0x80000000u)) {
                            receivedMicSequence = sequence; haveMicSequence = true;
                            remoteMicState = bytes[84]; remoteMicStateAt = now;
                            if (bytes[84]) { opponentLevel = 0; remoteSpokeAt = 0; }
                        }
                    }
                }
                if (message->m_nChannel == ProbeProtocol::Channel && message->m_pData && message->m_cbSize == 724) {
                    const auto* bytes = static_cast<const uint8_t*>(message->m_pData);
                    if (handshake.AcceptAudio(message->m_identityPeer.GetSteamID64(), {bytes, 80})) {
                        uint32_t sequence = 0;
                        for (int n = 0; n < 4; ++n) sequence |= uint32_t(bytes[80+n]) << (n*8);
                        if (!haveSequence || (sequence != receivedSequence && uint32_t(sequence - receivedSequence) < 0x80000000u)) {
                            receivedSequence = sequence; haveSequence = true;
                            if (++receivedAudio == 1) log("voice-audio first authenticated PCM packet received");
                            const float level = PcmLevel(bytes + 84);
                            opponentLevel = level; remoteLevelAt = now;
                            if (level > .025f) remoteSpokeAt = now;
                            audio.Play(bytes + 84);
                        }
                    }
                }
                if (message->m_nChannel == ProbeProtocol::Channel && message->m_pData && message->m_cbSize == ProbeProtocol::PacketSize) {
                    const bool wasReady = handshake.Ready();
                    const auto previousAudioHeader = handshake.AudioHeader();
                    const auto receipt = handshake.Receive(message->m_identityPeer.GetSteamID64(),
                        {static_cast<const uint8_t*>(message->m_pData), ProbeProtocol::PacketSize});
                    if (previousAudioHeader != handshake.AudioHeader()) {
                        haveSequence = false;
                        ResetRemoteAudioState();
                        audio.Stop();
                    }
                    if (receipt.reply) Send(*receipt.reply);
                    if (receipt.acknowledged) {
                        lastAck = now;
                        if (!wasReady) {
                            connectedAt = now;
                            haveSequence = false;
                            SetVoiceStatus("Connected to SteamID " + std::to_string(selected->remoteSteam));
                            log("steam-probe verified generation=%llu remote=%llu: nonce ACK matched",
                                static_cast<unsigned long long>(generation), static_cast<unsigned long long>(selected->remoteSteam));
                        }
                    }
                    if (receipt.peerClosed) {
                        Disconnected();
                        audio.Stop();
                        SetVoiceStatus("Peer disconnected");
                        log("steam-probe peer-bye remote=%llu", static_cast<unsigned long long>(selected->remoteSteam));
                    }
                }
                message->Release();
            }
            if (count < 8) break;
        }
        if (handshake.Ready() && now - lastAck > 10000) {
            handshake.MarkTimedOut();
            Disconnected();
            audio.Stop();
            SetVoiceStatus("Reconnecting (ACK timeout)");
            log("steam-probe ACK timeout remote=%llu", static_cast<unsigned long long>(selected->remoteSteam));
        }
        if (!handshake.Ready() && !noReplyShown && now - startedAt >= 30000) {
            noReplyShown = true;
            log("steam-probe no-ACK-after-30s remote=%llu", static_cast<unsigned long long>(selected->remoteSteam));
            SetVoiceStatus("Opponent has not joined voice chat");
        }
    }
    void AudioTick() {
        if (const auto id = openProfileRequest.exchange(0); id && !profile.OpenProfile(id))
            log("voice-profile overlay unavailable");
        const bool ready = selected && handshake.Ready();
        const bool peerMuted = selected && peerPreferences.Get(selected->remoteSteam).is_mute;
        audio.Update(ready, peerMuted);
        if (ready && messages) {
            const int state = (!voiceEnabled || microphoneMuted || peerMuted || microphoneTest || !captureActive) ? 1 : 0;
            const auto now = GetTickCount64();
            if (state != lastMicState || now >= nextMicState) {
                if (const auto header = handshake.MicStateHeader()) {
                    std::array<uint8_t, 85> packet{};
                    memcpy(packet.data(), header->data(), 80);
                    const uint32_t sequence = ++micSequence;
                    for (int n = 0; n < 4; ++n) packet[80+n] = static_cast<uint8_t>(sequence >> (n*8));
                    packet[84] = static_cast<uint8_t>(state);
                    messages->SendMessageToUser(Identity(selected->remoteSteam), packet.data(), static_cast<uint32>(packet.size()),
                        k_nSteamNetworkingSend_UnreliableNoDelay, ProbeProtocol::Channel);
                }
                lastMicState = state; nextMicState = now + 500;
            }
        }
        audio.Capture([&](const short* pcm) {
            if (!ready || !messages || peerPreferences.Get(selected->remoteSteam).is_mute) return;
            const auto header = handshake.AudioHeader();
            if (!header) return;
            std::array<uint8_t, 724> packet{};
            memcpy(packet.data(), header->data(), 80);
            const uint32_t sequence = ++sendSequence;
            for (int i = 0; i < 4; ++i) packet[80+i] = static_cast<uint8_t>(sequence >> (i*8));
            memcpy(packet.data()+84, pcm, 640);
            const auto result = messages->SendMessageToUser(Identity(selected->remoteSteam), packet.data(), static_cast<uint32>(packet.size()),
                k_nSteamNetworkingSend_UnreliableNoDelay, ProbeProtocol::Channel);
            if (result == k_EResultOK) {
                if (++sentAudio == 1) log("voice-audio first captured PCM packet accepted by Steam");
            } else if (++failedAudio == 1) log("voice-audio send failed result=%d", static_cast<int>(result));
        });
    }
};

SteamChannelProbe::SteamChannelProbe(Log log, uintptr_t userInitializerRva)
    : impl(std::make_unique<Impl>(log, userInitializerRva)) {}
SteamChannelProbe::~SteamChannelProbe() = default;
void SteamChannelProbe::Update(const IdentitySnapshot& snapshot, const IdentitySession& identities) {
    impl->Prepare(snapshot);
    impl->Update(identities);
    impl->AudioTick();
    impl->RefreshPeer();
}
void SteamChannelProbe::Stop() {
    microphoneTest = false;
    impl->Stop();
    impl->audio.Stop();
}

#ifdef T8_PEER_TRACE_TEST
void TestSteamProbeCandidates() {
    // Fixed synthetic identities, unrelated to captured players.
    IdentitySessionState state{1,
        {"628491735206819453", "7rQ2mK9vXp4A", "76561202131974853", "76561202131974853"},
        {"716384925018273649", "6tN8bR3wYk5C", "76561202084629517", "76561202084629517"}};
    assert(SelectedPair(state));
    state.remoteIds = {"583729164028375691", "8pD4sH7zWq2E", "test_q7m4", "8274619503827164953"};
    assert(!SelectedPair(state));
    state.remoteIds[2] = state.remoteIds[3];
    assert(!SelectedPair(state));
    state.remoteIds = state.localIds;
    assert(!SelectedPair(state));
    state.remoteIds = {"", "6tN8bR3wYk5C", "76561202084629517x", "76561202084629517x"};
    assert(!SelectedPair(state));
    state.remoteIds[2] = state.remoteIds[3] = "18446744073709551616";
    assert(!SelectedPair(state));
}
#endif
}
