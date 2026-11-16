#include "VoiceChat/LoungeLayout.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <unordered_map>

using namespace VoiceChat;
namespace {
struct Fixture {
    static constexpr uintptr_t Base = 0x10000000;
    static constexpr uintptr_t World = Base + 0x20000, Mode = Base + 0x21000;
    static constexpr uintptr_t Subsystem = Base + 0x22000, Network = Base + 0x23000;
    static constexpr uintptr_t Instance = Base + 0x24000, LocalArray = Base + 0x25000;
    static constexpr uintptr_t Local = Base + 0x26000, Controller = Base + 0x27000;
    static constexpr uintptr_t Pawn = Base + 0x28000, LocalRoot = Base + 0x29000;
    static constexpr uintptr_t Manager = Base + 0x2A000, Entries = Base + 0x2B000;
    static constexpr uintptr_t Remote = Base + 0x2C000, Remote2 = Base + 0x2D000;
    static constexpr uintptr_t RemoteRoot = Base + 0x2E000, RemoteRoot2 = Base + 0x2F000;
    static constexpr uintptr_t NameBlock = Base + 0x80000;
    LoungeLayout layout{0x1000, 0x2000, 0x1100, 0x12345678, 0x100000};
    std::vector<unsigned char> memory = std::vector<unsigned char>(0x100000);
    uintptr_t heap = Base + 0x40000;
    uint32_t nextName = 0;
    std::unordered_map<std::string, uintptr_t> classes;
    uintptr_t changeAddress = 0, blockedAddress = 0;
    int reads = 0;

