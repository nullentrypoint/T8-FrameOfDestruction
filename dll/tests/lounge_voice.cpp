#include "VoiceChat/LoungeVoice.h"
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>

using namespace VoiceChat;
struct Message { uint64_t from, to; LoungeProtocol::Bytes bytes; };
int main() {
    VoiceSequenceWindow window;
    assert(window.Accept(0xfffffffe) && window.Accept(0) && window.Accept(0xffffffff));
    assert(!window.Accept(0xffffffff) && !window.Accept(0xffffff00));
    VoiceBlock loud; loud.fill(30000);
    VoiceMix saturation; saturation.Add(loud, 1); saturation.Add(loud, 1);
    assert(saturation.Finish()[0] == 32767);
    loud.fill(-30000); VoiceMix negative; negative.Add(loud, 1); negative.Add(loud, 1);
    assert(negative.Finish()[0] == -32768);
    assert(ProximityGain(0, 1500) == 1 && ProximityGain(1500, 1500) == 0);
    assert(ProximityGain(900, 1500) > ProximityGain(1200, 1500));
    VoiceJitterBuffer jitter;
    VoiceBlock first; first.fill(1000);
    VoiceBlock second; second.fill(2000);
    jitter.Push(2, second, 100); jitter.Push(1, first, 110);
    assert(jitter.Pop(139)[0] == 0 && jitter.Pop(140)[0] == 1000 && jitter.Pop(160)[0] == 2000);
    assert(jitter.Pop(400)[0] == 0);
    assert(DefaultLoungeVoiceDistance == -1);

    std::deque<Message> queue;
    std::array<std::unique_ptr<LoungeVoice>, 3> clients;
    std::array<LoungeSnapshot, 3> rosters;
    std::array<std::array<LoungeVoice::Controls, 4>, 3> prefs{};
    uint64_t now = 1000;
    unsigned nonce = 0, closes = 0;
    for (size_t n = 0; n < clients.size(); ++n) {
        clients[n] = std::make_unique<LoungeVoice>(
            [&, n](uint64_t to, const auto& bytes, bool) { queue.push_back({n + 1, to, bytes}); return true; },
            [&](uint64_t) { ++closes; },
            [&](auto& value) { value.fill(0); value[0] = static_cast<uint8_t>(++nonce); return true; },
            [&, n](uint64_t id) { return prefs[n][id]; });
        auto& roster = rosters[n];
        roster.status = LoungeReadStatus::Ok; roster.world = 123; roster.roomKey = "ROOM_A";
        for (uint64_t id = 1; id <= 3; ++id) if (id != n + 1) {
            LoungeParticipant p; p.steamIdCandidate = id; p.platform = 0; p.distance = 100;
            roster.participants.push_back(p);
        }
        clients[n]->Sync(n + 1, roster, now, 1500, true);
    }
    const auto deliver = [&] {
        unsigned budget = 1000;
        while (!queue.empty()) {
            assert(budget--);
            auto m = std::move(queue.front()); queue.pop_front();
            clients[m.to - 1]->Receive(m.from, m.bytes, now);
        }
    };
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    for (auto& c : clients) assert(c->ConnectedCount() == 2);
    first.fill(1000); second.fill(2000);
    clients[1]->Capture(first.data()); clients[2]->Capture(second.data());
    const auto replay = queue.front();
    deliver();
    auto& target = *clients[0];
    assert(target.Peers().at(2).received == 1);
    target.Receive(2, replay.bytes, now); // Duplicate.
    target.Receive(3, replay.bytes, now); // Wrong authenticated Steam sender.
    auto wrongRoom = replay.bytes; wrongRoom[8] ^= 1;
    target.Receive(2, wrongRoom, now);
    for (size_t size = 0; size < replay.bytes.size(); ++size)
        target.Receive(2, std::span<const uint8_t>(replay.bytes.data(), size), now);
    assert(target.Peers().at(2).received == 1 && target.Peers().at(3).received == 1);
    assert(target.Mix(now)->at(0) == 0);
    now += 40;
    assert(target.Mix(now)->at(0) == 3000); // Two simultaneous speakers.
    prefs[0][2].muted = true;
    target.Sync(1, rosters[0], now, 1500, true);
    assert(target.TakePlaybackReset());
    clients[1]->Capture(first.data()); clients[2]->Capture(second.data()); deliver();
    now += 20;
    assert(target.Mix(now)->at(0) == 2000);
    prefs[0][2].muted = false; prefs[0][2].volume = 50;
    target.Sync(1, rosters[0], now, 1500, true);
    clients[1]->Capture(first.data()); deliver();
    now += 40;
    assert(target.Mix(now)->at(0) == 500);
    rosters[0].participants[0].distance = 1600;
    target.Sync(1, rosters[0], now, 1500, true);
    assert(target.Peers().contains(2) && target.Peers().at(2).gain == 0);
    rosters[0].participants[0].distance = 1800;
    target.Sync(1, rosters[0], now, 1500, true);
    assert(!target.Peers().contains(2));
    rosters[0].participants[0].distance = 100;
    target.Sync(1, rosters[0], now, 1500, true);
    target.Tick(now, false); deliver();
    now += 2000;
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    assert(target.ConnectedCount() == 2);
    const auto nonceBeforeTimeout = nonce;
    now += 7000;
    target.Tick(now, false);
    assert(target.ConnectedCount() == 0 && nonce > nonceBeforeTimeout);
    deliver();
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    assert(target.ConnectedCount() == 2);
    const auto before = target.Peers().at(2).received;
    target.Receive(2, replay.bytes, now);
    assert(target.Peers().at(2).received == before); // Previous nonce epoch rejected.
    rosters[0].roomKey = "ROOM_B";
    target.Sync(1, rosters[0], now, 1500, true); target.Tick(now, false); deliver();
    assert(target.ConnectedCount() == 0);
    rosters[0].world++;
    target.Sync(1, rosters[0], now, 1500, true);
    target.Sync(1, rosters[0], now, 1500, false);
    assert(target.Peers().empty() && closes > 0);
    LoungeProtocol::Handshake h;
    LoungeProtocol::Nonce random{}; random[0] = 1;
    assert(h.Begin(1, 2, "ROOM_A", random));
    assert(!h.Begin(1, 2, "", random) && !h.Hello());
    for (size_t n = 0; n < clients.size(); ++n) {
        clients[n]->Reset(); prefs[n] = {};
        rosters[n].roomKey = "ROOM_UNLIMITED"; rosters[n].world = 777;
        for (auto& member : rosters[n].participants) member.distance = 1000000;
        clients[n]->Sync(n + 1, rosters[n], now, DefaultLoungeVoiceDistance, true);
    }
    queue.clear();
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    for (auto& c : clients) assert(c->ConnectedCount() == 2);
    assert(target.Peers().at(2).gain == 1 && target.Peers().at(3).gain == 1);
    clients[1]->Capture(first.data()); clients[2]->Capture(second.data()); deliver();
    assert(target.Mix(now)->at(0) == 0);
    now += 40;
    assert(target.Mix(now)->at(0) == 3000);
    target.Sync(1, rosters[0], now, 1500, true);
    assert(target.Peers().empty()); // Explicit finite range still filters distant peers.
    queue.clear();
    for (size_t n = 0; n < clients.size(); ++n) {
        clients[n]->Reset();
        prefs[n] = {};
        auto& roster = rosters[n];
        roster.customRoom = true; roster.roomKey = "custom:PLMabcdef"; roster.world = 999;
        roster.participants.clear();
        for (uint64_t id = 1; id <= 3; ++id) {
            LoungeParticipant p; p.steamIdCandidate = id; p.platform = 0; p.distance = 100000;
            roster.participants.push_back(p);
        }
        clients[n]->Sync(n + 1, roster, now, 0, true); // Lounge hearing distance does not disable custom rooms.
    }
    queue.clear();
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    for (auto& c : clients) assert(c->ConnectedCount() == 2);
    clients[1]->Capture(first.data()); clients[2]->Capture(second.data()); deliver();
    assert(target.Mix(now)->at(0) == 0);
    now += 40;
    assert(target.Mix(now)->at(0) == 3000); // No proximity attenuation.
    // Coarse Windows polling: 31 ms worker ticks must still render 50 blocks/s.
    // A muted spectator sends keepalives throughout without resetting other voices.
    for (size_t n = 0; n < clients.size(); ++n) {
        rosters[n].world = 1001;
        clients[n]->Sync(n + 1, rosters[n], now, 0, true);
    }
    queue.clear();
    for (auto& c : clients) c->Tick(now, false);
    deliver();
    const auto start = now;
    uint64_t nextCapture = start;
    int rendered = 0, audible = 0, resets = 0;
    for (uint64_t elapsed = 0; elapsed <= 10000; elapsed += 31) {
        now = start + elapsed;
        clients[1]->Tick(now, false);
        clients[2]->Tick(now, true);
        target.Tick(now, false);
        while (nextCapture <= now) {
            clients[1]->Capture(first.data());
            nextCapture += 20;
        }
        deliver();
        if (target.TakePlaybackReset()) ++resets;
        for (int budget = 0; budget < 6; ++budget) {
            const auto pcm = target.Mix(now);
            if (!pcm) break;
            ++rendered;
            if (pcm->at(0) == 1000) ++audible;
        }
    }
    assert(rendered >= 499 && rendered <= 501);
    assert(audible >= rendered - 6);
    assert(resets == 1); // Initial epoch/mute only, never the periodic mute keepalive.
    rosters[0].participants.erase(rosters[0].participants.begin()); // Local identity absent.
    target.Sync(1, rosters[0], now, 0, true);
    assert(target.Peers().empty());
    std::cout << "Lounge protocol, three-client transport, replay, mixer and controls passed\n";
}
