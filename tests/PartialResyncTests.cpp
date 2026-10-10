#include "TestHarness.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "ECS/World.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// N4.3 — 부분 resync의 데이터 계층.
//
//  - SelectPoolsForPartialResync: 서버가 "어느 풀을 보낼지 / 전체로 갈지" 판정하는 순수 함수
//  - World::CreatePartialSnapshot / ApplyPartialSnapshot: 풀만 뜨고, 풀만 통째로 갈아끼우기
//  - ResyncRequest / PartialSnapshot 와이어 포맷
//
// 동기화 시스템에 연결하는 것(서버의 부분 응답, 클라의 tick 정렬 적용, 폴백)은 N4.4다.
// 설계: docs/design/partial-resync.md
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    EF::Transform MakeTransform(int x) {
        return EF::Transform{
            .position = EF::Vector3(x, 0, 0),
            .rotation = EF::Quaternion::Identity()
        };
    }

    EF::Velocity MakeVelocity(int x) {
        return EF::Velocity{
            .linear = EF::Vector3(x, 0, 0),
            .angular = EF::Vector3(0, 0, 0)
        };
    }

    // 엔티티 둘에 Transform·Velocity를 붙인 월드. transformBase만 바꿔 "Transform 풀만 다른"
    // 두 월드를 만든다. 엔티티 할당 이력은 항상 같다.
    EF::World MakeWorld(int transformBase, int velocityBase = 100) {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::Velocity>();
        for (int i = 0; i < 2; ++i) {
            const EF::EntityId entity = world.CreateEntity();
            world.AddComponent<EF::Transform>(entity, MakeTransform(transformBase + i));
            world.AddComponent<EF::Velocity>(entity, MakeVelocity(velocityBase + i));
        }
        return world;
    }

    EF::TypeHash TransformHash() { return EF::GetTypeInfo<EF::Transform>().hash; }
    EF::TypeHash VelocityHash() { return EF::GetTypeInfo<EF::Velocity>().hash; }
}

// ── 풀 선택 ─────────────────────────────────────────────────────────────────

TEST_CASE(PartialResync, SelectsOnlyTheDifferingPool) {
    const EF::WorldHashBreakdown server = MakeWorld(1).ComputeHashBreakdown();
    const EF::WorldHashBreakdown client = MakeWorld(9).ComputeHashBreakdown();

    const std::optional<std::vector<EF::TypeHash>> pools = EF::SelectPoolsForPartialResync(client, &server);
    EXPECT_TRUE(pools.has_value());
    if (pools.has_value()) {
        EXPECT_EQ(pools->size(), std::size_t{ 1 });
        EXPECT_EQ(pools->front(), TransformHash());
    }
}

TEST_CASE(PartialResync, FallsBackToFullWithoutServerRecord) {
    const EF::WorldHashBreakdown client = MakeWorld(9).ComputeHashBreakdown();
    EXPECT_FALSE(EF::SelectPoolsForPartialResync(client, nullptr).has_value());
}

TEST_CASE(PartialResync, FallsBackToFullWhenEntityAllocationDiffers) {
    EF::World churned = MakeWorld(1);
    churned.DestroyEntity(churned.CreateEntity());  // 풀에는 흔적이 없는 엔티티 할당 차이

    const EF::WorldHashBreakdown server = MakeWorld(1).ComputeHashBreakdown();
    const EF::WorldHashBreakdown client = churned.ComputeHashBreakdown();

    EXPECT_FALSE(EF::SelectPoolsForPartialResync(client, &server).has_value());
}

TEST_CASE(PartialResync, FallsBackToFullWhenPoolSetsDiffer) {
    // 클라에는 Velocity 풀이 아예 없다(같은 엔티티 이력).
    EF::World client;
    client.RegisterSnapshotComponent<EF::Transform>();
    for (int i = 0; i < 2; ++i) {
        const EF::EntityId entity = client.CreateEntity();
        client.AddComponent<EF::Transform>(entity, MakeTransform(1 + i));
    }

    const EF::WorldHashBreakdown server = MakeWorld(1).ComputeHashBreakdown();
    EXPECT_FALSE(EF::SelectPoolsForPartialResync(client.ComputeHashBreakdown(), &server).has_value());
}

TEST_CASE(PartialResync, FallsBackToFullWhenNoPoolDiffers) {
    // 분해표는 같은데 resync가 요청된 이상한 상황 — 무엇이 틀렸는지 모르므로 전체로 간다.
    const EF::WorldHashBreakdown same = MakeWorld(1).ComputeHashBreakdown();
    EXPECT_FALSE(EF::SelectPoolsForPartialResync(same, &same).has_value());
}

