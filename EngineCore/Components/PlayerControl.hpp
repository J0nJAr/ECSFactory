#pragma once

#include <cstdint>

namespace EF {

    // 이 엔티티를 어느 clientId가 조종하는지 나타내는 컴포넌트.
    // 스냅샷으로 서버→클라 전파되어(trivially copyable), 서버와 모든 클라가
    // 같은 입력을 같은 엔티티에 적용할 수 있게 한다.
    struct PlayerControl {
        uint32_t clientId = 0;
    };

}
