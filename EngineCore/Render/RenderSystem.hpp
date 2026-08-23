#pragma once

#include "ECS/ISystem.hpp"
#include "Render/Camera.hpp"
#include "Render/IRenderer.hpp"
#include "Render/RenderContext.hpp"

namespace EF {

    // ECS를 순회해서 이번 프레임에 그릴 것을 모으고 백엔드에 넘기는 시스템.
    //
    // 책임은 하나다: `View<Transform, Renderable>` → `DrawItem` 목록 → `IRenderer`.
    // 그리는 방법(셰이더·버퍼·Present)은 전혀 모른다.
    //
    // ── 왜 FrameUpdate인가 ───────────────────────────────────────────────────
    // 렌더링은 `FixedUpdate`가 아니라 `FrameUpdate`에 산다. 시뮬레이션은 50Hz 고정이고
    // 화면은 모니터 주사율로 도는데, 둘을 묶으면 프레임률이 시뮬레이션 속도를 바꿔버린다.
    // 이 시스템은 상태를 **읽기만** 하므로 몇 번 돌든 시뮬레이션에 영향이 없다.
    //
    // ── 결정론 경계 ──────────────────────────────────────────────────────────
    // 여기가 `Fixed64` → float 변환이 일어나는 자리다. 그리고 그 방향은 한쪽뿐이다.
    // 변환 결과는 절대 시뮬레이션으로 돌아가지 않는다(돌아가면 클라마다 값이 갈린다).
    // 이 시스템이 월드에 쓰기를 하지 않는 것이 그 보장의 실질적인 내용이다.
    //
    // ── 이 타입이 하지 않는 일 ───────────────────────────────────────────────
    //  - `Renderable`을 붙이지 않는다. 무엇이 어떤 모습이어야 하는지는 게임의 판단이고,
    //    렌더 계층이 정할 일이 아니다. 붙이는 쪽은 앱이다(GameClient의 EnsurePlayerVisuals).
    //  - 보간하지 않는다. 50Hz 시뮬을 그대로 그리므로 빠르게 움직이면 끊겨 보인다.
    //    `PreviousTransform` + alpha 보간은 R7이다.
    //  - 컬링·정렬을 하지 않는다. 지금은 보이는 것이 몇 개뿐이라 전부 제출한다.
    //    넣을 자리는 `RenderContext`를 채운 직후다.
    //
    // ⚠️ 소유하지 않는 참조를 든다. `IWindow`/`IRenderer`/`Camera`는 클라 앱의 루프
    //    스택 프레임에 살고, 이 시스템은 그보다 먼저 죽어야 한다.
    //    (설계서 §2는 이것들을 월드 리소스로 두자고 했는데, `AddResource<T>`가 T를 제자리
    //     생성하는 형태라 이미 스택에 있는 객체를 담으려면 포인터 래퍼가 한 겹 더 필요했다.
    //     생성자 주입이 수명 관계를 더 분명하게 드러내서 이쪽을 골랐다.)
    class RenderSystem final : public ISystem {
    public:
        RenderSystem(IRenderer& renderer, const Camera& camera, const IWindow& window);

        void Initialize(World& world) override;
        void FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) override;
        void FrameUpdate(World& world, float frameDeltaSeconds) override;
        void Shutdown(World& world) override;

        // 배경색. 프레임 시작에 화면을 이 색으로 지운다.
        //
        // 검정으로 두지 않는 이유: 검정이면 "지우기는 됐는데 아무것도 안 그려진 상태"와
        // "렌더 경로가 통째로 죽은 상태"가 화면에서 똑같아 보인다. 색이 하나 있으면
        // 그 색이 보이는 것만으로 device/swapchain/RTV/Present가 살아 있음이 확인된다.
        void SetClearColor(const Color& color) noexcept {
            m_clearColor = color;
        }

        // 마지막 프레임에 제출한 DrawItem 수. 디버그 표시용이다.
        [[nodiscard]] std::size_t LastSubmittedCount() const noexcept {
            return m_context.Items().size();
        }

    private:
        IRenderer& m_renderer;
        const Camera& m_camera;
        const IWindow& m_window;

        // 매 프레임 재사용한다. capacity가 유지되어 재할당이 없다.
        RenderContext m_context;

        Color m_clearColor{ 0.09f, 0.10f, 0.13f, 1.0f };
    };

}