// ── 부분 스냅샷 생성·적용 ───────────────────────────────────────────────────

// 기본 경로: Transform만 어긋난 클라가 서버의 Transform 풀 하나로 서버와 같아진다.
TEST_CASE(PartialResync, PartialSnapshotRepairsTheDivergedPool) {
    const EF::World server = MakeWorld(1);
    EF::World client = MakeWorld(9);
    EXPECT_FALSE(client.ComputeHash() == server.ComputeHash());

    const EF::PartialSnapshot partial = server.CreatePartialSnapshot(42, { TransformHash() });
    EXPECT_EQ(partial.tick, uint64_t{ 42 });
    EXPECT_EQ(partial.components.size(), std::size_t{ 1 });
    EXPECT_EQ(partial.worldHash, server.ComputeHash());

    EXPECT_TRUE(client.ApplyPartialSnapshot(partial));
    EXPECT_EQ(client.ComputeHash(), server.ComputeHash());
    EXPECT_EQ(client.ComputeHash(), partial.worldHash);
}

// 엔티티 할당이 갈라진 월드에는 적용하지 않는다(풀만으로는 못 고친다 → 호출자가 전체로 폴백).
TEST_CASE(PartialResync, ApplyRejectsWhenEntityHashDiffers) {
    const EF::World server = MakeWorld(1);
    EF::World client = MakeWorld(9);
    client.DestroyEntity(client.CreateEntity());

    const uint64_t before = client.ComputeHash();
    EXPECT_FALSE(client.ApplyPartialSnapshot(server.CreatePartialSnapshot(42, { TransformHash() })));
    EXPECT_EQ(client.ComputeHash(), before);
}

// 미등록 타입은 전체 스냅샷에서는 "무시"지만 여기서는 거부한다.
// 서버가 어긋났다고 판정해 보낸 풀을 무시하면, 고쳐지지 않은 채 복구했다고 착각한다.
TEST_CASE(PartialResync, ApplyRejectsUnregisteredType) {
    const EF::World server = MakeWorld(1);

    EF::World client;
    client.RegisterSnapshotComponent<EF::Velocity>();  // Transform은 등록하지 않는다
    for (int i = 0; i < 2; ++i) {
        const EF::EntityId entity = client.CreateEntity();
        client.AddComponent<EF::Transform>(entity, MakeTransform(9 + i));
        client.AddComponent<EF::Velocity>(entity, MakeVelocity(100 + i));
    }

    const uint64_t before = client.ComputeHash();
    EXPECT_FALSE(client.ApplyPartialSnapshot(server.CreatePartialSnapshot(42, { TransformHash() })));
    EXPECT_EQ(client.ComputeHash(), before);
}

// 전부 아니면 전무: 두 번째 풀이 깨져 있으면 첫 번째 풀도 바뀌지 않는다.
TEST_CASE(PartialResync, ApplyIsAllOrNothing) {
    const EF::World server = MakeWorld(1, 200);
    EF::World client = MakeWorld(9, 300);

    EF::PartialSnapshot partial = server.CreatePartialSnapshot(42, { TransformHash(), VelocityHash() });
    EXPECT_EQ(partial.components.size(), std::size_t{ 2 });
    // 뒤쪽 풀의 바이트 수를 망가뜨린다(모양 검사에서 걸린다).
    partial.components.back().bytes.pop_back();

    const uint64_t before = client.ComputeHash();
    EXPECT_FALSE(client.ApplyPartialSnapshot(partial));
    EXPECT_EQ(client.ComputeHash(), before);
}

// 죽은 엔티티 index에 컴포넌트가 붙은 부분 스냅샷은 자기모순이라 거부한다.
TEST_CASE(PartialResync, ApplyRejectsDeadEntityIndex) {
    const EF::World server = MakeWorld(1);
    EF::World client = MakeWorld(9);

    EF::PartialSnapshot partial = server.CreatePartialSnapshot(42, { TransformHash() });
    partial.components.front().entities.back() = 77;  // 존재하지 않는 index

    const uint64_t before = client.ComputeHash();
    EXPECT_FALSE(client.ApplyPartialSnapshot(partial));
    EXPECT_EQ(client.ComputeHash(), before);
}

