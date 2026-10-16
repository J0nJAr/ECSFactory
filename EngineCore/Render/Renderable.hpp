#pragma once

#include "Render/RenderTypes.hpp"

namespace EF {

    // "이 엔티티를 어떻게 그릴 것인가". 클라이언트 전용 표현 컴포넌트다.
    //
    // ⚠️ 스냅샷 대상이 아니다. `RegisterSnapshotComponent<Renderable>()` 를 부르지 말 것.
    //    색과 크기는 시뮬레이션 상태가 아니라서 서버가 알 필요도, 해시에 들어갈 이유도 없다.
    //    (docs/design/rendering-dx11.md §0 원칙 3)
    //
    // ⚠️ **그래서 스냅샷을 받으면 사라진다.** World::ApplySnapshot은 레지스트리를 새로 만들어
    //    통째로 교체하므로, 스냅샷에 없는 컴포넌트는 전부 날아간다. 스냅샷은 조인과 resync에서
    //    오므로 "접속 직후"와 "디싱크 복구 직후"에 화면이 비게 된다.
    //    붙이는 쪽이 매 프레임 "없으면 붙인다"로 동작해야 한다.
    //    (GameClient의 EnsurePlayerVisuals 참조. 이 성질을 모르면 resync 후 화면이 빈 채로
    //     남는데, 원인을 렌더러에서 찾게 되어 시간을 크게 버린다.)
    //
    // 지금은 색과 크기뿐이다. 텍스처·스프라이트·메시 핸들은 여기 필드가 늘어나는 형태로 온다.
    struct Renderable {
        Color color;

        // 지면(XZ 평면) 위의 크기. x는 월드 X 방향, y는 월드 **Z** 방향 길이다.
        // DrawItem.size와 같은 규약이다(Render/RenderContext.hpp).
        Float2 size{ 1.0f, 1.0f };
    };

}
