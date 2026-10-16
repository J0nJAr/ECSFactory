#include "UI/AnchorLayout.hpp"
#include "TestHarness.hpp"

#include <cmath>

namespace {
    // UI는 결정론 대상이 아니라 float로 계산한다(docs/design/ui-layer.md §0-1).
    // 그래서 정확 비교 대신 허용오차를 쓴다 — 여기 값들은 화면 픽셀이고,
    // 1e-3px면 눈에 보이는 차이보다 세 자리 아래라 "맞다/틀리다"를 가르기에 충분하다.
    constexpr float kTolerance = 1e-3f;
}

// 실패 위치가 헬퍼 함수가 아니라 호출한 줄로 찍히도록 매크로로 둔다.
#define EXPECT_NEAR(actual, expected)                                                       \
    do {                                                                                    \
        const float actual_ = (actual);                                                     \
        const float expected_ = (expected);                                                 \
        if (!(std::fabs(actual_ - expected_) <= kTolerance)) {                              \
            ::Test::FailEqual(#actual, #expected, actual_, expected_, __FILE__, __LINE__);  \
        }                                                                                   \
    } while (false)

#define EXPECT_RECT(rect, expectMinX, expectMinY, expectMaxX, expectMaxY)                   \
    do {                                                                                    \
        const EF::Rect resolved_ = (rect);                                                  \
        EXPECT_NEAR(resolved_.min.x, (expectMinX));                                         \
        EXPECT_NEAR(resolved_.min.y, (expectMinY));                                         \
        EXPECT_NEAR(resolved_.max.x, (expectMaxX));                                         \
        EXPECT_NEAR(resolved_.max.y, (expectMaxY));                                         \
    } while (false)

// 프리셋 표 자체를 화면 좌표계 규약(Y가 아래로 증가) 기준으로 못 박는다.
// 이 테스트가 깨지면 Top/Bottom이 뒤집힌 것이다 — 제일 저지르기 쉬운 실수라 따로 둔다.
TEST_CASE(UI, PresetAnchorRectsFollowScreenSpaceYDown) {
    using EF::UI::Anchor;
    using EF::UI::ToAnchorRect;

    // Top이 작은 y, Bottom이 큰 y다.
    EXPECT_NEAR(ToAnchorRect(Anchor::Top).min.y, 0.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::Center).min.y, 0.5f);
    EXPECT_NEAR(ToAnchorRect(Anchor::Bottom).min.y, 1.0f);
    EXPECT_TRUE(ToAnchorRect(Anchor::Top).min.y < ToAnchorRect(Anchor::Bottom).min.y);

    // 네 구석.
    EXPECT_NEAR(ToAnchorRect(Anchor::TopLeft).min.x, 0.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::TopLeft).min.y, 0.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::TopRight).min.x, 1.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::TopRight).min.y, 0.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::BottomLeft).min.x, 0.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::BottomLeft).min.y, 1.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::BottomRight).min.x, 1.0f);
    EXPECT_NEAR(ToAnchorRect(Anchor::BottomRight).min.y, 1.0f);

    // 9개 프리셋은 전부 두 축 모두 점 앵커여야 한다(min == max).
    const Anchor points[] = {
        Anchor::TopLeft, Anchor::Top, Anchor::TopRight,
        Anchor::Left, Anchor::Center, Anchor::Right,
        Anchor::BottomLeft, Anchor::Bottom, Anchor::BottomRight
    };
    for (const Anchor preset : points) {
        const EF::UI::AnchorRect anchor = ToAnchorRect(preset);
        EXPECT_TRUE(anchor.min.x == anchor.max.x);
        EXPECT_TRUE(anchor.min.y == anchor.max.y);
    }

    // stretch 프리셋은 늘어나는 축만 min != max 여야 한다.
    const EF::UI::AnchorRect stretchX = ToAnchorRect(Anchor::StretchX);
    EXPECT_TRUE(stretchX.min.x != stretchX.max.x);
    EXPECT_TRUE(stretchX.min.y == stretchX.max.y);

    const EF::UI::AnchorRect stretchY = ToAnchorRect(Anchor::StretchY);
    EXPECT_TRUE(stretchY.min.x == stretchY.max.x);
    EXPECT_TRUE(stretchY.min.y != stretchY.max.y);

    const EF::UI::AnchorRect stretchAll = ToAnchorRect(Anchor::StretchAll);
    EXPECT_TRUE(stretchAll.min.x != stretchAll.max.x);
    EXPECT_TRUE(stretchAll.min.y != stretchAll.max.y);
}

