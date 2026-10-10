#include <cstdint>
#include <string_view>

#include "TestHarness.hpp"

#include "ECS/TypeInfo.hpp"
#include "ECS/World.hpp"

namespace {
    /**
     * "두 프로세스가 타입을 다른 순서로 처음 썼다"를 한 프로세스 안에서 재현하기 위한 장치.
     *
     * `TypeInfo::index` 는 프로세스 전역 카운터라 한 프로세스 안에서는 순서를 뒤집을 수 없다.
     * 대신 `TypeInfo::hash` 는 `static constexpr TypeName` 으로 덮어쓸 수 있다는 점을 쓴다 —
     * **이름(= 해시)은 같고 C++ 타입은 다른** 쌍을 만들면, 둘은
     *
     *   - 같은 TypeHash / size / alignment 를 갖고(= 네트워크 너머에서 "같은 컴포넌트"),
     *   - 서로 다른 TypeIndex 를 받는다(= 처음 쓴 순서가 다른 프로세스).
     *
     * 아래 테스트는 서버 역할 월드에서 Alpha→Beta 순으로, 클라 역할 월드에서 Beta→Alpha 순으로
     * 타입을 처음 써서 두 월드의 풀 index 순서를 반대로 만든다. 내용은 완전히 같으므로
     * `ComputeHash()` 는 같은 값을 내야 한다.
     */
    struct AlphaAsServer {
        static constexpr std::string_view TypeName = "WorldHashTestAlpha";
        uint32_t value;
    };

    struct AlphaAsClient {
        static constexpr std::string_view TypeName = "WorldHashTestAlpha";
        uint32_t value;
    };

    struct BetaAsServer {
        static constexpr std::string_view TypeName = "WorldHashTestBeta";
        uint32_t value;
    };

    struct BetaAsClient {
        static constexpr std::string_view TypeName = "WorldHashTestBeta";
        uint32_t value;
    };
}

// 결함 재현: 내용이 같아도 타입을 처음 쓴 순서가 다르면 해시가 갈라지던 문제.
TEST_CASE(WorldHash, TypeFirstUseOrderDoesNotChangeHash) {
    // 서버 역할 — Alpha를 먼저 쓴다.
    EF::World server;
    const EF::EntityId serverFirst = server.CreateEntity();
    const EF::EntityId serverSecond = server.CreateEntity();
    server.AddComponent<AlphaAsServer>(serverFirst, AlphaAsServer{ 7 });
    server.AddComponent<BetaAsServer>(serverSecond, BetaAsServer{ 9 });

    // 클라 역할 — 같은 내용이지만 Beta를 먼저 쓴다.
    EF::World client;
    const EF::EntityId clientFirst = client.CreateEntity();
    const EF::EntityId clientSecond = client.CreateEntity();
    client.AddComponent<BetaAsClient>(clientSecond, BetaAsClient{ 9 });
    client.AddComponent<AlphaAsClient>(clientFirst, AlphaAsClient{ 7 });

    // 전제 확인. 이 두 줄이 깨지면 이 테스트는 아무것도 검증하지 못한다 —
    // 해시가 같지 않으면(=같은 컴포넌트가 아니면) 애초에 비교할 이유가 없고,
    // index 순서가 뒤집히지 않았으면 결함 상황을 재현하지 못한 것이다.
    EXPECT_EQ(EF::GetTypeInfo<AlphaAsServer>().hash, EF::GetTypeInfo<AlphaAsClient>().hash);
    EXPECT_EQ(EF::GetTypeInfo<BetaAsServer>().hash, EF::GetTypeInfo<BetaAsClient>().hash);
    EXPECT_TRUE(EF::GetTypeInfo<AlphaAsServer>().index < EF::GetTypeInfo<BetaAsServer>().index);
    EXPECT_TRUE(EF::GetTypeInfo<BetaAsClient>().index < EF::GetTypeInfo<AlphaAsClient>().index);

    EXPECT_EQ(server.ComputeHash(), client.ComputeHash());
}

// 순서를 고정했다고 해서 해시가 "타입과 바이트의 결합"을 잃으면 안 된다.
// 값만 서로 맞바꾼 월드는 여전히 다른 해시여야 한다 — 그렇지 않으면
// 정렬이 체크섬의 탐지력을 깎아먹은 것이다.
TEST_CASE(WorldHash, SwappingValuesBetweenTypesStillChangesHash) {
    EF::World original;
    const EF::EntityId a = original.CreateEntity();
    original.AddComponent<AlphaAsServer>(a, AlphaAsServer{ 1 });
    original.AddComponent<BetaAsServer>(a, BetaAsServer{ 2 });

    EF::World swapped;
    const EF::EntityId b = swapped.CreateEntity();
    swapped.AddComponent<AlphaAsServer>(b, AlphaAsServer{ 2 });
    swapped.AddComponent<BetaAsServer>(b, BetaAsServer{ 1 });

    EXPECT_TRUE(original.ComputeHash() != swapped.ComputeHash());
}

