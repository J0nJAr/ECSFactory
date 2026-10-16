#pragma once

#include "Render/RenderTypes.hpp"

namespace EF::UI {

    // 앵커 레이아웃 코어 — "부모 사각형과 앵커 정보가 주어지면 자식 사각형은 어디인가".
    // (docs/design/ui-layer.md §2)
    //
    // ⚠️ 좌표계는 화면 픽셀이고 Y가 아래로 증가한다(Render/RenderTypes.hpp의 Rect 주석).
    //    그래서 Top이 작은 y(0), Bottom이 큰 y(1)다. 월드 좌표계(Y가 위)와 반대라서
    //    아래 프리셋 표를 읽을 때 제일 헷갈리는 지점이다 — TopLeft가 {0,0},
    //    BottomRight가 {1,1}인 것이 정상이다.
    //
    // ⚠️ 전부 float다. UI는 결정론 대상이 아니다(설계서 §0-1). 여기서 나온 값이
    //    시뮬레이션(Fixed64)으로 되돌아가면 안 된다 — 화면에만 쓴다. Fixed64를 쓰지 않는
    //    이유가 이것이다. UI 레이아웃은 peer마다 창 크기가 달라 애초에 같은 값이 나올 수
    //    없고, 같을 필요도 없다.
    //
    // ⚠️ 이 파일은 유지형(Widget 트리)과 즉시형(UIContext)이 **함께** 쓴다(설계서 §1).
    //    그래서 순수 함수만 둔다 — 위젯도, 컨텍스트도, 전역 상태도 참조하지 않는다.
    //    한쪽에 의존을 붙이는 순간 다른 쪽이 못 쓰게 되고, "UI 시스템 두 개"로 갈라진다.
    //    그 갈라짐을 막는 것이 U3을 두 트랙보다 먼저 만드는 이유다.

    // 부모 사각형 안에서의 정규화(0~1) 기준 영역.
    //
    // 축마다 독립으로 판정한다:
    //   min == max 인 축 → 점 앵커. 자식이 그 지점에 매달리고 size를 그대로 쓴다.
    //   min != max 인 축 → stretch 앵커. 자식이 부모를 따라 늘어나고 offset이 여백이 된다.
    // StretchX가 "X만 늘어나고 Y는 점 앵커"인 것이 이 축별 판정의 결과다.
    struct AnchorRect {
        Float2 min{0.0f, 0.0f};
        Float2 max{0.0f, 0.0f};
    };

    // 겉으로 쓰는 프리셋 — 프리셋은 AnchorRect로 변환되는 표기 편의일 뿐이다.
    // 프리셋만 있으면 stretch를 표현할 수 없고, min/max만 노출하면 쓰기 불편하다.
    // 둘 다 두면 나중에 에디터에서 자유로운 앵커 편집을 붙여도 코어를 안 고친다(설계서 §2).
    enum class Anchor {
        TopLeft, Top, TopRight,
        Left,    Center, Right,
        BottomLeft, Bottom, BottomRight,

        // stretch 프리셋의 "늘어나지 않는 축"은 부모 중앙(0.5)에 둔다.
        // 설계서 §2는 이 축의 값을 정해두지 않았다. 중앙을 고른 이유는 위/아래 어느 쪽으로도
        // offset으로 옮길 수 있는 중립값이기 때문이고, 하단 고정 로그 패널처럼 확실히
        // 치우친 배치는 AnchorRect{{0,1},{1,1}}로 직접 쓰는 편이 의도가 더 분명해서다.
        StretchX,     // 좌우로 늘어남 (폭 100% 로그 패널). Y는 부모 중앙의 점 앵커.
        StretchY,     // 위아래로 늘어남 (높이 100% 사이드바). X는 부모 중앙의 점 앵커.
        StretchAll    // 부모를 가득 채움
    };

    [[nodiscard]] AnchorRect ToAnchorRect(Anchor preset) noexcept;