    template<class T> void Put(uintptr_t address, const T& value) {
        assert(address >= Base && address - Base + sizeof(T) <= memory.size());
        memcpy(memory.data() + address - Base, &value, sizeof(T));
    }
    uintptr_t Allocate(size_t n = 0x100) { const auto p = heap; heap += n; return p; }
    uint32_t Name(const std::string& value) {
        const auto id = nextName;
        Put<uint16_t>(NameBlock + id * 2, static_cast<uint16_t>(value.size() << 6));
        memcpy(memory.data() + NameBlock + id * 2 + 2 - Base, value.data(), value.size());
        nextName += static_cast<uint32_t>((value.size() + 3) / 2);
        return id;
    }
    void Class(uintptr_t object, const std::string& name) {
        auto& type = classes[name];
        if (!type) { type = Allocate(); Put(type + 0x18, Name(name)); }
        Put(object + 0x10, type);
    }
    uintptr_t String(uintptr_t address, const std::u16string& value) {
        const auto p = Allocate((value.size() + 1) * 2 + 16);
        memcpy(memory.data() + p - Base, value.c_str(), (value.size() + 1) * 2);
        Put(address, p);
        Put<int32_t>(address + 8, static_cast<int32_t>(value.size() + 1));
        Put<int32_t>(address + 12, static_cast<int32_t>(value.size() + 1));
        return p;
    }
    void Peer(uintptr_t p, uintptr_t root, uint64_t cosmos, int platform, const std::u16string& id) {
        Class(p, "BP_AvatarEnemy_Onl_C");
        Put(p + 0x390, cosmos);
        Put<int32_t>(p + 0x2A0, platform);
        Put(p + 0x2D8, Name("synthetic-user"));
        Put(p + 0x1A0, root);
        const auto display = Allocate();
        Put(p + 0x380, display);
        Put<int32_t>(p + 0x388, 3); Put<int32_t>(p + 0x38C, 3);
        String(display, u"Test \u0418\u0433\u0440\u043e\u043a \U0001F600");
        String(display + 16, id);
        String(display + 32, u"synthetic-id");
    }
    Fixture() {
        Put<uint16_t>(Base, 0x5A4D); Put<uint32_t>(Base + 0x3C, 0x80);
        Put<uint32_t>(Base + 0x80, 0x4550); Put<uint16_t>(Base + 0x84, 0x8664);
        Put(Base + 0x88, layout.peTimestamp); Put<uint16_t>(Base + 0x98, 0x20B);
        Put(Base + 0xD0, layout.imageSize);
        Put(Base + layout.worldRva, World); Put(Base + layout.subsystemRva, Subsystem);
        Put(Base + layout.nameBlocksRva, NameBlock); assert(Name("None") == 0);
        Put(World + 0x150, Mode); Class(Mode, "BP_LobbyGameMode_Onl_C");
        Put<uint8_t>(Mode + 0x3EE, 1); Put<uint8_t>(Mode + 0x3EF, 1);
        Class(Subsystem, "LobbyGameInstanceSubsystem"); Put<uint8_t>(Subsystem + 0x1B1, 1);
        Put(Subsystem + 0x2A0, Network); Class(Network, "LobbyNetworkObject");
        String(Network + 0x140, u"SYNTHETIC_ROOM_A");
        Put(World + 0x1B8, Instance); Put(Instance + 0x38, LocalArray);
        Put<int32_t>(Instance + 0x40, 1); Put<int32_t>(Instance + 0x44, 1);
        Put(LocalArray, Local); Put(Local + 0x30, Controller); Put(Controller + 0x340, Pawn);
        Class(Pawn, "BP_Player_Onl_C"); Put(Pawn + 0x1A0, LocalRoot);
        Put(LocalRoot + 0x260, LoungePosition{0, 0, 0});
        Put(Mode + 0x488, Manager); Class(Manager, "LobbyEnemyManager");
        const auto m = Manager + 0x28;
        Put(m, Entries); Put<int32_t>(m + 8, 34); Put<int32_t>(m + 12, 40);
        Put<uint32_t>(m + 0x10, 1); Put<uint32_t>(m + 0x14, 2);
        Put<int32_t>(m + 0x28, 34); Put<int32_t>(m + 0x2C, 128);
        Put<int32_t>(m + 0x30, 1); Put<int32_t>(m + 0x34, 32);
        Put<uint64_t>(Entries, 123); Put(Entries + 8, Remote);
        Put<uint64_t>(Entries + 33 * 24, 456); Put(Entries + 33 * 24 + 8, Remote2);
        // Free slots contain poison pointers: the reader must not follow them.
        Put<uintptr_t>(Entries + 24 + 8, 0xDEADBEEF);
        Peer(Remote, RemoteRoot, 123, 0, u"76561198000000001");
        Peer(Remote2, RemoteRoot2, 456, 1, u"76561198000000002");
        Put(RemoteRoot + 0x260, LoungePosition{6, 8, 0});
        Put(RemoteRoot2 + 0x260, LoungePosition{3, 4, 0});
    }
    static bool Read(void* context, uintptr_t address, void* output, size_t size) {
        auto& f = *static_cast<Fixture*>(context);
        if (address == f.blockedAddress || address < Base || address - Base > f.memory.size() ||
            size > f.memory.size() - (address - Base)) return false;
        memcpy(output, f.memory.data() + address - Base, size);
        if (address == f.changeAddress && ++f.reads == 2 && size)
            static_cast<unsigned char*>(output)[0] ^= 1;
        return true;
    }
    LoungeSnapshot Snapshot() { return LoungeReader(Base, layout, Read, this).Read(); }
};
void Failed(Fixture& f, LoungeReadStatus expected) {
    const auto s = f.Snapshot();
    assert(s.status == expected && s.participants.empty() && s.roomKey.empty() && !s.localActor);
}
}
int main() {
    {
        Fixture f;
        const auto s = f.Snapshot();
        assert(s.status == LoungeReadStatus::Ok && s.roomKey == "SYNTHETIC_ROOM_A");
        assert(s.participants.size() == 2 && s.participants[0].cosmosId == 456);
        assert(s.participants[0].distance == 5 && s.participants[1].distance == 10);
        assert(s.participants[0].steamIdCandidate == 0); // A console field must not become a Steam peer.
        assert(s.participants[1].steamIdCandidate == 76561198000000001ULL);
        assert(s.participants[0].name.find("\xF0\x9F\x98\x80") != std::string::npos);
        // World translation, not relative location, determines distance.
        f.Put(Fixture::RemoteRoot + 0x128, LoungePosition{999, 999, 999});
        assert(f.Snapshot().participants[1].distance == 10);
        LoungeReader reader(Fixture::Base, f.layout, Fixture::Read, &f);
        assert(reader.StillCurrent(s));
        f.Put<uint8_t>(Fixture::Mode + 0x3EE, 0);
        assert(!reader.StillCurrent(s));
        f.Put<uint8_t>(Fixture::Mode + 0x3EE, 1);
        f.Put<uintptr_t>(Fixture::World + 0x150, Fixture::Mode + 8);
        assert(!reader.StillCurrent(s));
        f.Put(Fixture::World + 0x150, Fixture::Mode);
        f.Put<uintptr_t>(Fixture::Base + f.layout.worldRva, 0);
        assert(!reader.StillCurrent(s));
    }
    {
        Fixture f;
        f.Class(Fixture::Mode, "MainTmp_SimpleKamuiGameMode_C");
        f.blockedAddress = Fixture::Base + f.layout.subsystemRva;
        Failed(f, LoungeReadStatus::NotInLounge); // Stale room must never be consulted.
    }
    {
        Fixture f; f.layout.peTimestamp ^= 1; Failed(f, LoungeReadStatus::UnsupportedBuild);
    }
    {
        Fixture f; f.Put<uintptr_t>(Fixture::Base + f.layout.worldRva, 0);
        Failed(f, LoungeReadStatus::NotReady);
    }
    {
        Fixture f; f.Put<int32_t>(Fixture::Manager + 0x28 + 0x2C, 256);
        Failed(f, LoungeReadStatus::UnsupportedLayout);
    }
    {
        Fixture f; f.Put<int32_t>(Fixture::Manager + 0x28 + 0x34, 31);
        Failed(f, LoungeReadStatus::InvalidData);
    }
    {
        Fixture f; f.Put<uint64_t>(Fixture::Remote + 0x390, 999);
        Failed(f, LoungeReadStatus::InvalidData);
    }
    {
        Fixture f; f.Put(Fixture::RemoteRoot + 0x260,
            LoungePosition{std::numeric_limits<double>::quiet_NaN(), 0, 0});
        Failed(f, LoungeReadStatus::InvalidData);
    }
    {
        Fixture f; f.blockedAddress = Fixture::Remote + 0x390;
        Failed(f, LoungeReadStatus::Unreadable);
    }
    for (const auto address : {Fixture::Base + 0x1000, Fixture::Manager + 0x28,
        Fixture::Entries, Fixture::Remote + 0x390}) {
        Fixture f; f.changeAddress = address; Failed(f, LoungeReadStatus::ChangedDuringRead);
    }
    {
        Fixture f; f.changeAddress = f.String(Fixture::Network + 0x140, u"SYNTHETIC_ROOM_B");
        Failed(f, LoungeReadStatus::ChangedDuringRead); // Same header, changed contents.
    }
    {
        Fixture f; f.Put<int32_t>(Fixture::Network + 0x148, 9999);
        Failed(f, LoungeReadStatus::InvalidData);
    }
    {
        Fixture f;
        const auto map = Fixture::Manager + 0x28;
        f.Put<int32_t>(map + 8, 0); f.Put<int32_t>(map + 0x28, 0); f.Put<int32_t>(map + 0x34, 0);
        const auto s = f.Snapshot(); assert(s.status == LoungeReadStatus::Ok && s.participants.empty());
    }
    {
        const auto path = std::filesystem::temp_directory_path() /
            ("t8_lounge_layout_" + std::to_string(GetCurrentProcessId()) + ".ini");
        { std::ofstream out(path); out << "[VoiceLounge]\nworld_rva=0x1000\nname_blocks_rva=2000\n"
            "subsystem_rva=1100\npe_timestamp=12345678\nimage_size=100000\n"; }
        LoungeLayout layout; std::string error;
        assert(LoadLoungeLayout(path, layout, error) && layout.worldRva == 0x1000);
        WritePrivateProfileStringW(L"VoiceLounge", L"subsystem_rva", L"bad-input", path.c_str());
        assert(!LoadLoungeLayout(path, layout, error) && layout.worldRva == 0);
        std::filesystem::remove(path);
    }
    std::cout << "Lounge reader tests passed\n";
}