// N4.1 — 컴포넌트가 같아도 엔티티 할당 상태가 다르면 다른 해시여야 한다.
//
// 두 월드 모두 index 0 엔티티에 같은 컴포넌트 하나만 붙어 있다. 차이는 한쪽이 엔티티를
// 하나 더 만들었다 지웠다는 것뿐이다 — 컴포넌트 풀에는 흔적이 없지만 next index·version·
// free list가 달라서, 다음 `CreateEntity()`가 서로 다른 id를 낸다. lockstep에서 이건
// 곧 디싱크이므로 해시가 지금 잡아야 한다(예전엔 섞지 않아서 놓쳤다).
TEST_CASE(WorldHash, EntityAllocationDifferenceChangesHash) {
    EF::World plain;
    const EF::EntityId plainEntity = plain.CreateEntity();
    plain.AddComponent<AlphaAsServer>(plainEntity, AlphaAsServer{ 5 });

    EF::World churned;
    const EF::EntityId churnedEntity = churned.CreateEntity();
    const EF::EntityId temporary = churned.CreateEntity();
    churned.DestroyEntity(temporary);
    churned.AddComponent<AlphaAsServer>(churnedEntity, AlphaAsServer{ 5 });

    // 전제: 살아 있는 엔티티와 컴포넌트는 같다(같은 index, 같은 값).
    EXPECT_EQ(EF::GetEntityIndex(plainEntity), EF::GetEntityIndex(churnedEntity));

    EXPECT_TRUE(plain.ComputeHash() != churned.ComputeHash());
}

// free list는 순서가 의미를 갖는다(LIFO — 다음에 어떤 index가 나오는가).
// 같은 두 엔티티를 다른 순서로 지운 월드는 다음 할당이 갈라지므로 다른 해시여야 한다.
TEST_CASE(WorldHash, FreeListOrderChangesHash) {
    auto makeWorld = [](bool destroyLowFirst) {
        EF::World world;
        world.CreateEntity();
        const EF::EntityId low = world.CreateEntity();
        const EF::EntityId high = world.CreateEntity();
        if (destroyLowFirst) {
            world.DestroyEntity(low);
            world.DestroyEntity(high);
        } else {
            world.DestroyEntity(high);
            world.DestroyEntity(low);
        }
        return world;
    };

    const EF::World lowFirst = makeWorld(true);
    const EF::World highFirst = makeWorld(false);

    EXPECT_TRUE(lowFirst.ComputeHash() != highFirst.ComputeHash());
}

// 같은 할당 이력을 밟은 두 월드는 여전히 같은 해시여야 한다(엔티티 상태를 섞으면서
// 해시가 결정론을 잃지 않았는지 — 예: 포인터나 용량 같은 비결정 값을 섞는 실수).
TEST_CASE(WorldHash, SameAllocationHistoryKeepsHashEqual) {
    auto makeWorld = []() {
        EF::World world;
        const EF::EntityId first = world.CreateEntity();
        const EF::EntityId second = world.CreateEntity();
        world.DestroyEntity(first);
        world.CreateEntity();  // free list에서 first의 index를 version 1로 재사용
        world.AddComponent<AlphaAsServer>(second, AlphaAsServer{ 3 });
        return world;
    };

    EXPECT_EQ(makeWorld().ComputeHash(), makeWorld().ComputeHash());
}

// ─────────────────────────────────────────────────────────────────────────────
// N4.2 — 해시 분해표 (엔티티·풀별·월드). 부분 resync가 "어느 풀이 어긋났나"를 고르는 근거다.
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    // 분해표에서 type에 해당하는 풀 해시를 찾는다. 없으면 0.
    uint64_t PoolHashOf(const EF::WorldHashBreakdown& breakdown, EF::TypeHash type) {
        for (const EF::PoolHash& pool : breakdown.pools) {
            if (pool.type == type) {
                return pool.hash;
            }
        }
        return 0;
    }

    struct PresentationOnly {
        uint32_t color;
    };
}

// 계산 경로가 하나라는 불변식: ComputeHash()는 분해표의 worldHash와 같아야 한다.
TEST_CASE(WorldHash, BreakdownWorldHashMatchesComputeHash) {
    EF::World world;
    const EF::EntityId entity = world.CreateEntity();
    world.AddComponent<AlphaAsServer>(entity, AlphaAsServer{ 11 });
    world.AddComponent<BetaAsServer>(entity, BetaAsServer{ 22 });

    EXPECT_EQ(world.ComputeHashBreakdown().worldHash, world.ComputeHash());
}