    // 프리셋에 어울리는 기본 pivot. 앵커와 같은 지점을 고른다
    // (TopLeft→{0,0}, Center→{0.5,0.5}, BottomRight→{1,1}).
    //
    // Unity는 기본 pivot이 항상 {0.5,0.5}지만 여기서는 따르지 않았다. 그러면 설계서 §4의
    //     ui.BeginPanel("debug", Anchor::TopRight, {320, 200})
    // 이 부모 밖으로 절반이 튀어나간다. 즉시형 호출부는 pivot을 안 넘기는 것이 기본이라
    // 이 기본값이 곧 화면에 보이는 결과다. "구석에 붙이랬더니 구석에 걸쳤다"보다
    // "구석에 딱 붙는다"가 놀라움이 적다.
    // stretch 축의 pivot은 쓰이지 않으므로 무해한 0.5가 된다.
    [[nodiscard]] Float2 DefaultPivot(Anchor preset) noexcept;

    // 자식 배치 명세.
    //
    // ⚠️ 필드의 의미가 축마다 다르다. 설계서 §2의 모델이 원래 그렇다:
    //
    //                 점 앵커 축 (min == max)        stretch 축 (min != max)
    //     offsetMin   기준점으로부터의 변위           가까운 쪽 여백 (좌 / 상)
    //     offsetMax   쓰지 않음                       먼 쪽 여백 (우 / 하)
    //     size        자식 크기 (지정값 그대로)       쓰지 않음 (부모를 따라간다)
    //     pivot       size 안에서의 기준점 0~1        쓰지 않음
    //
    // 왜 offsetMin/offsetMax를 나눠 뒀나: 설계서 §2의 stretch 식이 여백을 두 개 요구하는데
    // (min = anchorMin + offsetMin, max = anchorMax - offsetMax), 같은 문서가 제시한 간편
    // 시그니처 ResolveRect(parent, anchor, offset, size)에는 offset이 하나뿐이라 그대로는
    // 식을 채울 수 없다. 비대칭 여백(상단 타이틀바 40px를 피해 나머지를 채우는 패널 같은 것)은
    // 실제로 필요하므로 코어에는 두 개를 두고, 간편형을 그 위의 얇은 껍데기로 만들었다.
    struct LayoutSpec {
        AnchorRect anchor{};
        Float2 offsetMin{0.0f, 0.0f};
        Float2 offsetMax{0.0f, 0.0f};
        Float2 size{0.0f, 0.0f};
        Float2 pivot{0.0f, 0.0f};
    };

    // 핵심 순수 함수. 부모 Rect + 명세 → 자식 Rect.
    //
    // 부모의 min을 원점으로 삼기 때문에 중첩은 그냥 결과를 다시 넣으면 누적된다 —
    // 트리를 부모→자식 순서로 한 번만 훑으면 레이아웃이 끝난다(설계서 §3).
    [[nodiscard]] Rect ResolveRect(const Rect& parent, const LayoutSpec& spec) noexcept;

    // 간편형 — 프리셋과 offset/size/pivot으로 바로 계산한다.
    //
    // stretch 축에서 offset은 **양쪽 대칭 여백**으로 해석된다(offsetMin = offsetMax = offset).
    // 위에 적은 간극(여백 둘 vs offset 하나)을 대칭으로 메운 것이다. 이렇게 두면
    // StretchAll + offset{8,8}이 "사방 8px 안쪽"이 되어 가장 흔한 쓰임(설계서 §4의 디버그·
    // 로그 패널)이 한 줄로 나온다. 비대칭이 필요하면 LayoutSpec을 직접 쓴다.
    [[nodiscard]] Rect ResolveRect(
        const Rect& parent,
        Anchor preset,
        Float2 offset,
        Float2 size,
        Float2 pivot) noexcept;

    // pivot 생략형 — DefaultPivot(preset)을 쓴다. 즉시형 호출부가 기대하는 형태다.
    [[nodiscard]] Rect ResolveRect(
        const Rect& parent,
        Anchor preset,
        Float2 offset,
        Float2 size) noexcept;

}
