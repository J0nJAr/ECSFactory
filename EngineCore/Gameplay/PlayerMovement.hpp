#pragma once

#include <cstdint>
#include <span>

#include "ECS/Core.hpp"
#include "Network/InputCommand.hpp"

namespace EF {

    class World;

    // 이동 입력 비트(buttonMask). 나머지 비트는 행동 입력 등으로 확장.
    inline constexpr uint64_t kInputMoveRight   = 1ull << 0;
    inline constexpr uint64_t kInputMoveLeft    = 1ull << 1;
    inline constexpr uint64_t kInputMoveForward = 1ull << 2;
    inline constexpr uint64_t kInputMoveBack    = 1ull << 3;

    // 플레이어 하나가 스폰될 때 옆자리와 벌어지는 거리(월드 유닛).
    //
    // 이동이 tick당 1유닛이라 6이면 6 tick(50Hz에서 0.12초) 밀어야 옆자리에 닿는다.
    // 겹쳐 나오면 화면에서 누가 누군지 구별이 안 되고, 너무 멀면 초기 시야에서 벗어난다.
    inline constexpr int32_t kPlayerSpawnSpacingUnits = 6;

    // 확정된 세션 이벤트를 적용한다. **모든 peer가 같은 tick에 같은 순서로 부른다.**
    //
    // ⚠️ 이 함수가 이 엔진의 결정론 스폰을 떠받친다. 서버가 혼자 스폰하고 결과를 알려주는
    //    구조가 아니다 — 서버와 모든 클라이언트가 **각자** 이 함수를 돌려 각자 엔티티를 만들고,
    //    EntityManager 상태가 같으므로 같은 EntityId가 나온다.
    //
    //    그래서 여기 있는 것은 전부 확정 입력의 순수 함수여야 한다.
    //    난수·시간·프레임률·접속 순서 같은 peer마다 다른 것을 절대 읽지 말 것.
    //    clientId에서 유도되는 값만 쓴다.
    //
    // ⚠️ 이벤트 순서는 호출자가 보장한다(서버가 clientId로 정렬해서 확정한다).
    //    여기서 다시 정렬하지 않는 이유: 정렬을 두 곳에서 하면 한쪽만 바뀌었을 때
    //    조용히 갈라진다. 순서의 책임을 한 곳에 둔다.
    void ApplySessionEvents(World& world, std::span<const SessionEvent> events);

    // clientId가 조종하는 엔티티를 찾는다. 없으면 kNullEntity.
    // 서버가 장부(NetworkClientState.controlledEntity)를 채울 때 쓴다.
    [[nodiscard]] EntityId FindPlayerEntityFor(World& world, uint32_t clientId);

    // 확정 입력셋을 결정론적으로 적용한다.
    //  - 각 입력은 clientId로 PlayerControl 엔티티에 매칭된다.
    //  - buttonMask 이동 비트만큼 Transform.position을 Fixed64 고정 스텝으로 옮긴다.
    // 순수 Fixed64 연산이라 서버·클라에서 비트 단위로 동일한 결과가 난다.
    // 서버·모든 클라가 같은 확정 입력셋을 넣으면 상태가 일치한다(lockstep).
    void ApplyConfirmedInputs(World& world, std::span<const InputCommand> inputs);

}
