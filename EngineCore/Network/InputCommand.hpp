#pragma once

#include <cstdint>
#include <vector>

#include "Math/Fixed64.hpp"

namespace EF {

    // 클라이언트가 특정 tick에 대해 제출하는 입력.
    struct InputCommand {
        uint32_t clientId = 0;
        uint64_t tick = 0;      // 이 입력이 적용될 tick (applyTick)
        uint32_t sequence = 0;
        uint64_t buttonMask = 0;
        // 마우스 룩 등은 화면 픽셀(float)이 아니라 결정론적 월드 의도(Fixed64)로 싣는다.
        Fixed64 aimYaw = Fixed64::Zero();
        Fixed64 aimPitch = Fixed64::Zero();
    };

    // 특정 tick의 월드 상태 체크섬. 결정론 검증(디싱크 감지)용.
    struct TickHash {
        uint64_t tick = 0;
        uint64_t worldHash = 0;
    };

    struct InputFrame {
        uint64_t tick = 0;
        std::vector<InputCommand> inputs;
    };

}
