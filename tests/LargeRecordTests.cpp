#include "TestHarness.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "ECS/World.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/ReliabilityChannel.hpp"
#include "Network/SimulatedNetwork.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// 큰 레코드 전송 (docs/design/large-record-transport.md)
//
// N4.5 — 앱 계층의 64KB 상한을 전달 등급별 상한으로 바꾸고, 받는 쪽 재조립에 상한을 둔다.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    using EF::Network::ReliabilityChannel;
    using Delivery = EF::Network::ReliabilityChannel::Delivery;

    // 신뢰 레코드 하나를 데이터그램으로 만들어 돌려준다(조각 하나짜리가 되도록 작게).
    std::vector<uint8_t> MakeSingleFragmentDatagram(ReliabilityChannel& sender, uint8_t marker) {
        const std::vector<uint8_t> record{ marker };
        EXPECT_TRUE(sender.Enqueue(Delivery::Reliable, record));
        std::vector<std::vector<uint8_t>> datagrams = sender.Flush(1);
        EXPECT_EQ(datagrams.size(), std::size_t{ 1 });
        return datagrams.empty() ? std::vector<uint8_t>{} : datagrams.front();
    }

    // 데이터그램 배치: [version u8][ack u32][channel u8][sequence u32][index u16][count u16][length u16][bytes]
    // 테스트가 와이어 값을 조작하려고 쓰는 오프셋이다(ReliabilityChannel::Flush의 배치와 같다).
    constexpr std::size_t kSequenceOffset = 1 + 4 + 1;
    constexpr std::size_t kFragmentCountOffset = kSequenceOffset + 4 + 2;

    void WriteU16BigEndian(std::vector<uint8_t>& bytes, std::size_t offset, uint16_t value) {
        bytes[offset] = static_cast<uint8_t>(value >> 8);
        bytes[offset + 1] = static_cast<uint8_t>(value & 0xFF);
    }

    void WriteU32BigEndian(std::vector<uint8_t>& bytes, std::size_t offset, uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            bytes[offset + static_cast<std::size_t>(i)] = static_cast<uint8_t>(value >> (24 - 8 * i));
        }
    }

    EF::Fixed64 FixedTickDelta() {
        return EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    }
}

// ── 송신 상한 ────────────────────────────────────────────────────────────────

// 상한 바로 아래의 신뢰 레코드는 받아들이고, 상한을 넘으면 거부한다.
TEST_CASE(LargeRecord, ReliableRecordCapIsEnforcedAtEnqueue) {
    ReliabilityChannel channel;
    const std::vector<uint8_t> atCap(EF::Network::kMaxReliableRecordBytes, 0x5A);
    const std::vector<uint8_t> overCap(EF::Network::kMaxReliableRecordBytes + 1, 0x5A);

    EXPECT_TRUE(channel.Enqueue(Delivery::Reliable, atCap));
    EXPECT_FALSE(channel.Enqueue(Delivery::Reliable, overCap));
}

// 비신뢰 레코드는 데이터그램 하나에 들어가야 한다(넘으면 IP 단편화에 맡기게 되고, 단편 하나만
// 잃어도 통째로 사라진다). 상한 안의 것은 실제로 데이터그램 크기 안에 포장되어야 한다.
TEST_CASE(LargeRecord, UnreliableRecordMustFitOneDatagram) {
    for (Delivery delivery : { Delivery::Sequenced, Delivery::Unreliable }) {
        ReliabilityChannel channel;
        const std::vector<uint8_t> fits(EF::Network::kMaxUnfragmentedRecordBytes, 0x11);
        const std::vector<uint8_t> tooBig(EF::Network::kMaxUnfragmentedRecordBytes + 1, 0x11);

        EXPECT_FALSE(channel.Enqueue(delivery, tooBig));
        EXPECT_TRUE(channel.Enqueue(delivery, fits));

        const std::vector<std::vector<uint8_t>> datagrams = channel.Flush(1);
        EXPECT_EQ(datagrams.size(), std::size_t{ 1 });
        for (const auto& datagram : datagrams) {
            EXPECT_TRUE(datagram.size() <= EF::Network::kMaxDatagramPayload);
        }
    }
}

// ── 큰 스냅샷이 실제로 간다 ──────────────────────────────────────────────────

