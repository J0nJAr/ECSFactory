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

    // 세션이 들고 나는 사건. **확정 입력과 같은 경로로 흐른다.**
    //
    // 왜 이게 필요한가:
    // lockstep에서 스폰은 원래 복제할 필요가 없다 — 모두가 같은 확정 입력을 같은 tick에
    // 적용하면 각자 알아서 똑같은 엔티티를 만든다. 그런데 "누가 접속했다"는 **입력이 아니다.**
    // 도착 시각이 peer마다 다르고, 클라이언트는 다른 클라이언트의 접속을 스스로 알 수 없다.
    //
    // 그래서 서버가 그 비결정론적 사건을 **특정 tick에 못 박아 확정한다.** 그 뒤로는
    // 확정 입력과 똑같이 취급된다 — 모든 peer가 같은 tick에 같은 순서로 적용하므로
    // 각자 만든 엔티티의 EntityId까지 일치한다. 스폰 자체를 복제하는 게 아니라
    // **스폰을 일으키는 사건**을 복제한다.
    //
    // ⚠️ 이 이벤트를 확정 경로 밖에서 처리하면 그 즉시 EntityManager 상태가 갈라진다
    //    (다음 CreateEntity가 서로 다른 index를 뽑는다). roadmap N2가 정확히 그 문제였다.
    struct SessionEvent {
        enum class Kind : uint8_t {
            Join = 0,    // 이 clientId의 플레이어 엔티티를 만든다
            Leave = 1    // 이 clientId의 플레이어 엔티티를 파괴한다
        };

        Kind kind = Kind::Join;
        uint32_t clientId = 0;
    };

    // 서버가 tick 하나에 대해 확정한 것 전부.
    //
    // ⚠️ 적용 순서는 **events → inputs** 로 고정이다. 같은 tick에 접속과 입력이 함께 있으면
    //    엔티티가 먼저 있어야 입력이 갈 곳이 생긴다. 이 순서를 뒤집으면 접속 첫 tick의
    //    입력이 조용히 사라지고, 그건 peer마다 다르게 나타나지 않으므로 디싱크로도 안 잡힌다.
    struct InputFrame {
        uint64_t tick = 0;
        std::vector<SessionEvent> events;
        std::vector<InputCommand> inputs;
    };

}