// 9개 점 앵커 프리셋이 각각 기대 위치를 낸다.
//
// 부모를 일부러 원점이 아닌 (100,200)에 둔다 — 부모 offset을 계산에 반영하지 않는 구현도
// 부모가 원점이면 전부 통과해 버리기 때문이다. 여기서는 그런 구현이 9개 전부에서 깨진다.
TEST_CASE(UI, PointAnchorPresetsPlaceChildRelativeToParentOrigin) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {100.0f, 200.0f}, {500.0f, 600.0f} };  // 400x400
    const EF::Float2 offset{0.0f, 0.0f};
    const EF::Float2 size{40.0f, 20.0f};
    const EF::Float2 pivot{0.0f, 0.0f};  // 앵커 위치만 보기 위해 pivot 영향을 뺀다

    // 위쪽 줄 (y = parent.min.y = 200)
    EXPECT_RECT(ResolveRect(parent, Anchor::TopLeft,  offset, size, pivot), 100.0f, 200.0f, 140.0f, 220.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::Top,      offset, size, pivot), 300.0f, 200.0f, 340.0f, 220.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::TopRight, offset, size, pivot), 500.0f, 200.0f, 540.0f, 220.0f);

    // 가운데 줄 (y = 400)
    EXPECT_RECT(ResolveRect(parent, Anchor::Left,   offset, size, pivot), 100.0f, 400.0f, 140.0f, 420.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::Center, offset, size, pivot), 300.0f, 400.0f, 340.0f, 420.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::Right,  offset, size, pivot), 500.0f, 400.0f, 540.0f, 420.0f);

    // 아래쪽 줄 (y = parent.max.y = 600) — Y가 아래로 증가하므로 Bottom이 더 큰 y다.
    EXPECT_RECT(ResolveRect(parent, Anchor::BottomLeft,  offset, size, pivot), 100.0f, 600.0f, 140.0f, 620.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::Bottom,      offset, size, pivot), 300.0f, 600.0f, 340.0f, 620.0f);
    EXPECT_RECT(ResolveRect(parent, Anchor::BottomRight, offset, size, pivot), 500.0f, 600.0f, 540.0f, 620.0f);
}

// 점 앵커 축에서 offset은 부호 있는 변위다. +y는 화면 아래쪽이다.
TEST_CASE(UI, OffsetOnPointAnchorShiftsAlongScreenAxes) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {0.0f, 0.0f}, {100.0f, 100.0f} };
    const EF::Float2 size{30.0f, 30.0f};
    const EF::Float2 pivot{0.0f, 0.0f};

    EXPECT_RECT(ResolveRect(parent, Anchor::TopLeft, {10.0f, 20.0f}, size, pivot),
                10.0f, 20.0f, 40.0f, 50.0f);

    // 음수 offset은 반대 방향. Bottom 기준에서 -25는 위로 올라간다.
    EXPECT_RECT(ResolveRect(parent, Anchor::BottomLeft, {0.0f, -25.0f}, size, pivot),
                0.0f, 75.0f, 30.0f, 105.0f);
}

// pivot이 "size 안의 어느 점을 앵커에 맞출지"를 정한다.
TEST_CASE(UI, PivotMovesTheReferencePoint) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {0.0f, 0.0f}, {200.0f, 200.0f} };  // 중앙 = (100,100)
    const EF::Float2 offset{0.0f, 0.0f};
    const EF::Float2 size{40.0f, 40.0f};

    // pivot {0,0} — 좌상단이 중앙에 놓인다.
    EXPECT_RECT(ResolveRect(parent, Anchor::Center, offset, size, {0.0f, 0.0f}),
                100.0f, 100.0f, 140.0f, 140.0f);

    // pivot {0.5,0.5} — 자식의 중앙이 중앙에 놓인다.
    EXPECT_RECT(ResolveRect(parent, Anchor::Center, offset, size, {0.5f, 0.5f}),
                80.0f, 80.0f, 120.0f, 120.0f);

    // pivot {1,1} — 우하단이 중앙에 놓인다.
    EXPECT_RECT(ResolveRect(parent, Anchor::Center, offset, size, {1.0f, 1.0f}),
                60.0f, 60.0f, 100.0f, 100.0f);

    // pivot이 무엇이든 크기는 그대로다.
    const EF::Rect sized = ResolveRect(parent, Anchor::Center, offset, size, {0.25f, 0.75f});
    EXPECT_NEAR(sized.Width(), 40.0f);
    EXPECT_NEAR(sized.Height(), 40.0f);
}