// 1 MiB가 넘는 월드 스냅샷으로 조인한다. 예전에는 SendPacket이 64KB에서 조용히 거부해서
// 클라가 영영 스냅샷을 받지 못했다. 손실 링크(10%)에서도 재조립되어 해시가 같아야 한다.
//
// ⚠️ 이 테스트의 링크(SimulatedNetwork)에는 수신 버퍼 한계가 없다. 실제 UDP에서 큰 레코드가
//    버스트로 나가 수신 버퍼 꼬리가 버려지는 문제는 여기서 재현되지 않는다 — 그건 N4.6
//    (페이싱)이 따로 재현하고 고친다(large-record-transport.md §2).
TEST_CASE(LargeRecord, MegabyteSnapshotJoinsOverLossyLink) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20261101);
    net.SetLoss(0.10f);
    const Endpoint serverEp = Endpoint::Memory(900);
    const Endpoint clientEp = Endpoint::Memory(1);
    auto serverHost = net.CreateHost(serverEp);
    auto clientHost = net.CreateHost(clientEp, serverEp);

    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();
    serverWorld.RegisterSnapshotComponent<EF::Velocity>();
    serverWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    constexpr int kEntityCount = 10000;  // 엔티티당 Transform + Velocity ≈ 110바이트 → 1 MiB 남짓
    for (int i = 0; i < kEntityCount; ++i) {
        const EF::EntityId entity = serverWorld.CreateEntity();
        serverWorld.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(i, i + 1, i + 2),
            .rotation = EF::Quaternion::Identity()
        });
        serverWorld.AddComponent<EF::Velocity>(entity, EF::Velocity{
            .linear = EF::Vector3(i, 0, 0),
            .angular = EF::Vector3(0, i, 0)
        });
    }
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    // 전제: 정말로 1 MiB를 넘는다(넘지 않으면 이 테스트는 아무것도 증명하지 못한다).
    EXPECT_TRUE(EF::NetworkManager::EncodeWorldSnapshot(serverWorld.CreateSnapshot(0)).size() > std::size_t{ 1 } << 20);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientHost);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 200 && !clientSync.HasSnapshot(); ++tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_EQ(clientSync.AppliedSnapshotCount(), uint64_t{ 1 });
}

// ── 받는 쪽 상한 ─────────────────────────────────────────────────────────────

// 와이어에 실린 조각 수가 상한을 넘으면 그 조각을 버린다. 받아들이면 재조립 버퍼를 그 개수로
// 잡고, 같은 시퀀스의 정상 조각은 "조각 수 불일치"로 버려져 레코드가 영영 완성되지 않는다.
TEST_CASE(LargeRecord, ReceiverIgnoresAbsurdFragmentCount) {
    ReliabilityChannel sender;
    std::vector<uint8_t> legit = MakeSingleFragmentDatagram(sender, 0x42);

    std::vector<uint8_t> forged = legit;
    WriteU16BigEndian(forged, kFragmentCountOffset, 0xFFFF);  // 조각 65535개라고 주장

    ReliabilityChannel receiver;
    std::vector<std::vector<uint8_t>> delivered;
    receiver.OnReceive(forged, delivered);
    receiver.OnReceive(legit, delivered);

    EXPECT_EQ(delivered.size(), std::size_t{ 1 });
    if (!delivered.empty()) {
        EXPECT_EQ(delivered.front().size(), std::size_t{ 1 });
        EXPECT_EQ(delivered.front().front(), uint8_t{ 0x42 });
    }
}

// 다음 기대 시퀀스보다 너무 멀리 앞선 시퀀스는 재조립하지 않는다. 정상 송신자는 ack 없이
// 그렇게 앞서 가지 않으므로, 그런 조각을 받아 두면 메모리만 잡힌다. 버려도 송신자가 재전송한다.
//
// 관찰 방법: 창 밖 시퀀스에 "조각 5개짜리" 가짜 조각을 먼저 넣는다. 받아들였다면 나중에 오는
// 정상(조각 1개) 레코드가 조각 수 불일치로 버려져 그 시퀀스에서 전달이 멈춘다.
TEST_CASE(LargeRecord, ReceiverIgnoresSequencesFarAheadOfWindow) {
    constexpr uint32_t kFarSequence = EF::Network::kMaxReassemblyWindow + 10;

    ReliabilityChannel sender;
    std::vector<uint8_t> forged = MakeSingleFragmentDatagram(sender, 0x00);  // 시퀀스 0을 소모한다
    WriteU32BigEndian(forged, kSequenceOffset, kFarSequence);
    WriteU16BigEndian(forged, kFragmentCountOffset, 5);

    ReliabilityChannel receiver;
    std::vector<std::vector<uint8_t>> delivered;
    receiver.OnReceive(forged, delivered);

    // 정상 흐름: 시퀀스 0(이미 만든 것)부터 kFarSequence까지 순서대로 보낸다.
    // 수신 측이 앞 레코드를 소화해야 창이 앞으로 가므로 한 번에 하나씩 보내고 받는다.
    ReliabilityChannel replay;
    for (uint32_t i = 0; i <= kFarSequence; ++i) {
        const std::vector<uint8_t> record{ static_cast<uint8_t>(i & 0xFF) };
        EXPECT_TRUE(replay.Enqueue(Delivery::Reliable, record));
        for (const auto& datagram : replay.Flush(1 + i)) {
            receiver.OnReceive(datagram, delivered);
        }
    }

    EXPECT_EQ(delivered.size(), static_cast<std::size_t>(kFarSequence) + 1);
}
