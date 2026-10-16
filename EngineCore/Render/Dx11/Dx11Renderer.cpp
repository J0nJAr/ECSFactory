#include "Render/Dx11/Dx11Renderer.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

// UNICODE: Win32Window와 같은 이유로 W계열 API를 쓴다.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

// NOMINMAX: windows.h가 min/max를 매크로로 정의하면 std::min/std::max가 깨진다.
// 이 파일 안에서만 막으면 되므로 헤더가 아니라 여기서 정의한다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

#include "Base/Logger.hpp"
#include "Render/Dx11/ComPtr.hpp"

namespace EF {
    namespace {
        using Dx11::ComPtr;

        // 정점 하나. 설계서 §5.3의 v1 포맷 그대로 — 위치 + 색.
        // 텍스처가 들어오면 u,v가 붙는다.
        struct Vertex {
            float x, y, z;
            float r, g, b, a;
        };

        // 한 프레임에 담을 수 있는 최대 쿼드 수. 넘으면 그 프레임은 잘라서 그린다.
        //
        // 왜 고정 크기인가: Dynamic 버퍼를 매 프레임 재생성하면 드라이버가 매번
        // 할당·해제를 하게 된다. 넉넉히 한 번 잡아두고 Map/Unmap으로 덮어쓰는 것이
        // DX11의 표준 패턴이다. 부족해지면 이 상수를 올리거나 버퍼를 키우는 로직을 넣는다.
        constexpr std::size_t kMaxQuadsPerFrame = 4096;
        constexpr std::size_t kVerticesPerQuad = 6;   // 삼각형 2개
        constexpr std::size_t kMaxVertices = kMaxQuadsPerFrame * kVerticesPerQuad;

        // 셰이더는 런타임에 컴파일한다(D3DCompile). 빌드 의존이 0이고,
        // 나중에 사전 컴파일로 바꾸고 싶으면 이 문자열만 파일로 옮기면 된다.
        //
        // row_major: C++ 쪽 Mat4를 행 우선으로 저장하기 때문이다(RenderTypes.hpp 주석 참조).
        // 이걸 안 붙이면 HLSL이 열 우선으로 읽어 행렬이 전치된 채로 곱해진다.
        constexpr const char* kShaderSource = R"(
cbuffer CameraBuffer : register(b0)
{
    row_major float4x4 uViewProj;
};

struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };

VSOut VSMain(VSIn input)
{
    VSOut output;
    output.pos = mul(float4(input.pos, 1.0f), uViewProj);
    output.col = input.col;
    return output;
}

float4 PSMain(VSOut input) : SV_TARGET
{
    return input.col;
}
)";

        // HLSL 한 덩어리를 컴파일한다. 실패하면 컴파일러가 준 메시지를 그대로 로그에 남긴다.
        // (셰이더 오류는 메시지를 안 보면 원인을 알 방법이 없다.)
        bool CompileShader(const char* entryPoint, const char* target, ComPtr<ID3DBlob>& outBlob) {
            ComPtr<ID3DBlob> errors;
            UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifndef NDEBUG
            flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            const HRESULT hr = D3DCompile(
                kShaderSource, std::strlen(kShaderSource),
                nullptr, nullptr, nullptr,
                entryPoint, target, flags, 0,
                outBlob.PutRef(), errors.PutRef());

            if (FAILED(hr)) {
                const char* message = errors ? static_cast<const char*>(errors->GetBufferPointer())
                                             : "(컴파일러 메시지 없음)";
                LOG_ERROR("Dx11Renderer: 셰이더 컴파일 실패 ({}): {}", entryPoint, message);
                return false;
            }
            return true;
        }
    }

    struct Dx11Renderer::Impl {
        IWindow* window = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool initialized = false;

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> swapChain;
        ComPtr<ID3D11RenderTargetView> backBufferView;

        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11Buffer> vertexBuffer;
        ComPtr<ID3D11Buffer> cameraBuffer;
        ComPtr<ID3D11BlendState> blendState;
        ComPtr<ID3D11RasterizerState> rasterizerState;

        Mat4 viewProjection = Mat4::Identity();

        // 매 프레임 재사용하는 CPU 정점 버퍼. capacity를 유지해 재할당을 피한다.
        std::vector<Vertex> vertices;

        // 백버퍼에서 RTV를 만들고 뷰포트를 맞춘다. 초기화와 리사이즈가 공유한다.
        bool CreateBackBufferView() {
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.PutVoid());
            if (FAILED(hr)) {
                LOG_ERROR("Dx11Renderer: 백버퍼를 가져오지 못했다 (hr=0x{:08X}).", static_cast<uint32_t>(hr));
                return false;
            }

            hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, backBufferView.PutRef());
            if (FAILED(hr)) {
                LOG_ERROR("Dx11Renderer: RTV 생성 실패 (hr=0x{:08X}).", static_cast<uint32_t>(hr));
                return false;
            }

            D3D11_VIEWPORT viewport = {};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            return true;
        }
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

        auto* hwnd = static_cast<HWND>(window.NativeHandle());
        if (hwnd == nullptr) {
            LOG_ERROR("Dx11Renderer: 창 핸들이 없다. IWindow::Create를 먼저 불러야 한다.");
            return false;
        }

        // ── 1. Device + SwapChain ────────────────────────────────────────────
        DXGI_SWAP_CHAIN_DESC swapDesc = {};
        swapDesc.BufferCount = 2;
        swapDesc.BufferDesc.Width = m_impl->width;
        swapDesc.BufferDesc.Height = m_impl->height;
        swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.OutputWindow = hwnd;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.Windowed = TRUE;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        UINT deviceFlags = 0;