// pivot 생략형이 설계서 §4의 호출을 부모 안쪽에 앉힌다.
//     ui.BeginPanel("debug", Anchor::TopRight, {320, 200})
// Unity식 기본 pivot {0.5,0.5}였다면 이 패널은 우상단 밖으로 절반이 나간다.
TEST_CASE(UI, DefaultPivotKeepsPresetInsideParentCorner) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect screen{ {0.0f, 0.0f}, {1280.0f, 720.0f} };

    EXPECT_RECT(ResolveRect(screen, Anchor::TopRight, {0.0f, 0.0f}, {320.0f, 200.0f}),
                960.0f, 0.0f, 1280.0f, 200.0f);

    EXPECT_RECT(ResolveRect(screen, Anchor::TopLeft, {0.0f, 0.0f}, {320.0f, 200.0f}),
                0.0f, 0.0f, 320.0f, 200.0f);

    EXPECT_RECT(ResolveRect(screen, Anchor::BottomRight, {0.0f, 0.0f}, {320.0f, 200.0f}),
                960.0f, 520.0f, 1280.0f, 720.0f);

    // Center만은 자식 중앙이 화면 중앙에 온다.
    EXPECT_RECT(ResolveRect(screen, Anchor::Center, {0.0f, 0.0f}, {320.0f, 200.0f}),
                480.0f, 260.0f, 800.0f, 460.0f);
}

// StretchX: X는 부모 폭을 따라가고 offset.x가 좌우 여백이 된다.
// Y는 점 앵커이므로 size.y / pivot.y가 그대로 살아 있다 — 축별 독립 판정의 증거다.
TEST_CASE(UI, StretchXFillsParentWidthAndTreatsOffsetAsMargin) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {0.0f, 0.0f}, {400.0f, 300.0f} };

    // size.x에 말도 안 되는 값을 넣어 둔다. stretch 축이 size를 무시하는지 보기 위함이다.
    const EF::Rect panel = ResolveRect(parent, Anchor::StretchX, {20.0f, 0.0f}, {9999.0f, 50.0f});
    EXPECT_RECT(panel, 20.0f, 125.0f, 380.0f, 175.0f);
    EXPECT_NEAR(panel.Width(), 360.0f);   // 부모 폭 400 - 여백 20*2
    EXPECT_NEAR(panel.Height(), 50.0f);   // 점 앵커 축이라 size.y 그대로

    // 여백 0이면 부모 폭을 그대로 채운다.
    const EF::Rect full = ResolveRect(parent, Anchor::StretchX, {0.0f, 0.0f}, {0.0f, 50.0f});
    EXPECT_NEAR(full.min.x, 0.0f);
    EXPECT_NEAR(full.max.x, 400.0f);
}

// StretchY: 위 테스트의 축 반전.
TEST_CASE(UI, StretchYFillsParentHeightAndTreatsOffsetAsMargin) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {0.0f, 0.0f}, {400.0f, 300.0f} };

    const EF::Rect sidebar = ResolveRect(parent, Anchor::StretchY, {0.0f, 30.0f}, {60.0f, 9999.0f});
    EXPECT_RECT(sidebar, 170.0f, 30.0f, 230.0f, 270.0f);
    EXPECT_NEAR(sidebar.Height(), 240.0f);  // 부모 높이 300 - 여백 30*2
    EXPECT_NEAR(sidebar.Width(), 60.0f);    // 점 앵커 축이라 size.x 그대로
}

// StretchAll: 두 축 모두 stretch. offset이 사방 균일 안쪽 여백이 된다.
// 부모를 원점이 아닌 곳에 둬서 부모 offset 반영도 함께 본다.
TEST_CASE(UI, StretchAllFillsParentWithUniformInset) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect parent{ {10.0f, 20.0f}, {210.0f, 220.0f} };  // 200x200

    EXPECT_RECT(ResolveRect(parent, Anchor::StretchAll, {8.0f, 8.0f}, {0.0f, 0.0f}),
                18.0f, 28.0f, 202.0f, 212.0f);

    // 여백 0이면 부모와 정확히 같다.
    EXPECT_RECT(ResolveRect(parent, Anchor::StretchAll, {0.0f, 0.0f}, {0.0f, 0.0f}),
                10.0f, 20.0f, 210.0f, 220.0f);

    // 축마다 다른 여백.
    EXPECT_RECT(ResolveRect(parent, Anchor::StretchAll, {5.0f, 25.0f}, {0.0f, 0.0f}),
                15.0f, 45.0f, 205.0f, 195.0f);
}

