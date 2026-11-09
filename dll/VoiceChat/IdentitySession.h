#pragma once
#include "PlayerIdentityReader.h"
#include <optional>

namespace VoiceChat {
enum class IdentitySessionEventType { Started, Ended, Suspended, Resumed };

struct IdentitySessionState {
    uint64_t generation = 0;
    std::array<std::string, 4> localIds;
    std::array<std::string, 4> remoteIds;
};

struct IdentitySessionEvent {
    IdentitySessionEventType type;
    IdentitySessionState session;
    const char* reason;
};

// Tracks the accepted-opponent interval, including rematches. It neither
// identifies the transport platform nor claims that a voice link is connected.
class IdentitySession {
    std::optional<IdentitySessionState> session;
    uint64_t generation = 0;
    bool suspended = false;

    static bool Complete(const PlayerIdentity& player) {
        return !player.ids[1].empty() && (!player.ids[2].empty() || !player.ids[3].empty());
    }
public:
    bool HasVerifiedIdentityPair() const { return session.has_value() && !suspended; }
    const std::optional<IdentitySessionState>& Current() const { return session; }

    std::vector<IdentitySessionEvent> Update(const IdentitySnapshot& snapshot) {
        std::vector<IdentitySessionEvent> events;
        const auto suspend = [&](const char* reason) {
            if (session && !suspended) {
                suspended = true;
                events.push_back({IdentitySessionEventType::Suspended, *session, reason});
            }
        };
        const auto end = [&](const char* reason) {
            if (session) events.push_back({IdentitySessionEventType::Ended, *session, reason});
            session.reset();
            suspended = false;
        };
        if (snapshot.status != IdentityReadStatus::Ok) {
            suspend("identity-read-failed");
            return events;
        }
        const PlayerIdentity* local = nullptr;
        const PlayerIdentity* remote = nullptr;
        bool ambiguous = false;
        for (const auto& player : snapshot.players) {
            // Observed on the researched build: +8 is 1 for the persistent
            // local record, 3 for the confirmed online peer, 0 for placeholders.
            // This is a build-specific interpretation, not a public game enum.
            if (player.rawWords[0] == 1) {
                if (local) ambiguous = true;
                local = &player;
            } else if (player.rawWords[0] == 3) {
                if (remote) ambiguous = true;
                remote = &player;
            } else if (Complete(player)) ambiguous = true;
        }
        if (ambiguous) {
            suspend("ambiguous-player-records");
            return events;
        }
        if (!remote) {
            end("opponent-record-removed");
            return events;
        }
        if (!local || !Complete(*local) || !Complete(*remote)) {
            suspend("incomplete-player-records");
            return events;
        }
        if (local->ids[1] == remote->ids[1]) {
            suspend("same-local-and-remote-identity");
            return events;
        }
        if (session && session->localIds == local->ids && session->remoteIds == remote->ids) {
            if (suspended) {
                suspended = false;
                events.push_back({IdentitySessionEventType::Resumed, *session, "identity-pair-restored"});
            }
            return events;
        }
        end("identity-pair-replaced");
        session = IdentitySessionState{++generation, local->ids, remote->ids};
        events.push_back({IdentitySessionEventType::Started, *session, "opponent-record-present"});
        return events;
    }
};
}
