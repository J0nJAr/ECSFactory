#include "Render/Dx11/Dx11Renderer.hpp"

#include "Base/Logger.hpp"

namespace EF {

    // DX11 상태를 담을 자리.
    // R0에서는 비어 있다 — device / swapChain / renderTargetView 등은 R2에서 들어온다.
    // 지금 필드를 미리 선언해두지 않는 이유: 쓰지 않는 멤버는 설명할 것도 검증할 것도 없다.
    struct Dx11Renderer::Impl {
        IWindow* window = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool initialized = false;
    };

    Dx11Renderer::Dx11Renderer()
        : m_impl(std::make_unique<Impl>()) {
    }

    Dx11Renderer::~Dx11Renderer() {
        Shutdown();
    }

    bool Dx11Renderer::Initialize(IWindow& window) {
        m_impl->window = &window;
        m_impl->width = window.Width();
        m_impl->height = window.Height();

        // R2에서 여기에 D3D11CreateDeviceAndSwapChain이 들어온다.
        m_impl->initialized = true;

        Logger::Log(LogLevel::Info,
                    "Dx11Renderer: 스텁 초기화 ({}x{}). 실제 device 생성은 R2에서 들어온다.",
                    m_impl->width, m_impl->height);
        return true;
    }

    void Dx11Renderer::Shutdown() {
        if (!m_impl->initialized) {
            return;
        }

        m_impl->initialized = false;
        m_impl->window = nullptr;
    }

    void Dx11Renderer::Resize(uint32_t width, uint32_t height) {
        // 최소화하면 0x0이 들어온다. 그 상태로 swapchain을 재생성하면 실패하므로 무시한다.
        if (width == 0 || height == 0) {
            return;
        }

        m_impl->width = width;
        m_impl->height = height;
    }

    void Dx11Renderer::BeginFrame(const Color& clearColor) {
        (void)clearColor;
    }

    void Dx11Renderer::SetCamera(const Mat4& viewProjection) {
        (void)viewProjection;
    }

    void Dx11Renderer::Submit(const RenderContext& context) {
        (void)context;
    }

    void Dx11Renderer::EndFrame() {
    }

}