// 서버 쪽 풀이 비어 있으면 "그 풀을 비워라"로 실린다 — 클라에만 남은 컴포넌트가 지워져야 한다.
// (서버에서 컴포넌트를 떼는 API가 아직 없으므로, 서버가 뜬 부분 스냅샷의 풀을 비워서 흉내 낸다.)
TEST_CASE(PartialResync, EmptyServerPoolClearsClientPool) {
    const EF::World server = MakeWorld(1);
    EF::World client = MakeWorld(1);

    EF::PartialSnapshot emptied = server.CreatePartialSnapshot(42, { VelocityHash() });
    EXPECT_EQ(emptied.components.size(), std::size_t{ 1 });
    emptied.components.front().entities.clear();
    emptied.components.front().bytes.clear();

    EXPECT_TRUE(client.ApplyPartialSnapshot(emptied));

    std::size_t remainingVelocities = 0;
    client.CreateView<EF::Velocity>().ForEach([&](EF::EntityIndex, EF::Velocity&) { ++remainingVelocities; });
    EXPECT_EQ(remainingVelocities, std::size_t{ 0 });
}

// ── 와이어 포맷 ─────────────────────────────────────────────────────────────

TEST_CASE(PartialResync, ResyncRequestRoundTripsHashBreakdown) {
    const EF::ResyncRequest original{
        .tick = 1234,
        .remoteHash = 0xABCDEFull,
        .localBreakdown = MakeWorld(9).ComputeHashBreakdown()
    };

    EF::ResyncRequest decoded;
    EXPECT_TRUE(EF::NetworkManager::DecodeResyncRequest(EF::NetworkManager::EncodeResyncRequest(original), decoded));
    EXPECT_EQ(decoded.tick, original.tick);
    EXPECT_EQ(decoded.remoteHash, original.remoteHash);
    EXPECT_EQ(decoded.localBreakdown.entityHash, original.localBreakdown.entityHash);
    EXPECT_EQ(decoded.localBreakdown.worldHash, original.localBreakdown.worldHash);
    EXPECT_EQ(decoded.localBreakdown.pools.size(), original.localBreakdown.pools.size());
    for (std::size_t i = 0; i < decoded.localBreakdown.pools.size() && i < original.localBreakdown.pools.size(); ++i) {
        EXPECT_EQ(decoded.localBreakdown.pools[i].type, original.localBreakdown.pools[i].type);
        EXPECT_EQ(decoded.localBreakdown.pools[i].hash, original.localBreakdown.pools[i].hash);
    }
}

TEST_CASE(PartialResync, PartialSnapshotRoundTripsAndStillRepairs) {
    const EF::World server = MakeWorld(1);
    EF::World client = MakeWorld(9);

    EF::PartialSnapshot decoded;
    EXPECT_TRUE(EF::NetworkManager::DecodePartialSnapshot(
        EF::NetworkManager::EncodePartialSnapshot(server.CreatePartialSnapshot(42, { TransformHash() })), decoded));
    EXPECT_EQ(decoded.tick, uint64_t{ 42 });

    EXPECT_TRUE(client.ApplyPartialSnapshot(decoded));
    EXPECT_EQ(client.ComputeHash(), server.ComputeHash());
}

// 잘린 페이로드는 어느 지점에서 잘려도 거부한다(신뢰할 수 없는 와이어 데이터).
TEST_CASE(PartialResync, DecodersRejectTruncatedPayloads) {
    const std::vector<uint8_t> request = EF::NetworkManager::EncodeResyncRequest(EF::ResyncRequest{
        .tick = 1, .remoteHash = 2, .localBreakdown = MakeWorld(1).ComputeHashBreakdown()
    });
    const std::vector<uint8_t> partial = EF::NetworkManager::EncodePartialSnapshot(
        MakeWorld(1).CreatePartialSnapshot(3, { TransformHash(), VelocityHash() }));

    for (std::size_t cut = 0; cut < request.size(); ++cut) {
        EF::ResyncRequest out;
        EXPECT_FALSE(EF::NetworkManager::DecodeResyncRequest(
            std::vector<uint8_t>(request.begin(), request.begin() + static_cast<std::ptrdiff_t>(cut)), out));
    }
    for (std::size_t cut = 0; cut < partial.size(); ++cut) {
        EF::PartialSnapshot out;
        EXPECT_FALSE(EF::NetworkManager::DecodePartialSnapshot(
            std::vector<uint8_t>(partial.begin(), partial.begin() + static_cast<std::ptrdiff_t>(cut)), out));
    }
}
