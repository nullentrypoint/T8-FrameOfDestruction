#include "VoiceChat/SteamProbeProtocol.h"
#include <cassert>
#include <string>

namespace {
using namespace VoiceChat::ProbeProtocol;
Pair PairA() {
    // Fixed synthetic identities, unrelated to captured players.
    Pair p{76561202131974853ULL, 76561202084629517ULL};
    memcpy(p.localTekken.data(), "7rQ2mK9vXp4A", 12);
    memcpy(p.remoteTekken.data(), "6tN8bR3wYk5C", 12);
    return p;
}
Pair Reverse(Pair p) {
    std::swap(p.localSteam, p.remoteSteam);
    std::swap(p.localTekken, p.remoteTekken);
    return p;
}
Nonce Token(uint8_t value) { Nonce n{}; n[0] = value; return n; }
void Exchange(Handshake& a, Handshake& b, const Pair& pair) {
    const auto helloA = a.Hello();
    const auto helloB = b.Hello();
    assert(helloA && helloB);
    const auto replyB = b.Receive(pair.localSteam, *helloA);
    const auto replyA = a.Receive(pair.remoteSteam, *helloB);
    assert(replyA.reply && replyB.reply);
    assert(a.Receive(pair.remoteSteam, *replyB.reply).acknowledged);
    assert(b.Receive(pair.localSteam, *replyA.reply).acknowledged);
    assert(a.Ready() && b.Ready());
}
}

void TestSteamProbeProtocol() {
    const auto pair = PairA();
    Handshake a, b;
    a.Begin(pair, Token(1));
    b.Begin(Reverse(pair), Token(2));
    const auto helloA = a.Hello();
    assert(helloA && !a.Ready() && !b.Ready());
    const auto ackB = b.Receive(pair.localSteam, *helloA);
    assert(ackB.reply && !ackB.acknowledged && !b.Ready());
    assert(!a.Receive(pair.localSteam, *ackB.reply).acknowledged); // Wrong authenticated sender.
    auto bad = *ackB.reply;
    bad[4] = 2;
    assert(!a.Receive(pair.remoteSteam, bad).acknowledged);
    bad = *ackB.reply;
    bad[68] ^= 1; // Wrong recipient Tekken ID.
    assert(!a.Receive(pair.remoteSteam, bad).acknowledged);
    bad = *ackB.reply;
    bad[16] ^= 1; // Wrong recipient SteamID.
    assert(!a.Receive(pair.remoteSteam, bad).acknowledged);
    bad = *ackB.reply;
    bad[40] ^= 1; // Wrong echoed challenge.
    assert(!a.Receive(pair.remoteSteam, bad).acknowledged);
    assert(!a.Ready());
    assert(!Decode(std::span<const uint8_t>(bad.data(), bad.size() - 1)));
    std::array<uint8_t, PacketSize + 1> tooLong{};
    memcpy(tooLong.data(), helloA->data(), PacketSize);
    assert(!Decode(tooLong));
    bad = *helloA;
    bad[7] = 1;
    assert(!Decode(bad));
    bad = *helloA;
    bad[6] = 99;
    assert(!Decode(bad));
    bad = *helloA;
    std::fill(bad.begin() + 24, bad.begin() + 40, 0);
    assert(!Decode(bad));

    Exchange(a, b, pair);
    const auto audioA = a.AudioHeader();
    const auto stateA = a.MicStateHeader();
    assert(stateA && b.AcceptData(pair.localSteam, *stateA, Type::MicState));
    assert(!b.AcceptAudio(pair.localSteam, *stateA));
    assert(!b.Receive(pair.localSteam, *stateA).acknowledged);
    assert(!b.AcceptData(pair.remoteSteam, *stateA, Type::MicState));
    assert(audioA && b.AcceptAudio(pair.localSteam, *audioA));
    assert(!b.AcceptAudio(pair.remoteSteam, *audioA));
    assert(!b.Receive(pair.localSteam, *audioA).acknowledged);
    auto wrongAudio = *audioA;
    wrongAudio[40] ^= 1;
    assert(!b.AcceptAudio(pair.localSteam, wrongAudio));
    assert(!b.AcceptAudio(pair.localSteam, *a.Hello()));
    a.MarkTimedOut();
    assert(!a.AudioHeader());
    assert(!a.Ready());
    assert(a.Receive(pair.remoteSteam, *ackB.reply).acknowledged && a.Ready());

    // A restarted peer proves possession of the current local challenge.
    Handshake restartedB;
    restartedB.Begin(Reverse(pair), Token(3));
    Exchange(a, restartedB, pair);
    assert(!restartedB.AcceptData(pair.localSteam, *stateA, Type::MicState));
    assert(!restartedB.AcceptAudio(pair.localSteam, *audioA));
    assert(!a.Receive(pair.remoteSteam, *ackB.reply).acknowledged); // Retired peer nonce.
    const auto bye = restartedB.End();
    assert(bye && a.Receive(pair.remoteSteam, *bye).peerClosed && !a.Ready());
    assert(!a.Receive(pair.remoteSteam, *bye).peerClosed);
    assert(!restartedB.Hello());

    // New accepted-opponent interval rejects delayed ACK from the previous one.
    a.Begin(pair, Token(4));
    assert(!a.Receive(pair.remoteSteam, *ackB.reply).acknowledged && !a.Ready());
    Handshake freshB;
    freshB.Begin(Reverse(pair), Token(5));
    Exchange(a, freshB, pair);
    const auto packet = Decode(*a.Hello());
    assert(packet && packet->sender == pair.localSteam && packet->recipient == pair.remoteSteam);
    assert(packet->senderTekken == pair.localTekken && packet->recipientTekken == pair.remoteTekken);
    assert(a.End() && !a.Ready() && !a.Hello());
    assert(!a.Receive(pair.remoteSteam, *freshB.Hello()).reply);

    Handshake disabled;
    disabled.Begin(pair, {});
    assert(!disabled.Hello());
    auto self = pair;
    self.remoteSteam = self.localSteam;
    disabled.Begin(self, Token(1));
    assert(!disabled.Hello());
}
