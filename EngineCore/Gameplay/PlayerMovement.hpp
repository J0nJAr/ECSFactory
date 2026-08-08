#pragma once

#include <cstdint>
#include <span>

#include "Network/InputCommand.hpp"

namespace EF {

    class World;

    // 이동 입력 비트(buttonMask). 나머지 비트는 행동 입력 등으로 확장.
    inline constexpr uint64_t kInputMoveRight   = 1ull << 0;
    inline constexpr uint64_t kInputMoveLeft    = 1ull << 1;
    inline constexpr uint64_t kInputMoveForward = 1ull << 2;
    inline constexpr uint64_t kInputMoveBack    = 1ull << 3;

    // 확정 입력셋을 결정론적으로 적용한다.
    //  - 각 입력은 clientId로 PlayerControl 엔티티에 매칭된다.
    //  - buttonMask 이동 비트만큼 Transform.position을 Fixed64 고정 스텝으로 옮긴다.
    // 순수 Fixed64 연산이라 서버·클라에서 비트 단위로 동일한 결과가 난다.
    // 서버·모든 클라가 같은 확정 입력셋을 넣으면 상태가 일치한다(lockstep).
    void ApplyConfirmedInputs(World& world, std::span<const InputCommand> inputs);

}
