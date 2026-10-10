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
