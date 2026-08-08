#pragma once

#include <memory>

#include "Render/IRenderer.hpp"

namespace EF {

    // IRenderer의 DX11 구현.
    //
    // ⚠️ 이 헤더는 <d3d11.h>를 포함하지 않는다. 일부러 그렇게 했다.
    //    d3d11.h는 windows.h를 끌고 오고, windows.h는 min/max 매크로 같은 지뢰를
    //    포함하는 모든 번역 단위에 뿌린다. 그래서 DX11 타입은 전부 .cpp 안의 Impl에 가둔다
    //    (PIMPL). 이 헤더를 include하는 쪽은 Windows 헤더를 보지 않는다.
    //
    // COM 포인터는 Microsoft::WRL::ComPtr 대신 raw 포인터 + 수동 Release를 쓴다.
    // MinGW(msys2 ucrt64)에서 WRL은 기대대로 잡히지 않는 경우가 있다.
    // (docs/design/rendering-dx11.md §6)
    //
    // 현재 상태: R0 — 스텁이다. 실제 device/swapchain 생성은 R2에서 들어온다.
    class Dx11Renderer final : public IRenderer {
    public:
        Dx11Renderer();
        ~Dx11Renderer() override;

        Dx11Renderer(const Dx11Renderer&) = delete;
        Dx11Renderer& operator=(const Dx11Renderer&) = delete;

        bool Initialize(IWindow& window) override;
        void Shutdown() override;

        void Resize(uint32_t width, uint32_t height) override;

        void BeginFrame(const Color& clearColor) override;
        void SetCamera(const Mat4& viewProjection) override;
        void Submit(const RenderContext& context) override;
        void EndFrame() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

}