// 분해표의 풀 목록은 TypeHash 오름차순이고, 타입 첫 사용 순서와 무관하다.
// (TypeFirstUseOrderDoesNotChangeHash와 같은 장치로 index 순서를 뒤집는다.)
TEST_CASE(WorldHash, BreakdownPoolsAreSortedAndIndependentOfFirstUseOrder) {
    EF::World server;
    const EF::EntityId serverEntity = server.CreateEntity();
    server.AddComponent<AlphaAsServer>(serverEntity, AlphaAsServer{ 7 });
    server.AddComponent<BetaAsServer>(serverEntity, BetaAsServer{ 9 });

    EF::World client;
    const EF::EntityId clientEntity = client.CreateEntity();
    client.AddComponent<BetaAsClient>(clientEntity, BetaAsClient{ 9 });
    client.AddComponent<AlphaAsClient>(clientEntity, AlphaAsClient{ 7 });

    const EF::WorldHashBreakdown a = server.ComputeHashBreakdown();
    const EF::WorldHashBreakdown b = client.ComputeHashBreakdown();

    EXPECT_EQ(a.pools.size(), std::size_t{ 2 });
    EXPECT_EQ(b.pools.size(), std::size_t{ 2 });
    EXPECT_TRUE(a.pools[0].type < a.pools[1].type);
    for (std::size_t i = 0; i < a.pools.size() && i < b.pools.size(); ++i) {
        EXPECT_EQ(a.pools[i].type, b.pools[i].type);
        EXPECT_EQ(a.pools[i].hash, b.pools[i].hash);
    }
    EXPECT_EQ(a.worldHash, b.worldHash);
}

// 풀 하나의 값만 다르면 그 풀의 해시만 달라진다 — 엔티티 해시와 다른 풀 해시는 그대로.
// 이게 깨지면(예: 풀끼리 누산기를 공유) 부분 resync가 멀쩡한 풀까지 보내게 된다.
TEST_CASE(WorldHash, ChangingOnePoolChangesOnlyThatPoolHash) {
    auto makeWorld = [](uint32_t betaValue) {
        EF::World world;
        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<AlphaAsServer>(entity, AlphaAsServer{ 1 });
        world.AddComponent<BetaAsServer>(entity, BetaAsServer{ betaValue });
        return world;
    };

    const EF::WorldHashBreakdown original = makeWorld(2).ComputeHashBreakdown();
    const EF::WorldHashBreakdown changed = makeWorld(3).ComputeHashBreakdown();

    const EF::TypeHash alpha = EF::GetTypeInfo<AlphaAsServer>().hash;
    const EF::TypeHash beta = EF::GetTypeInfo<BetaAsServer>().hash;

    EXPECT_EQ(original.entityHash, changed.entityHash);
    EXPECT_EQ(PoolHashOf(original, alpha), PoolHashOf(changed, alpha));
    EXPECT_TRUE(PoolHashOf(original, beta) != PoolHashOf(changed, beta));
    EXPECT_TRUE(original.worldHash != changed.worldHash);
}

// 엔티티 할당만 다르면 엔티티 해시만 달라진다(풀 해시는 그대로).
TEST_CASE(WorldHash, EntityChurnChangesOnlyEntityHash) {
    EF::World plain;
    const EF::EntityId plainEntity = plain.CreateEntity();
    plain.AddComponent<AlphaAsServer>(plainEntity, AlphaAsServer{ 5 });

    EF::World churned;
    const EF::EntityId churnedEntity = churned.CreateEntity();
    churned.DestroyEntity(churned.CreateEntity());
    churned.AddComponent<AlphaAsServer>(churnedEntity, AlphaAsServer{ 5 });

    const EF::WorldHashBreakdown a = plain.ComputeHashBreakdown();
    const EF::WorldHashBreakdown b = churned.ComputeHashBreakdown();

    const EF::TypeHash alpha = EF::GetTypeInfo<AlphaAsServer>().hash;
    EXPECT_TRUE(a.entityHash != b.entityHash);
    EXPECT_EQ(PoolHashOf(a, alpha), PoolHashOf(b, alpha));
}

// 표현 전용 컴포넌트는 분해표에도 나타나지 않는다(서버에 없는 풀이라 넣으면 영원히 "다름").
TEST_CASE(WorldHash, BreakdownSkipsPresentationComponents) {
    EF::World world;
    world.RegisterPresentationComponent<PresentationOnly>();
    const EF::EntityId entity = world.CreateEntity();
    world.AddComponent<AlphaAsServer>(entity, AlphaAsServer{ 1 });
    world.AddComponent<PresentationOnly>(entity, PresentationOnly{ 0xFF00FFu });

    const EF::WorldHashBreakdown breakdown = world.ComputeHashBreakdown();
    EXPECT_EQ(breakdown.pools.size(), std::size_t{ 1 });
    EXPECT_EQ(PoolHashOf(breakdown, EF::GetTypeInfo<PresentationOnly>().hash), uint64_t{ 0 });
}
