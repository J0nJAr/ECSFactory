#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "ECS/World.hpp"
#include "Render/Camera.hpp"
#include "Render/RenderSystem.hpp"
#include "Render/Renderable.hpp"
#include "TestHarness.hpp"

#include <string>
#include <vector>

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

// ---------------------------------------------------------------------------
// RenderSystem — ECS 순회에서 DrawItem이 나오는 부분.
//
// IRenderer와 IWindow가 인터페이스라서 GPU도 창도 없이 테스트할 수 있다.
// 이게 R0에서 백엔드를 인터페이스 뒤에 둔 값어치 중 하나다.
// ---------------------------------------------------------------------------
namespace {

    // 제출된 DrawItem을 기록만 하는 가짜 백엔드.
    class RecordingRenderer final : public EF::IRenderer {
    public:
        bool Initialize(EF::IWindow&) override { return true; }
        void Shutdown() override {}
        void Resize(uint32_t, uint32_t) override {}

        void BeginFrame(const EF::Color& clearColor) override {
            ++beginFrameCount;
            lastClearColor = clearColor;
        }
        void SetCamera(const EF::Mat4& viewProjection) override {
            lastViewProjection = viewProjection;
        }
        void Submit(const EF::RenderContext& context) override {
            submitted.assign(context.Items().begin(), context.Items().end());
        }
        void EndFrame() override { ++endFrameCount; }

        std::vector<EF::DrawItem> submitted;
        EF::Color lastClearColor{};
        EF::Mat4 lastViewProjection{};
        int beginFrameCount = 0;
        int endFrameCount = 0;
    };

    // 고정 크기만 돌려주는 가짜 창.
    class FixedSizeWindow final : public EF::IWindow {
    public:
        FixedSizeWindow(uint32_t w, uint32_t h) : m_width(w), m_height(h) {}

        bool Create(const char*, uint32_t, uint32_t) override { return true; }
        void Destroy() override {}
        bool PumpMessages() override { return true; }
        [[nodiscard]] void* NativeHandle() const override { return nullptr; }
        [[nodiscard]] uint32_t Width() const override { return m_width; }
        [[nodiscard]] uint32_t Height() const override { return m_height; }
        bool ConsumeResized() override { return false; }

        // 마지막으로 설정된 제목을 들고만 있는다. 실제 창이 없으므로 OS 호출이 없다.
        void SetTitle(const char* title) override {
            lastTitle = (title != nullptr) ? title : "";
        }

        std::string lastTitle;

    private:
        uint32_t m_width;
        uint32_t m_height;
    };

    EF::EntityId SpawnDrawable(EF::World& world, int x, int z, EF::Float2 size) {
        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(x, 0, z),
            .rotation = EF::Quaternion::Identity()
        });
        world.AddComponent<EF::Renderable>(entity, EF::Renderable{
            .color = EF::Color::White(),
            .size = size
        });
        return entity;
    }

}

// Transform + Renderable을 가진 엔티티가 DrawItem으로 나온다.
// 위치는 Fixed64에서 float으로 넘어오고, 축 대응이 유지되어야 한다.
TEST_CASE(Render, SystemTurnsEntitiesIntoDrawItems) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(800, 600);
    EF::Camera camera;

    SpawnDrawable(world, 3, -7, EF::Float2{ 2.0f, 5.0f });

    auto& system = world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);
    static_cast<void>(system);
    world.FrameUpdate(0.016f);

    EXPECT_EQ(renderer.submitted.size(), static_cast<std::size_t>(1));
    EXPECT_NEAR(renderer.submitted[0].position.x, 3.0f);
    EXPECT_NEAR(renderer.submitted[0].position.z, -7.0f);
    EXPECT_NEAR(renderer.submitted[0].size.x, 2.0f);
    EXPECT_NEAR(renderer.submitted[0].size.y, 5.0f);
}

// Renderable이 없는 엔티티는 그리지 않는다.
// 서버가 만든 엔티티에 표현 컴포넌트가 아직 안 붙은 상태가 실제로 존재한다.
TEST_CASE(Render, SystemSkipsEntitiesWithoutRenderable) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(800, 600);
    EF::Camera camera;

    SpawnDrawable(world, 0, 0, EF::Float2{ 1.0f, 1.0f });

    // Transform만 있고 Renderable이 없는 엔티티
    const EF::EntityId bare = world.CreateEntity();
    world.AddComponent<EF::Transform>(bare, EF::Transform{});

    world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);
    world.FrameUpdate(0.016f);

    EXPECT_EQ(renderer.submitted.size(), static_cast<std::size_t>(1));
}

// 렌더 시스템은 월드를 읽기만 한다. 시뮬레이션 상태를 바꾸면 결정론이 깨진다.
// 프레임을 여러 번 돌려도 월드 해시가 그대로여야 한다.
TEST_CASE(Render, SystemDoesNotMutateSimulationState) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(800, 600);
    EF::Camera camera;

    SpawnDrawable(world, 5, 5, EF::Float2{ 1.0f, 1.0f });
    world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);

    const uint64_t before = world.ComputeHash();
    world.FrameUpdate(0.016f);
    world.FrameUpdate(0.016f);
    world.FrameUpdate(0.016f);
    const uint64_t after = world.ComputeHash();

    EXPECT_EQ(before, after);
}

// 시뮬레이션 박자에서는 아무것도 그리지 않는다.
// FixedUpdate에서 그리면 프레임률이 시뮬레이션 속도를 바꾸게 된다.
TEST_CASE(Render, SystemDrawsOnFrameUpdateNotFixedUpdate) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(800, 600);
    EF::Camera camera;

    SpawnDrawable(world, 0, 0, EF::Float2{ 1.0f, 1.0f });
    world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);

    world.FixedUpdate(EF::Fixed64::FromInt(0), 1);
    EXPECT_EQ(renderer.beginFrameCount, 0);

    world.FrameUpdate(0.016f);
    EXPECT_EQ(renderer.beginFrameCount, 1);
}

// Present는 시스템이 하지 않는다. 앱이 프레임을 닫는다.
// 이게 지켜져야 나중에 UI 패스가 월드 위에 낄 자리가 생긴다.
TEST_CASE(Render, SystemDoesNotPresent) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(800, 600);
    EF::Camera camera;

    world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);
    world.FrameUpdate(0.016f);

    EXPECT_EQ(renderer.endFrameCount, 0);
}

// 창 크기에서 aspect를 읽어 카메라 행렬을 만든다.
// 높이가 0(최소화)이면 항등으로 물러나 NaN 프레임을 막는다.
TEST_CASE(Render, SystemBuildsCameraMatrixFromWindowAspect) {
    EF::World world;
    RecordingRenderer renderer;
    FixedSizeWindow window(1000, 500);   // aspect 2.0
    EF::Camera camera;
    camera.viewHeightWorldUnits = 20.0f; // halfHeight 10 → halfWidth 20

    world.RegisterSystem<EF::RenderSystem>(renderer, camera, window);
    world.FrameUpdate(0.016f);

    EXPECT_NDC(EF::ProjectPoint(renderer.lastViewProjection, EF::Float3{ 20.0f, 0.0f, 0.0f }), 1.0f, 0.0f);
    EXPECT_NDC(EF::ProjectPoint(renderer.lastViewProjection, EF::Float3{ 0.0f, 0.0f, 10.0f }), 0.0f, 1.0f);
}