#ifndef NDEBUG
        // 디버그 레이어는 SDK가 설치된 기계에서만 있다. 없으면 아래에서 재시도한다.
        deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

        // HARDWARE → WARP 순으로 시도한다.
        // WARP는 소프트웨어 래스터라이저다. GPU가 없는 CI 러너나 원격 데스크톱에서도
        // 렌더 경로 자체는 돌아가야 하므로 폴백을 둔다(느릴 뿐 결과는 같다).
        const D3D_DRIVER_TYPE driverTypes[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };

        HRESULT hr = E_FAIL;
        for (D3D_DRIVER_TYPE driverType : driverTypes) {
            hr = D3D11CreateDeviceAndSwapChain(
                nullptr, driverType, nullptr, deviceFlags,
                wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
                &swapDesc, m_impl->swapChain.PutRef(), m_impl->device.PutRef(),
                &obtained, m_impl->context.PutRef());

            if (SUCCEEDED(hr)) {
                LOG_INFO("Dx11Renderer: device 생성 ({}).",
                         driverType == D3D_DRIVER_TYPE_HARDWARE ? "하드웨어" : "WARP 소프트웨어");
                break;
            }

            // 디버그 레이어가 없어서 실패했을 수 있다. 플래그를 빼고 같은 드라이버로 한 번 더.
            if (deviceFlags != 0) {
                deviceFlags = 0;
                hr = D3D11CreateDeviceAndSwapChain(
                    nullptr, driverType, nullptr, 0,
                    wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
                    &swapDesc, m_impl->swapChain.PutRef(), m_impl->device.PutRef(),
                    &obtained, m_impl->context.PutRef());
                if (SUCCEEDED(hr)) {
                    LOG_INFO("Dx11Renderer: device 생성 (디버그 레이어 없이).");
                    break;
                }
            }
        }

        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: device/swapchain 생성 실패 (hr=0x{:08X}).", static_cast<uint32_t>(hr));
            return false;
        }

        // ── 2. 백버퍼 RTV + 뷰포트 ───────────────────────────────────────────
        if (!m_impl->CreateBackBufferView()) {
            return false;
        }

        // ── 3. 셰이더 + 입력 레이아웃 ────────────────────────────────────────
        ComPtr<ID3DBlob> vsBlob;
        ComPtr<ID3DBlob> psBlob;
        if (!CompileShader("VSMain", "vs_4_0", vsBlob) || !CompileShader("PSMain", "ps_4_0", psBlob)) {
            return false;
        }

        hr = m_impl->device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                                nullptr, m_impl->vertexShader.PutRef());
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: 정점 셰이더 생성 실패.");
            return false;
        }

        hr = m_impl->device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                               nullptr, m_impl->pixelShader.PutRef());
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: 픽셀 셰이더 생성 실패.");
            return false;
        }

        const D3D11_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };
        hr = m_impl->device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)),
                                               vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                               m_impl->inputLayout.PutRef());
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: 입력 레이아웃 생성 실패.");
            return false;
        }

        // ── 4. Dynamic 정점 버퍼 + 카메라 상수 버퍼 ──────────────────────────
        D3D11_BUFFER_DESC vbDesc = {};
        vbDesc.ByteWidth = static_cast<UINT>(sizeof(Vertex) * kMaxVertices);
        vbDesc.Usage = D3D11_USAGE_DYNAMIC;
        vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_impl->device->CreateBuffer(&vbDesc, nullptr, m_impl->vertexBuffer.PutRef());
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: 정점 버퍼 생성 실패.");
            return false;
        }

        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.ByteWidth = sizeof(Mat4);   // 64바이트 = 16의 배수라 그대로 맞는다
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        hr = m_impl->device->CreateBuffer(&cbDesc, nullptr, m_impl->cameraBuffer.PutRef());
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: 상수 버퍼 생성 실패.");
            return false;
        }

        // ── 5. 파이프라인 상태 ───────────────────────────────────────────────
        // 알파 블렌딩. UI와 반투명 쿼드를 위해 켜 둔다.
        D3D11_BLEND_DESC blendDesc = {};
        blendDesc.RenderTarget[0].BlendEnable = TRUE;
        blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_impl->device->CreateBlendState(&blendDesc, m_impl->blendState.PutRef());

        // 컬링을 끈다. 2D 쿼드는 정점 감김 방향을 신경 쓸 이유가 없고,
        // 켜 두면 "왜 안 보이지"의 원인이 하나 더 늘어난다.
        D3D11_RASTERIZER_DESC rasterDesc = {};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        m_impl->device->CreateRasterizerState(&rasterDesc, m_impl->rasterizerState.PutRef());

        m_impl->vertices.reserve(kMaxVertices);
        m_impl->initialized = true;
        LOG_INFO("Dx11Renderer: 초기화 완료 ({}x{}).", m_impl->width, m_impl->height);
        return true;
    }

    void Dx11Renderer::Shutdown() {
        if (!m_impl->initialized) {
            return;
        }

        // 스왑체인은 전체화면 상태로 파괴하면 안 된다(DXGI 규칙).
        // 지금은 항상 windowed지만, 나중에 전체화면을 넣어도 여기서 걸리지 않게 미리 내린다.
        if (m_impl->swapChain) {
            m_impl->swapChain->SetFullscreenState(FALSE, nullptr);
        }

        // ComPtr이 알아서 Release하지만, context에 묶인 상태를 먼저 푼다.
        // 안 그러면 디버그 레이어가 "살아 있는 참조" 경고를 낸다.
        if (m_impl->context) {
            m_impl->context->ClearState();
            m_impl->context->Flush();
        }

        m_impl->rasterizerState.Reset();
        m_impl->blendState.Reset();
        m_impl->cameraBuffer.Reset();
        m_impl->vertexBuffer.Reset();
        m_impl->inputLayout.Reset();
        m_impl->pixelShader.Reset();
        m_impl->vertexShader.Reset();
        m_impl->backBufferView.Reset();
        m_impl->swapChain.Reset();
        m_impl->context.Reset();
        m_impl->device.Reset();

        m_impl->initialized = false;
        m_impl->window = nullptr;
        LOG_INFO("Dx11Renderer: 종료.");
    }

    void Dx11Renderer::Resize(uint32_t width, uint32_t height) {
        // 최소화하면 0x0이 들어온다. 그 상태로 ResizeBuffers를 부르면 실패한다.
        if (width == 0 || height == 0) {
            return;
        }
        if (width == m_impl->width && height == m_impl->height) {
            return;
        }

        m_impl->width = width;
        m_impl->height = height;

        if (!m_impl->initialized) {
            return;
        }

        // ⚠️ 순서가 중요하다. 백버퍼를 가리키는 뷰가 살아 있으면 ResizeBuffers가 실패한다.
        //    RTV를 먼저 놓고, 컨텍스트에 묶인 것도 풀어야 한다.
        m_impl->context->OMSetRenderTargets(0, nullptr, nullptr);
        m_impl->backBufferView.Reset();

        const HRESULT hr = m_impl->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            LOG_ERROR("Dx11Renderer: ResizeBuffers 실패 (hr=0x{:08X}).", static_cast<uint32_t>(hr));
            return;
        }

        m_impl->CreateBackBufferView();
    }

    void Dx11Renderer::BeginFrame(const Color& clearColor) {
        if (!m_impl->initialized || !m_impl->backBufferView) {
            return;
        }

        ID3D11RenderTargetView* rtv = m_impl->backBufferView.Get();
        m_impl->context->OMSetRenderTargets(1, &rtv, nullptr);

        const float rgba[4] = { clearColor.r, clearColor.g, clearColor.b, clearColor.a };
        m_impl->context->ClearRenderTargetView(rtv, rgba);
    }

    void Dx11Renderer::SetCamera(const Mat4& viewProjection) {
        m_impl->viewProjection = viewProjection;

        if (!m_impl->initialized) {
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(m_impl->context->Map(m_impl->cameraBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, viewProjection.m, sizeof(viewProjection.m));
            m_impl->context->Unmap(m_impl->cameraBuffer.Get(), 0);
        }
    }

    void Dx11Renderer::Submit(const RenderContext& context) {
        if (!m_impl->initialized || context.Items().empty()) {
            return;
        }

        // ── DrawItem → 정점 6개 ──────────────────────────────────────────────
        // 쿼드 하나를 삼각형 2개로 편다. 인덱스 버퍼를 안 쓰는 이유:
        // 정점이 6개뿐이라 인덱스로 아끼는 양(2정점)보다 버퍼를 하나 더 관리하는 비용이 크다.
        // 텍스처 아틀라스와 배칭이 본격화되면 그때 인덱스를 넣는 게 맞다.
        m_impl->vertices.clear();

        std::size_t drawn = 0;
        for (const DrawItem& item : context.Items()) {
            if (drawn >= kMaxQuadsPerFrame) {
                LOG_WARN("Dx11Renderer: 한 프레임 쿼드 한도({})를 넘겨 나머지를 건너뛴다.", kMaxQuadsPerFrame);
                break;
            }
            ++drawn;

            const float hw = item.size.x * 0.5f;
            const float hh = item.size.y * 0.5f;
            const float cosR = std::cos(item.rotation);
            const float sinR = std::sin(item.rotation);

            // 중심 기준 네 모서리를 회전시켜 월드 위치에 더한다.
            //
            // ⚠️ 쿼드는 **XZ 평면(지면)에 눕는다.** 크기 오프셋이 x와 z로 가고 y는 그대로다.
            //    탑다운 카메라가 월드 XZ를 화면에 투영하고 Y(높이축)를 버리기 때문에
            //    (Render/Camera.hpp), 쿼드를 XY에 펴면 화면에서 선으로 찌그러진다.
            //    회전은 그래서 Y축(수직축) 기준이다.
            auto corner = [&](float ox, float oz) {
                Vertex v;
                v.x = item.position.x + (ox * cosR - oz * sinR);
                v.y = item.position.y;
                v.z = item.position.z + (ox * sinR + oz * cosR);
                v.r = item.color.r;
                v.g = item.color.g;
                v.b = item.color.b;
                v.a = item.color.a;
                return v;
            };

            // 이름의 top/bottom은 **화면 기준**이다. +Z가 화면 위로 가므로 +hh가 top이다.
            const Vertex topLeft     = corner(-hw,  hh);
            const Vertex topRight    = corner( hw,  hh);
            const Vertex bottomLeft  = corner(-hw, -hh);
            const Vertex bottomRight = corner( hw, -hh);

            m_impl->vertices.push_back(topLeft);
            m_impl->vertices.push_back(topRight);
            m_impl->vertices.push_back(bottomRight);

            m_impl->vertices.push_back(topLeft);
            m_impl->vertices.push_back(bottomRight);
            m_impl->vertices.push_back(bottomLeft);
        }

        if (m_impl->vertices.empty()) {
            return;
        }

        // ── CPU 배열 → Dynamic VB ────────────────────────────────────────────
        // WRITE_DISCARD: "이전 내용은 버려도 된다"는 약속이라, 드라이버가 GPU가 아직
        // 쓰고 있는 버퍼를 기다리지 않고 새 메모리를 준다. 매 프레임 통째로 덮어쓰는
        // 우리 패턴에 맞는 유일한 선택이다.
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_impl->context->Map(m_impl->vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            LOG_ERROR("Dx11Renderer: 정점 버퍼 Map 실패.");
            return;
        }
        std::memcpy(mapped.pData, m_impl->vertices.data(), sizeof(Vertex) * m_impl->vertices.size());
        m_impl->context->Unmap(m_impl->vertexBuffer.Get(), 0);

        // ── 파이프라인 설정 후 Draw 한 번 ────────────────────────────────────
        const UINT stride = sizeof(Vertex);
        const UINT offset = 0;
        ID3D11Buffer* vb = m_impl->vertexBuffer.Get();
        ID3D11Buffer* cb = m_impl->cameraBuffer.Get();

        m_impl->context->IASetInputLayout(m_impl->inputLayout.Get());
        m_impl->context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
        m_impl->context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        m_impl->context->VSSetShader(m_impl->vertexShader.Get(), nullptr, 0);
        m_impl->context->VSSetConstantBuffers(0, 1, &cb);
        m_impl->context->PSSetShader(m_impl->pixelShader.Get(), nullptr, 0);
        m_impl->context->RSSetState(m_impl->rasterizerState.Get());

        const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        m_impl->context->OMSetBlendState(m_impl->blendState.Get(), blendFactor, 0xFFFFFFFF);

        m_impl->context->Draw(static_cast<UINT>(m_impl->vertices.size()), 0);
    }

    bool Dx11Renderer::CaptureBackBuffer(const char* path) {
        if (!m_impl->initialized) {
            return false;
        }

        ComPtr<ID3D11Texture2D> backBuffer;
        if (FAILED(m_impl->swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), backBuffer.PutVoid()))) {
            LOG_ERROR("CaptureBackBuffer: 백버퍼를 가져오지 못했다.");
            return false;
        }

        // GPU 메모리는 CPU가 직접 못 읽는다. CPU_ACCESS_READ가 붙은 STAGING 텍스처로
        // 복사한 다음에야 Map 할 수 있다. DX11에서 읽기의 표준 경로다.
        D3D11_TEXTURE2D_DESC desc = {};
        backBuffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(m_impl->device->CreateTexture2D(&desc, nullptr, staging.PutRef()))) {
            LOG_ERROR("CaptureBackBuffer: staging 텍스처 생성 실패.");
            return false;
        }

        m_impl->context->CopyResource(staging.Get(), backBuffer.Get());

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (FAILED(m_impl->context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            LOG_ERROR("CaptureBackBuffer: staging Map 실패.");
            return false;
        }

        std::FILE* file = std::fopen(path, "wb");
        if (file == nullptr) {
            m_impl->context->Unmap(staging.Get(), 0);
            LOG_ERROR("CaptureBackBuffer: 파일을 열지 못했다: {}", path);
            return false;
        }

        // PPM(P6) 헤더: 매직, 가로 세로, 최대값. 그 뒤로 RGB 바이트가 그대로 이어진다.
        std::fprintf(file, "P6\n%u %u\n255\n", desc.Width, desc.Height);

        // RowPitch는 가로 픽셀 수 x 4 보다 클 수 있다(정렬 패딩). 줄마다 다시 계산해야 한다.
        const auto* base = static_cast<const uint8_t*>(mapped.pData);
        std::vector<uint8_t> row(static_cast<std::size_t>(desc.Width) * 3);
        for (uint32_t y = 0; y < desc.Height; ++y) {
            const uint8_t* src = base + static_cast<std::size_t>(y) * mapped.RowPitch;
            for (uint32_t x = 0; x < desc.Width; ++x) {
                // 백버퍼는 R8G8B8A8_UNORM이라 바이트 순서가 이미 R,G,B,A다.
                row[x * 3 + 0] = src[x * 4 + 0];
                row[x * 3 + 1] = src[x * 4 + 1];
                row[x * 3 + 2] = src[x * 4 + 2];
            }
            std::fwrite(row.data(), 1, row.size(), file);
        }

        std::fclose(file);
        m_impl->context->Unmap(staging.Get(), 0);
        LOG_INFO("CaptureBackBuffer: {} ({}x{}) 저장.", path, desc.Width, desc.Height);
        return true;
    }

    void Dx11Renderer::EndFrame() {
        if (!m_impl->initialized) {
            return;
        }

        // 첫 인자 1 = vsync 켬(모니터 주사율에 맞춰 대기).
        // 끄면(0) 루프가 수천 fps로 돌며 CPU를 태운다. 시뮬레이션 속도는 어차피
        // 엔진의 고정 timestep 누산기가 잡으므로, 프레임을 더 뽑아도 얻는 게 없다.
        m_impl->swapChain->Present(1, 0);
    }

}
