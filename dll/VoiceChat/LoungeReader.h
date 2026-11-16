#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoiceChat {
struct LoungeLayout {
    uintptr_t worldRva = 0, nameBlocksRva = 0, subsystemRva = 0;
    uint32_t peTimestamp = 0, imageSize = 0;
};

enum class LoungeReadStatus {
    Ok, NotInLounge, NotReady, UnsupportedBuild, UnsupportedLayout,
    Unreadable, InvalidData, ChangedDuringRead
};
const char* LoungeStatusName(LoungeReadStatus status);

struct LoungePosition { double x = 0, y = 0, z = 0; };
struct LoungeParticipant {
    uintptr_t actor = 0;
    uint64_t cosmosId = 0;
    std::string userId, name;
    int32_t platform = -1;
    // Syntactically valid public individual Steam ID; not transport-verified.
    uint64_t steamIdCandidate = 0;
    LoungePosition position;
    double distance = 0;
};
struct LoungeSnapshot {
    LoungeReadStatus status = LoungeReadStatus::NotReady;
    uintptr_t world = 0, localActor = 0;
    std::string roomKey;
    LoungePosition localPosition;
    std::vector<LoungeParticipant> participants;
    uintptr_t gameMode = 0;
    bool customRoom = false; // Room-wide voice; world holds the current session identity.
};

using LoungeMemoryRead = bool(*)(void*, uintptr_t, void*, size_t);
// Read-only, no cached heap pointers. Failed snapshots never contain participants.
// Structural/identity rereads detect observed mutation, not every ABA race.
// Positions are best-effort samples, not synchronized across actors.
class LoungeReader {
public:
    LoungeReader(uintptr_t moduleBase, LoungeLayout layout, LoungeMemoryRead read, void* context);
    LoungeSnapshot Read() const;
    bool StillCurrent(const LoungeSnapshot& snapshot) const {
        uintptr_t world = 0, mode = 0;
        uint8_t flags[2]{};
        return snapshot.status == LoungeReadStatus::Ok && read &&
            read(context, base + layout.worldRva, &world, sizeof(world)) && world == snapshot.world &&
            read(context, world + 0x150, &mode, sizeof(mode)) && mode == snapshot.gameMode &&
            read(context, mode + 0x3EE, flags, sizeof(flags)) && flags[0] == 1 && flags[1] == 1;
    }
private:
    uintptr_t base;
    LoungeLayout layout;
    LoungeMemoryRead read;
    void* context;
};
}
