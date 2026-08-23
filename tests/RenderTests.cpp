#include "Render/Camera.hpp"
#include "TestHarness.hpp"

#include <cmath>

// 렌더 계층은 결정론 대상이 아니라 float로 계산한다. 그래서 정확 비교 대신 허용오차를 쓴다.
// NDC는 -1..1 구간이므로 1e-5면 화면 픽셀 하나보다 훨씬 아래다.
namespace {
    constexpr float kTolerance = 1e-5f;
}

#define EXPECT_NEAR(actual, expected)                                                       \
    do {                                                                                    \
        const float actual_ = (actual);                                                     \
        const float expected_ = (expected);                                                 \
        if (!(std::fabs(actual_ - expected_) <= kTolerance)) {                              \
            ::Test::FailEqual(#actual, #expected, actual_, expected_, __FILE__, __LINE__);  \
        }                                                                                   \
    } while (false)

#define EXPECT_NDC(point, expectX, expectY)                                                 \
    do {                                                                                    \
        const EF::Float3 projected_ = (point);                                              \
        EXPECT_NEAR(projected_.x, (expectX));                                               \
        EXPECT_NEAR(projected_.y, (expectY));                                               \
    } while (false)

// 카메라가 보는 지점은 화면 한가운데(NDC 원점)로 간다.
// 제일 기본이라 이게 깨지면 나머지 테스트를 읽을 필요가 없다.
TEST_CASE(Render, CameraCenterMapsToNdcOrigin) {
    EF::Camera camera;
    camera.position = EF::Float2{ 12.0f, -7.0f };   // (월드 X, 월드 Z)
    camera.viewHeightWorldUnits = 60.0f;

    const EF::Mat4 viewProj = camera.ViewProjection(16.0f / 9.0f);

    // 월드 Y(높이)는 아무 값이나 넣어도 결과가 같아야 한다 — 2D 단계에서 버리는 축이다.
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 12.0f, 0.0f, -7.0f }), 0.0f, 0.0f);
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 12.0f, 999.0f, -7.0f }), 0.0f, 0.0f);
}

// 축 대응이 이 프로젝트에서 제일 헷갈리는 부분이라 방향까지 못 박는다.
// 월드 +X는 화면 오른쪽, 월드 +Z는 화면 **위쪽**이다.
TEST_CASE(Render, WorldXGoesRightAndWorldZGoesUp) {
    EF::Camera camera;
    camera.position = EF::Float2{ 0.0f, 0.0f };
    camera.viewHeightWorldUnits = 20.0f;   // halfHeight = 10

    const EF::Mat4 viewProj = camera.ViewProjection(1.0f);   // 정사각형이라 halfWidth도 10

    // 오른쪽 경계 / 왼쪽 경계
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 10.0f, 0.0f, 0.0f }),  1.0f, 0.0f);
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ -10.0f, 0.0f, 0.0f }), -1.0f, 0.0f);

    // 위쪽 경계 / 아래쪽 경계. +Z가 +Y(NDC)로 간다.
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 0.0f, 0.0f, 10.0f }),  0.0f, 1.0f);
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 0.0f, 0.0f, -10.0f }), 0.0f, -1.0f);
}

// aspect가 가로 방향에만 걸린다. 세로에 담기는 월드 길이는 창 비율과 무관하게 고정이다.
// 이게 뒤집히면 창을 옆으로 늘렸을 때 물체가 납작해진다.
TEST_CASE(Render, AspectStretchesHorizontalExtentOnly) {
    EF::Camera camera;
    camera.viewHeightWorldUnits = 20.0f;   // halfHeight = 10, 어떤 aspect에서도 그대로

    const EF::Mat4 wide = camera.ViewProjection(2.0f);   // halfWidth = 20

    // 세로: aspect와 무관하게 z=10이 위쪽 경계
    EXPECT_NDC(EF::ProjectPoint(wide, EF::Float3{ 0.0f, 0.0f, 10.0f }), 0.0f, 1.0f);

    // 가로: halfWidth가 20으로 늘었으므로 x=10은 경계의 절반 지점
    EXPECT_NDC(EF::ProjectPoint(wide, EF::Float3{ 10.0f, 0.0f, 0.0f }), 0.5f, 0.0f);
    EXPECT_NDC(EF::ProjectPoint(wide, EF::Float3{ 20.0f, 0.0f, 0.0f }), 1.0f, 0.0f);
}

// 카메라를 옮기면 화면에 보이는 창이 같이 옮겨간다(= 월드 점은 반대로 흐른다).
TEST_CASE(Render, MovingCameraShiftsWorldOppositeOnScreen) {
    EF::Camera camera;
    camera.viewHeightWorldUnits = 20.0f;
    camera.position = EF::Float2{ 5.0f, 0.0f };

    const EF::Mat4 viewProj = camera.ViewProjection(1.0f);

    // 카메라가 +X로 5 갔으니 원점은 화면에서 왼쪽으로 5/10 = 0.5 밀린다.
    EXPECT_NDC(EF::ProjectPoint(viewProj, EF::Float3{ 0.0f, 0.0f, 0.0f }), -0.5f, 0.0f);
}

// 깊이는 상수 0.5다. 깊이 버퍼가 없지만 DepthClipEnable이 켜져 있어서
// 클립 공간 z가 [0, w] 밖으로 나가면 그 도형이 통째로 잘린다.
// 이 값이 0이나 1로 밀리면 경계에서 사라지는 일이 생긴다.
TEST_CASE(Render, DepthIsConstantInsideClipRange) {
    EF::Camera camera;
    const EF::Mat4 viewProj = camera.ViewProjection(16.0f / 9.0f);

    EXPECT_NEAR(EF::ProjectPoint(viewProj, EF::Float3{ 0.0f, 0.0f, 0.0f }).z, 0.5f);
    EXPECT_NEAR(EF::ProjectPoint(viewProj, EF::Float3{ 100.0f, 50.0f, -30.0f }).z, 0.5f);
}

// 창이 최소화되면 크기가 0으로 들어온다. 그때 0으로 나누면 행렬이 NaN이 되고
// 그 프레임이 통째로 사라진다. 항등 행렬로 물러나는지 확인한다.
TEST_CASE(Render, DegenerateAspectFallsBackToIdentity) {
    EF::Camera camera;
    camera.position = EF::Float2{ 3.0f, 4.0f };

    const EF::Mat4 zeroAspect = camera.ViewProjection(0.0f);
    const EF::Float3 projected = EF::ProjectPoint(zeroAspect, EF::Float3{ 1.0f, 2.0f, 3.0f });

    // 항등이므로 입력이 그대로 나온다(NaN이 아니다).
    EXPECT_NEAR(projected.x, 1.0f);
    EXPECT_NEAR(projected.y, 2.0f);
    EXPECT_NEAR(projected.z, 3.0f);

    EF::Camera zeroHeight;
    zeroHeight.viewHeightWorldUnits = 0.0f;
    const EF::Float3 fromZeroHeight =
        EF::ProjectPoint(zeroHeight.ViewProjection(1.0f), EF::Float3{ 1.0f, 2.0f, 3.0f });
    EXPECT_NEAR(fromZeroHeight.x, 1.0f);
    EXPECT_NEAR(fromZeroHeight.z, 3.0f);
}
