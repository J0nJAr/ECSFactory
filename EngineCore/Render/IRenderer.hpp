#pragma once

#include <cstdint>

#include "Platform/IWindow.hpp"
#include "Render/RenderContext.hpp"
#include "Render/RenderTypes.hpp"

namespace EF {

    // 렌더 백엔드 추상화. 엔진 코어는 DX11을 모른다.
    //
    // 한 프레임의 순서는 이렇게 고정된다:
    //     BeginFrame(clearColor) → SetCamera(viewProj) → Submit(context) → EndFrame()
    // Submit은 여러 번 호출될 수 있다(UI 패스와 월드 패스를 나눌 때).
    //
    // DX11은 첫 구현일 뿐이다. 이 인터페이스가 값 타입(Mat4, RenderContext)만 주고받는
    // 이유가 그것이다 — 백엔드가 바뀌어도 위쪽 코드가 안 바뀐다.
    // (docs/design/rendering-dx11.md §0 원칙 4)
    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        virtual bool Initialize(IWindow& window) = 0;
        virtual void Shutdown() = 0;

        virtual void Resize(uint32_t width, uint32_t height) = 0;

        virtual void BeginFrame(const Color& clearColor) = 0;
        virtual void SetCamera(const Mat4& viewProjection) = 0;
        virtual void Submit(const RenderContext& context) = 0;
        virtual void EndFrame() = 0;
    };

}
