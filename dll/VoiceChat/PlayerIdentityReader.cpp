#include "../stdafx.h"
#include "PlayerIdentityReader.h"
#include <limits>
#include <utility>

namespace VoiceChat {
namespace {
    constexpr size_t MaxPlayers = 64;
    constexpr size_t MaxIdLength = 64;
    bool LocalRead(void*, uintptr_t address, void* output, size_t size) {
        SIZE_T copied = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), output, size, &copied) && copied == size;
    }
    struct VectorHeader {
        uintptr_t begin, end, capacity;
        bool operator==(const VectorHeader&) const = default;
    };
    static_assert(sizeof(VectorHeader) == 24);
}

const char* IdentityStatusName(IdentityReadStatus status) {
    switch (status) {
    case IdentityReadStatus::Ok: return "ok";
    case IdentityReadStatus::NotReady: return "not-ready";
    case IdentityReadStatus::UnsupportedBuild: return "unsupported-build";
    case IdentityReadStatus::Unreadable: return "unreadable";
    case IdentityReadStatus::InvalidData: return "invalid-data";
    case IdentityReadStatus::ChangedDuringRead: return "changed-during-read";
    }
    return "unknown";
}

PlayerIdentityReader::PlayerIdentityReader(uintptr_t moduleBase, IdentityLayout layout)
    : PlayerIdentityReader(moduleBase, LocalRead, nullptr, layout) {}

PlayerIdentityReader::PlayerIdentityReader(uintptr_t moduleBase, IdentityMemoryRead read, void* ctx, IdentityLayout offsets)
    : base(moduleBase), readMemory(read), context(ctx), layout(offsets) {}

bool PlayerIdentityReader::ReadBytes(uintptr_t address, void* output, size_t size) const {
    if (!readMemory || !address || size > (std::numeric_limits<uintptr_t>::max)() - address) return false;
    return readMemory(context, address, output, size);
}

IdentityReadStatus PlayerIdentityReader::ReadString(uintptr_t address, std::string& output) const {
    std::array<unsigned char, 32> header{}, again{};
    if (!ReadBytes(address, header.data(), header.size())) return IdentityReadStatus::Unreadable;
    uint64_t length = 0, capacity = 0;
    memcpy(&length, header.data() + 16, 8);
    memcpy(&capacity, header.data() + 24, 8);
    if (length > MaxIdLength || capacity < length || capacity > 4096 || (capacity < 16 && capacity != 15))
        return IdentityReadStatus::InvalidData;
    uintptr_t data = address;
    if (capacity >= 16) memcpy(&data, header.data(), sizeof(data));
    std::array<char, MaxIdLength + 1> bytes{};
    if (!ReadBytes(data, bytes.data(), static_cast<size_t>(length) + 1)) return IdentityReadStatus::Unreadable;
    if (bytes[length] != '\0') return IdentityReadStatus::InvalidData;
    for (size_t i = 0; i < length; ++i) {
        const auto ch = static_cast<unsigned char>(bytes[i]);
        if (ch < 0x20 || ch > 0x7E) return IdentityReadStatus::InvalidData;
    }
    if (!ReadBytes(address, again.data(), again.size())) return IdentityReadStatus::Unreadable;
    if (header != again) return IdentityReadStatus::ChangedDuringRead;
    output.assign(bytes.data(), static_cast<size_t>(length));
    return IdentityReadStatus::Ok;
}

IdentitySnapshot PlayerIdentityReader::Read() const {
    auto fail = [](IdentityReadStatus status) { return IdentitySnapshot{status, 0, {}}; };
    const auto max = (std::numeric_limits<uintptr_t>::max)();
    if (!base || !layout.rootRva || !layout.vtableRva || !layout.destructorRva ||
        layout.rootRva > max - base || layout.vtableRva > max - base || layout.destructorRva > max - base)
        return fail(IdentityReadStatus::UnsupportedBuild);
    uint16_t magic = 0;
    uintptr_t destructor = 0;
    if (!ReadBytes(base, &magic, sizeof(magic)) || magic != 0x5A4D ||
        !ReadBytes(base + layout.vtableRva, &destructor, sizeof(destructor)) || destructor != base + layout.destructorRva)
        return fail(IdentityReadStatus::UnsupportedBuild);
    uintptr_t root = 0;
    if (!ReadBytes(base + layout.rootRva, &root, sizeof(root))) return fail(IdentityReadStatus::Unreadable);
    if (!root) return fail(IdentityReadStatus::NotReady);
    VectorHeader header{};
    if (!ReadBytes(root, &header, sizeof(header))) return fail(IdentityReadStatus::Unreadable);
    if (header.end < header.begin || header.capacity < header.end ||
        header.capacity - header.begin > MaxPlayers * sizeof(uintptr_t) || (header.end - header.begin) % sizeof(uintptr_t))
        return fail(IdentityReadStatus::InvalidData);
    const size_t count = (header.end - header.begin) / sizeof(uintptr_t);
    std::array<uintptr_t, MaxPlayers> pointers{}, again{};
    if (count && !ReadBytes(header.begin, pointers.data(), count * sizeof(uintptr_t))) return fail(IdentityReadStatus::Unreadable);
    IdentitySnapshot result{IdentityReadStatus::Ok, root, {}};
    result.players.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const auto address = pointers[i];
        if (address > max - 0xE8) return fail(IdentityReadStatus::InvalidData);
        uintptr_t vtable = 0;
        if (!ReadBytes(address, &vtable, sizeof(vtable))) return fail(IdentityReadStatus::Unreadable);
        if (vtable != base + layout.vtableRva) return fail(IdentityReadStatus::InvalidData);
        PlayerIdentity identity;
        identity.address = address;
        if (!ReadBytes(address + 8, identity.rawWords.data(), sizeof(identity.rawWords))) return fail(IdentityReadStatus::Unreadable);
        for (size_t field = 0; field < identity.ids.size(); ++field) {
            const auto status = ReadString(address + 0x60 + field * 0x20, identity.ids[field]);
            if (status != IdentityReadStatus::Ok) return fail(status);
        }
        // Retry full string reads to detect observed content changes even when
        // length/capacity/pointer remain the same. This is not an atomic snapshot.
        for (size_t field = 0; field < identity.ids.size(); ++field) {
            std::string value;
            const auto status = ReadString(address + 0x60 + field * 0x20, value);
            if (status != IdentityReadStatus::Ok) return fail(status);
            if (value != identity.ids[field]) return fail(IdentityReadStatus::ChangedDuringRead);
        }
        std::array<uint32_t, 4> words{};
        if (!ReadBytes(address + 8, words.data(), sizeof(words)) || !ReadBytes(address, &vtable, sizeof(vtable)))
            return fail(IdentityReadStatus::Unreadable);
        if (words != identity.rawWords || vtable != base + layout.vtableRva) return fail(IdentityReadStatus::ChangedDuringRead);
        result.players.push_back(std::move(identity));
    }
    uintptr_t rootAgain = 0;
    VectorHeader headerAgain{};
    if (!ReadBytes(base + layout.rootRva, &rootAgain, sizeof(rootAgain)) || !ReadBytes(root, &headerAgain, sizeof(headerAgain)) ||
        (count && !ReadBytes(header.begin, again.data(), count * sizeof(uintptr_t)))) return fail(IdentityReadStatus::Unreadable);
    if (rootAgain != root || headerAgain != header || again != pointers) return fail(IdentityReadStatus::ChangedDuringRead);
    return result;
}
}
