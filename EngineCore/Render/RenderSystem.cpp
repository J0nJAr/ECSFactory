#include "Render/RenderSystem.hpp"

#include "Components/Transform.hpp"
#include "ECS/View.hpp"
#include "ECS/World.hpp"
#include "Render/Renderable.hpp"

namespace EF {

    RenderSystem::RenderSystem(IRenderer& renderer, const Camera& camera, const IWindow& window)
        : m_renderer(renderer)
        , m_camera(camera)
        , m_window(window) {
    }

    void RenderSystem::Initialize(World& world) {
        static_cast<void>(world);
    }

    // 시뮬레이션 박자에서는 아무것도 하지 않는다.
    // 렌더링이 FixedUpdate에 끼면 프레임률이 시뮬레이션 속도를 바꾼다(헤더 주석 참조).
    void RenderSystem::FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) {
        static_cast<void>(world);
        static_cast<void>(fixedDeltaTime);
        static_cast<void>(tick);
    }

    void RenderSystem::FrameUpdate(World& world, float frameDeltaSeconds) {
        static_cast<void>(frameDeltaSeconds);

        // aspect를 매 프레임 창에서 읽는다. 리사이즈 다음 프레임에 바로 반영된다.
        // 최소화되면 높이가 0으로 들어오는데, Camera::ViewProjection이 그때 항등 행렬로
        // 물러난다(0으로 나누면 행렬이 NaN이 되어 그 프레임이 통째로 사라진다).
        const uint32_t width = m_window.Width();
        const uint32_t height = m_window.Height();
        const float aspect = height > 0
            ? static_cast<float>(width) / static_cast<float>(height)
            : 0.0f;

        m_context.Clear();

        // 여기가 Fixed64 → float 경계다. 방향은 한쪽뿐이고, 결과는 시뮬레이션으로 돌아가지 않는다.
        auto view = world.CreateView<Transform, Renderable>();
        view.ForEach([&](Transform& transform, Renderable& renderable) {
            m_context.Add(DrawItem{
                .position = Float3{
                    transform.position.x.ToFloat(),
                    transform.position.y.ToFloat(),
                    transform.position.z.ToFloat()
                },
                .size = renderable.size,
                // 회전은 아직 넘기지 않는다. Quaternion에서 Y축 각도를 뽑는 코드가
                // 아직 없고, 지금 그리는 것들이 전부 정사각형이라 돌려도 차이가 없다.
                // 방향이 보이는 형태(벨트·기계)가 들어오면 그때 여기를 채운다.
                .rotation = 0.0f,
                .color = renderable.color
            });
        });

        m_renderer.BeginFrame(m_clearColor);
        m_renderer.SetCamera(m_camera.ViewProjection(aspect));
        m_renderer.Submit(m_context);

        // ⚠️ Present(EndFrame)는 여기서 부르지 않는다.
        //    한 프레임에 여러 패스를 그릴 수 있어야 하기 때문이다 — 월드를 그린 뒤
        //    UI 패스가 그 위에 올라간다(트랙 U). 이 시스템이 Present까지 해버리면
        //    UI가 낄 자리가 없어지고, 시스템 등록 순서가 곧 화면 순서가 되어버린다.
        //    프레임을 닫는 것은 루프를 소유한 앱의 몫이다.
    }

    void RenderSystem::Shutdown(World& world) {
        static_cast<void>(world);
    }

}
