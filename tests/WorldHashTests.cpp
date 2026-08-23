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