// 간편형은 대칭 여백만 낼 수 있다. 비대칭이 필요하면 LayoutSpec을 직접 쓴다 —
// 상단 타이틀바 40px를 피해 나머지를 채우는 패널이 그 예다.
TEST_CASE(UI, LayoutSpecSupportsAsymmetricStretchMargins) {
    using EF::UI::Anchor;
    using EF::UI::LayoutSpec;
    using EF::UI::ResolveRect;
    using EF::UI::ToAnchorRect;

    const EF::Rect parent{ {0.0f, 0.0f}, {300.0f, 200.0f} };

    LayoutSpec spec;
    spec.anchor = ToAnchorRect(Anchor::StretchAll);
    spec.offsetMin = {0.0f, 40.0f};   // 상단 40px는 타이틀바 몫
    spec.offsetMax = {0.0f, 0.0f};    // 하단은 부모 끝까지

    EXPECT_RECT(ResolveRect(parent, spec), 0.0f, 40.0f, 300.0f, 200.0f);

    // 좌우도 서로 다르게.
    spec.offsetMin = {12.0f, 0.0f};
    spec.offsetMax = {60.0f, 0.0f};
    EXPECT_RECT(ResolveRect(parent, spec), 12.0f, 0.0f, 240.0f, 200.0f);
}

// 프리셋으로는 못 하는 배치를 AnchorRect로 직접 쓴다 —
// "프리셋은 표기 편의일 뿐"이라는 설계서 §2의 주장이 실제로 성립하는지 보는 테스트다.
// 하단에 붙은 전폭 로그 패널: X는 stretch, Y는 부모 아래쪽(1.0)의 점 앵커.
TEST_CASE(UI, CustomAnchorRectExpressesBottomAlignedFullWidthPanel) {
    using EF::UI::AnchorRect;
    using EF::UI::LayoutSpec;
    using EF::UI::ResolveRect;

    const EF::Rect screen{ {0.0f, 0.0f}, {800.0f, 600.0f} };

    LayoutSpec spec;
    spec.anchor = AnchorRect{ {0.0f, 1.0f}, {1.0f, 1.0f} };
    spec.offsetMin = {16.0f, -8.0f};  // x: 왼쪽 여백 / y: 바닥에서 8px 띄운다
    spec.offsetMax = {16.0f, 0.0f};   // x: 오른쪽 여백 / y: 점 앵커라 무시됨
    spec.size = {0.0f, 120.0f};       // x: stretch라 무시됨 / y: 패널 높이
    spec.pivot = {0.0f, 1.0f};        // y 기준점은 패널의 아래 모서리

    EXPECT_RECT(ResolveRect(screen, spec), 16.0f, 472.0f, 784.0f, 592.0f);
}

// 중첩: 부모 → 자식 → 손자가 누적된다.
// 결과 Rect를 그대로 다음 부모로 넘기는 것이 유지형 트리 한 패스 순회(설계서 §3)와
// 즉시형 Begin/End 스택(§4)이 실제로 하는 일이다.
TEST_CASE(UI, NestedResolutionAccumulatesParentOffsets) {
    using EF::UI::Anchor;
    using EF::UI::ResolveRect;

    const EF::Rect root{ {0.0f, 0.0f}, {1000.0f, 800.0f} };

    // 자식: 사방 여백을 다르게 준 stretch 패널.
    const EF::Rect child = ResolveRect(root, Anchor::StretchAll, {100.0f, 50.0f}, {0.0f, 0.0f});
    EXPECT_RECT(child, 100.0f, 50.0f, 900.0f, 750.0f);  // 800x700

    // 손자 A: 자식의 좌상단 기준. 루트가 아니라 자식의 min이 기준이어야 한다.
    const EF::Rect grandTopLeft = ResolveRect(child, Anchor::TopLeft, {10.0f, 10.0f}, {20.0f, 20.0f}, {0.0f, 0.0f});
    EXPECT_RECT(grandTopLeft, 110.0f, 60.0f, 130.0f, 80.0f);

    // 손자 B: 자식의 중앙(500,400) 기준.
    const EF::Rect grandCenter = ResolveRect(child, Anchor::Center, {0.0f, 0.0f}, {40.0f, 40.0f}, {0.5f, 0.5f});
    EXPECT_RECT(grandCenter, 480.0f, 380.0f, 520.0f, 420.0f);

    // 손자 C: 자식 안에서 다시 stretch — 자식의 폭을 따라간다.
    const EF::Rect grandStretch = ResolveRect(child, Anchor::StretchX, {5.0f, 0.0f}, {0.0f, 30.0f});
    EXPECT_NEAR(grandStretch.min.x, 105.0f);
    EXPECT_NEAR(grandStretch.max.x, 895.0f);
    EXPECT_NEAR(grandStretch.Height(), 30.0f);

    // 증손자까지: 손자 B 안의 우하단.
    const EF::Rect great = ResolveRect(grandCenter, Anchor::BottomRight, {0.0f, 0.0f}, {8.0f, 8.0f});
    EXPECT_RECT(great, 512.0f, 412.0f, 520.0f, 420.0f);
}
