#pragma once
#include "PlayerIdentityReader.h"
#include <map>
#include <utility>

namespace VoiceChat {
struct IdentityEvent {
    bool appeared;
    PlayerIdentity player;
};

class IdentityTransitions {
    std::map<std::string, PlayerIdentity> previous;
public:
    std::vector<IdentityEvent> Update(const IdentitySnapshot& snapshot) {
        // A failed read is not proof that a player left. Ignore incomplete empty
        // placeholders, and track identity rather than heap address or selection.
        if (snapshot.status != IdentityReadStatus::Ok) return {};
        std::map<std::string, PlayerIdentity> current;
        for (const auto& player : snapshot.players) {
            if (!player.ids[1].empty() && (!player.ids[2].empty() || !player.ids[3].empty()))
                current.insert_or_assign(player.ids[1], player);
        }
        std::vector<IdentityEvent> events;
        for (const auto& [id, player] : previous)
            if (!current.contains(id)) events.push_back({false, player});
        for (const auto& [id, player] : current)
            if (!previous.contains(id)) events.push_back({true, player});
        previous = std::move(current);
        return events;
    }
};

inline std::string IdentityNotificationDetail(const PlayerIdentity& player) {
    std::string tekken = player.ids[1];
    if (tekken.size() == 12) tekken = tekken.substr(0, 4) + "-" + tekken.substr(4, 4) + "-" + tekken.substr(8);
    return "Tekken ID: " + tekken + "\nPlatform A: " + player.ids[2] +
        "\nPlatform B: " + player.ids[3] + "\nID (+0x60): " + player.ids[0];
}
}
