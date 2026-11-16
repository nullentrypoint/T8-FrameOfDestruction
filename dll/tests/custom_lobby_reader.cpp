#include "VoiceChat/CustomLobbyReader.h"
#include <cassert>
#include <iostream>

using namespace VoiceChat;
struct Fixture {
    static constexpr uintptr_t Base = 0x100000, Root = Base + 0x2000, Owner = Base + 0x3000,
        Holder = Base + 0x5000, Session = Base + 0x6000, Id = Base + 0x7000,
        Head = Base + 0x8000, Node1 = Base + 0x8100, Node2 = Base + 0x8200,
        Record1 = Base + 0x9000, Record2 = Base + 0xA000;
    CustomLobbyLayout layout{0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x12345678, 0x20000};
    std::vector<uint8_t> bytes = std::vector<uint8_t>(0x20000);
    uintptr_t nextString = Base + 0xC000, mutation = 0;
    unsigned seen = 0;
    template<class T> void Put(uintptr_t at, T value) { memcpy(bytes.data() + at - Base, &value, sizeof(value)); }
    void String(uintptr_t at, const std::string& text) {
        Put<uint64_t>(at + 16, text.size());
        Put<uint64_t>(at + 24, text.size() < 16 ? 15 : 127);
        auto data = at;
        if (text.size() >= 16) { data = nextString; nextString += 128; Put(at, data); }
        memcpy(bytes.data() + data - Base, text.c_str(), text.size() + 1);
    }
    void Member(uintptr_t node, uintptr_t record, const char* user, const char* name, const char* steam) {
        String(node + 0x10, user); Put(node + 0x30, record);
        Put<uintptr_t>(record, Base + layout.memberVtable);
        String(record + 8, user); String(record + 0x28, name);
        String(record + 0x70, steam); String(record + 0x90, steam);
    }
    Fixture() {
        Put<uint16_t>(Base, 0x5A4D); Put<uint32_t>(Base + 0x3C, 0x80);
        Put<uint32_t>(Base + 0x80, 0x4550); Put<uint16_t>(Base + 0x84, 0x8664);
        Put(Base + 0x88, layout.peTimestamp); Put(Base + 0xD0, layout.imageSize);
        Put(Base + layout.rootRva, Root); Put(Root + 0x640, Owner);
        Put<uintptr_t>(Owner, Base + layout.ownerVtable); Put(Owner + 0x730, Holder);
        Put(Holder, Session); Put<uintptr_t>(Session, Base + layout.sessionVtable);
        Put(Session + 8, Id); Put<uintptr_t>(Id, Base + layout.idVtable);
        String(Id + 8, "PLMabcdef1234567890");
        Put(Session + 0x180, Head); Put<uint64_t>(Session + 0x188, 2);
        Put(Head, Node1); Put(Head + 8, Node2);
        Put(Node1, Node2); Put(Node1 + 8, Head);
        Put(Node2, Head); Put(Node2 + 8, Node1);
        Member(Node1, Record1, "user1", "Self", "76561198000000001");
        Member(Node2, Record2, "user2", "Remote", "76561198000000002");
    }
    static bool Read(void* ctx, uintptr_t at, void* out, size_t size) {
        auto& f = *static_cast<Fixture*>(ctx);
        if (at < Base || at - Base > f.bytes.size() || size > f.bytes.size() - (at - Base)) return false;
        memcpy(out, f.bytes.data() + at - Base, size);
        if (at == f.mutation && ++f.seen == 2) static_cast<uint8_t*>(out)[0] ^= 1;
        return true;
    }
    CustomLobbyReader Reader() { return {Base, layout, Read, this}; }
};
int main() {
    {
        Fixture f; auto reader = f.Reader(); const auto s = reader.Read();
        assert(s.status == LoungeReadStatus::Ok && s.customRoom && s.participants.size() == 2);
        assert(s.roomKey == "custom:PLMabcdef1234567890" && reader.StillCurrent(s));
        assert(s.participants[1].name == "Remote" && s.participants[1].steamIdCandidate == 76561198000000002ULL);
        f.Put(Fixture::Node1, Fixture::Head); f.Put(Fixture::Head + 8, Fixture::Node1);
        f.Put<uint64_t>(Fixture::Session + 0x188, 1);
        assert(reader.Read().participants.size() == 1);
        f.Put<uintptr_t>(Fixture::Owner + 0x730, 0);
        assert(!reader.StillCurrent(s) && reader.Read().status == LoungeReadStatus::NotInLounge);
    }
    {
        Fixture f; f.String(Fixture::Record2 + 0x90, "console-account");
        const auto s = f.Reader().Read();
        assert(s.status == LoungeReadStatus::Ok && !s.participants[1].steamIdCandidate);
    }
    {
        Fixture f; f.String(Fixture::Id + 8, "OTHER");
        assert(f.Reader().Read().status == LoungeReadStatus::NotInLounge);
    }
    for (auto address : {Fixture::Owner + 0x730, Fixture::Session + 0x188, Fixture::Node1,
        Fixture::Record2 + 0x28}) {
        Fixture f; f.mutation = address;
        const auto s = f.Reader().Read();
        assert(s.status == LoungeReadStatus::ChangedDuringRead && s.participants.empty());
    }
    for (int fault = 0; fault < 5; ++fault) {
        Fixture f;
        if (fault == 0) f.Put<uint64_t>(Fixture::Session + 0x188, 65);
        if (fault == 1) f.Put(Fixture::Node2, Fixture::Node1);
        if (fault == 2) f.String(Fixture::Record2 + 8, "wrong-key");
        if (fault == 3) f.Put<uintptr_t>(Fixture::Owner, 0);
        if (fault == 4) f.layout.peTimestamp ^= 1;
        const auto s = f.Reader().Read();
        assert(s.status != LoungeReadStatus::Ok && s.participants.empty());
    }
    std::cout << "Custom lobby reader: roster, departure, stale data, guards and mutation checks passed\n";
}
