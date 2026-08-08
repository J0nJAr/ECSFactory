# 렌더링 계층 설계서 — DX11 백엔드

> 목표: **결정론 시뮬레이션을 화면으로 본다.** 렌더링은 이 엔진의 *주제*가 아니라 *관측 장비*다.
> 그래서 "가장 빠르고 방해 적은 길"인 DX11을, `IRenderer` 추상화 뒤에 둔다.

---

## 0. 원칙 (절대 안 깨는 것)

1. **결정론 분리** — 게임플레이는 `FixedUpdate` + `Fixed64`만. 렌더링은 read-only 표현. `Fixed64 → float` 변환은 **렌더 경계에서만**, 시뮬로 절대 역류 금지.
2. **서버는 렌더하지 않는다** — 렌더링 컴포넌트/시스템/리소스는 **ClientPresentation 월드 전용**. ServerSimulation 월드엔 안 붙는다.
3. **렌더 데이터는 스냅샷 대상이 아니다** — `Renderable`/`Camera`/`PreviousTransform`은 `RegisterSnapshotComponent` 하지 않는다. 스냅샷은 순수 시뮬 데이터만.
4. **백엔드는 인터페이스 뒤에** — `IRenderer`. DX11은 첫 구현일 뿐. 엔진 코어는 DX11을 모른다.
5. **최소 의존** — DX11/DXGI/d3dcompiler는 Windows SDK 기본 포함. `ws2_32` 링크하듯 링크만 한다.

---

## 1. 전체 아키텍처 (레이어)

```mermaid
flowchart TB
    subgraph App["GameClientApplication (OnRun = 플랫폼 루프)"]
        Loop["while window.Pump():\nStepFixed → alpha → StepFrame"]
    end

    subgraph Platform["플랫폼 계층"]
        IWindow["IWindow"] --> Win32Window["Win32Window (HWND, 메시지 펌프)"]
    end

    subgraph Engine["엔진 코어 (윈도우/DX11 모름)"]
        GameEngine --> ClientWorld["World: ClientPresentation"]
        GameEngine --> ServerWorld["World: ServerSimulation\n(렌더 없음)"]
    end

    subgraph RenderLayer["렌더 계층 (ClientPresentation 전용)"]
        RenderSystem["RenderSystem : ISystem"]
        RenderContext["RenderContext\n(DrawItem 리스트)"]
        IRenderer["IRenderer"] --> Dx11["Dx11Renderer"]
        Camera["Camera (리소스)"]
    end

    Loop --> IWindow
    Loop --> GameEngine
    ClientWorld --> RenderSystem
    RenderSystem -->|View<Transform,Renderable>| RenderContext
    RenderSystem --> Camera
    RenderContext --> IRenderer
    Dx11 -->|Present| GPU["GPU / 화면"]
    Win32Window -.HWND.-> Dx11
```

리소스로 월드에 붙는 것(= `NetworkManager`와 같은 패턴): **`IWindow`, `IRenderer`, `Camera`, `FrameContext`**.

---

## 2. 모듈 설계 (인터페이스 & 컴포넌트)

```mermaid
classDiagram
    class IWindow {
        <<interface>>
        +Create(title,w,h) bool
        +PumpMessages() bool
        +NativeHandle() void*
        +Size() uint2
        +WasResized() bool
    }
    class Win32Window
    IWindow <|.. Win32Window

    class IRenderer {
        <<interface>>
        +Initialize(IWindow&) bool
        +Resize(w,h) void
        +BeginFrame(Color) void
        +SetCamera(Mat4 viewProj) void
        +Submit(RenderContext&) void
        +EndFrame() void
        +Shutdown() void
    }
    class Dx11Renderer
    IRenderer <|.. Dx11Renderer

    class RenderSystem {
        +Initialize(World&)
        +FrameUpdate(World&, float dt)
    }
    class ISystem
    ISystem <|.. RenderSystem

    class RenderContext {
        +vector~DrawItem~ items
        +Clear()
    }
    class DrawItem {
        +Float2 pos
        +Float2 size
        +float rotation
        +Color color
    }
    class Renderable {
        +Color color
        +Float2 size
    }
    class Camera {
        +Float2 position
        +float zoom
        +Mat4 ViewProj(aspect)
    }
    class PreviousTransform {
        +Vector3 position
        +Quaternion rotation
    }

    RenderSystem --> RenderContext
    RenderSystem --> IRenderer
    RenderSystem --> Camera
    RenderContext --> DrawItem
```

**컴포넌트 (전부 클라 전용, 스냅샷 비대상):**
- `Renderable` — 무엇을 그릴지(색·크기, 나중에 sprite/mesh 핸들).
- `PreviousTransform` — 직전 틱의 Transform(보간용). 시뮬이 아니라 표현용.

**리소스:**
- `Camera` — 위치/줌, view-projection 행렬 계산(float).
- `FrameContext` — 이번 프레임의 보간 alpha와 frame dt(엔진 ↔ RenderSystem 채널).

---

## 3. 렌더 루프 — 엔진 루프와의 통합

엔진은 이미 **고정틱(sim) + 프레임(render)** 으로 갈라져 있다(`ManualStepFrame` → `CallFixedUpdates` + `FrameUpdateUpdatingWorlds`). 렌더링은 `FrameUpdate`에 산다. 창 메시지 펌프는 **엔진 코어가 아니라 클라 앱**이 소유한다(서버는 창이 없으니까).

### 권장 엔진 seam (작게)

```cpp
// GameEngine에 추가 — ManualStepFrame을 두 단계로 분리
void StepFixed(float dt);   // 누적 + 고정틱 sim 실행
void StepFrame(float dt);   // 프레임 업데이트(렌더) 실행
[[nodiscard]] float GetInterpolationAlpha() const noexcept; // accumulated / fixedStep (0..1)
```

### 클라 앱의 플랫폼 루프 (`GameClientApplication::OnRun` 재정의)

```cpp
int OnRun(GameEngine& engine) override {
    Timer timer;
    while (m_window->PumpMessages()) {        // WM_QUIT이면 false
        const float dt = timer.Tick();
        Input::Update();

        engine.StepFixed(dt);                 // ① 시뮬 전진 (0..n 틱)
        m_frameContext->alpha = engine.GetInterpolationAlpha();  // ② 보간 비율 확정
        engine.StepFrame(dt);                 // ③ RenderSystem이 alpha로 렌더
    }
    return 0;
}
```

### RenderSystem (ClientPresentation 월드)

```cpp
void RenderSystem::FrameUpdate(World& world, float /*dt*/) {
    auto& r   = world.GetResource<IRenderer>();
    auto& cam = world.GetResource<Camera>();
    const float alpha = world.GetResource<FrameContext>().alpha;

    r.BeginFrame(kClearColor);
    r.SetCamera(cam.ViewProj(m_aspect));

    m_context.Clear();
    world.CreateView<Transform, PreviousTransform, Renderable>()
         .ForEach([&](const Transform& cur, const PreviousTransform& prev, const Renderable& rd) {
            // Fixed64 → float 변환은 "여기서만"
            const Float2 p = WorldToScreen(Lerp(prev.position, cur.position, alpha));
            m_context.items.push_back({ p, rd.size, /*rot*/0.0f, rd.color });
         });

    r.Submit(m_context);   // 배칭은 렌더러가
    r.EndFrame();          // Present
}
```

### 한 프레임의 흐름

```mermaid
sequenceDiagram
    participant App as Client OnRun
    participant Win as Win32Window
    participant Eng as GameEngine
    participant Sim as Sim Systems (FixedUpdate)
    participant RS as RenderSystem (FrameUpdate)
    participant DX as Dx11Renderer

    loop 매 프레임
        App->>Win: PumpMessages()  (WM_* 처리)
        App->>Eng: StepFixed(dt)
        loop accumulator >= 20ms
            Eng->>Sim: FixedUpdate(Fixed64 dt, tick)  // 결정론
        end
        App->>Eng: GetInterpolationAlpha()
        App->>Eng: StepFrame(dt)
        Eng->>RS: FrameUpdate(world, dt)
        RS->>DX: BeginFrame(clear)
        RS->>DX: SetCamera(viewProj)
        RS->>RS: View<Transform,Prev,Renderable> → 보간 → DrawItem
        RS->>DX: Submit(context)
        DX->>DX: Map VB → IA/VS/PS → Draw
        RS->>DX: EndFrame() → Present
    end
```

---

## 4. 고정틱 ↔ 프레임 보간

시뮬은 50Hz(20ms)인데 화면은 가변(60/144Hz…). 그냥 그리면 떨린다. **직전 틱(prev) ↔ 현재 틱(cur)** 사이를 `alpha`로 보간한다.

```
시뮬 틱:     ●────────●────────●────────●        (20ms 간격, 결정론)
             tick N   N+1      N+2      N+3
프레임:        ↑   ↑    ↑  ↑   ↑   ↑  ↑           (가변)
보간 alpha:    0.3 0.7  0.1 0.6 0.95 ...
렌더 위치 = lerp(prev_transform, cur_transform, alpha)

alpha = (남은 누적시간) / (고정 timestep)   ∈ [0, 1)
```

`PreviousTransform`은 **매 고정틱 끝에 `cur → prev`로 복사**하는 작은 시스템(`TransformHistorySystem`)이 채운다. 그래서 렌더는 항상 "한 틱 전 ~ 지금" 사이를 부드럽게 메운다(= 1틱 지연 대신 부드러움).

> **v1에선 보간을 생략**하고 `cur`만 그려도 된다(Phase 7에서 추가). 50Hz면 그럭저럭 보인다.

---

## 5. DX11 백엔드 내부 (`Dx11Renderer`)

### 5.1 객체 소유

```mermaid
flowchart TB
    Dx11Renderer --> Device["ID3D11Device"]
    Dx11Renderer --> Context["ID3D11DeviceContext"]
    Dx11Renderer --> Swap["IDXGISwapChain"]
    Dx11Renderer --> RTV["ID3D11RenderTargetView (백버퍼)"]
    Dx11Renderer --> VS["VertexShader + InputLayout"]
    Dx11Renderer --> PS["PixelShader"]
    Dx11Renderer --> VB["Dynamic VertexBuffer (배칭)"]
    Dx11Renderer --> CB["ConstantBuffer (ViewProj)"]
    Dx11Renderer --> States["Blend/Rasterizer/DepthState"]
```

### 5.2 초기화 (`Initialize(IWindow&)`)

1. `D3D11CreateDeviceAndSwapChain` — HWND, 포맷 `R8G8B8A8_UNORM`, windowed, 백버퍼 2개.
2. `swap->GetBuffer(0)` → `device->CreateRenderTargetView` (백버퍼 RTV).
3. 뷰포트 설정 (창 크기).
4. 셰이더 컴파일 (`D3DCompile`, 런타임) → VS/PS 생성.
5. `CreateInputLayout` (POSITION+COLOR).
6. **Dynamic VertexBuffer** (`USAGE_DYNAMIC`, `CPU_ACCESS_WRITE`, 최대 쿼드 수×6 vtx) + **ConstantBuffer** (4x4 viewProj).
7. Blend(알파) / Rasterizer(컬링 off, solid) / Depth(2D는 off) 상태.

### 5.3 한 프레임 (per-frame GPU 흐름)

```
RenderSystem이 모은 DrawItem[]
        │  (각 쿼드 → 6 정점: pos+color)
        ▼
[CPU 정점 배열] ──Map/Unmap──▶ [Dynamic VertexBuffer]
        │
        ▼  ── DX11 파이프라인 ──
   IA (VB, InputLayout, TRIANGLELIST)
        ▼
   VS  clip = mul(uViewProj, float4(pos,1))     ← ConstantBuffer(b0)
        ▼
   RS (뷰포트, 컬링 off)
        ▼
   PS  return color
        ▼
   OM → RTV(백버퍼)
        ▼
   Present(swapchain)
```

- **배칭**: 한 프레임의 모든 쿼드를 한 Dynamic VB에 쌓아 **Draw 한 번**(또는 텍스처별로 몇 번). 엔티티 수천 개도 거뜬.
- **정점 포맷 (v1)**: `struct Vertex { float x,y,z;  float r,g,b,a; }`. 텍스처 붙이면 `u,v` 추가.

### 5.4 셰이더 (최소, HLSL)

```hlsl
cbuffer CB : register(b0) { float4x4 uViewProj; };
struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };

VSOut VSMain(VSIn i){ VSOut o; o.pos = mul(uViewProj, float4(i.pos,1)); o.col = i.col; return o; }
float4 PSMain(VSOut i) : SV_TARGET { return i.col; }
```

### 5.5 리사이즈 / 종료

- **Resize**: RTV Release → `swap->ResizeBuffers` → RTV 재생성 → 뷰포트 갱신. (`WM_SIZE` → `IWindow::WasResized()` → `IRenderer::Resize`)
- **Shutdown**: 모든 COM 객체 역순 Release.

---

## 6. MinGW + DX11 실전 메모 ⚠️

대부분 자료가 MSVC 기준이라, 당신의 `msys64 ucrt64 g++`에선 한 꺼풀 벗긴다.

| 항목 | MinGW에서 |
|---|---|
| 링크 | `-ld3d11 -ldxgi -ld3dcompiler` (CMake `target_link_libraries`, `ws2_32`처럼) |
| 헤더 | `<d3d11.h> <dxgi.h> <d3dcompiler.h>` — ucrt64에 있음 |
| COM 포인터 | `Microsoft::WRL::ComPtr`(MSVC계) 대신 **raw 포인터 + 수동 `Release()`** 또는 직접 만든 소형 `ComPtr` |
| `IID_PPV_ARGS` | MinGW에서 까다로울 수 있음 → `__uuidof(T)` 명시 사용 |
| 셰이더 컴파일 | `D3DCompile` (런타임) — 동작함. 나중에 빌드타임 사전 컴파일로 전환 가능 |

CMake (예시):

```cmake
if(WIN32)
    target_link_libraries(EngineCore PUBLIC d3d11 dxgi d3dcompiler)
endif()
```

---

## 7. 결정론 · 스레딩 규칙

- **단일 스레드 먼저** — sim과 render 모두 메인 스레드. 렌더 스레드 분리는 *나중에(아마 안 함)*.
- **`Fixed64 → float`는 `RenderSystem`에서만** — `transform.position.x.ToFloat()`. 그 결과는 절대 시뮬로 안 돌아간다.
- **클라 전용 데이터는 스냅샷에서 제외** — `Renderable`/`Camera`/`PreviousTransform`은 `RegisterSnapshotComponent` 금지. 서버 월드엔 아예 없음.

---

## 8. 단계별 구현 로드맵

```mermaid
flowchart LR
    P0["P0\nCMake DX11 링크\n+ 빈 스켈레톤"] --> P1["P1\nWin32Window\n(빈 창 + 닫기)"]
    P1 --> P2["P2\nDevice/SwapChain/RTV\n+ Clear + Present\n(색 창)"]
    P2 --> P3["P3\n셰이더+동적VB\nDrawQuad (쿼드 1개)"]
    P3 --> P4["P4\nCamera(ortho)\nworld→screen"]
    P4 --> P5["P5\nRenderable+RenderSystem\nView 기반 그리기"]
    P5 --> P6["P6\nMovementSystem\n(엔티티가 움직인다)"]
    P6 --> P7["P7\nPreviousTransform\n보간(부드럽게)"]
    P7 --> P8["P8 (스트레치)\n호스트 모드 스냅샷을\n렌더"]
```

| Phase | 산출물 | "됐다"의 기준 |
|---|---|---|
| **P0** | CMake 링크, `Dx11Renderer` 스텁 | 빌드 통과 |
| **P1** | `IWindow`/`Win32Window` + 펌프 | 빈 창이 뜨고 X로 닫힌다 |
| **P2** | Device/SwapChain/RTV + BeginFrame/Present | 창이 단색으로 칠해진다 |
| **P3** | 셰이더 + Dynamic VB + `DrawQuad` | 화면에 쿼드 하나 |
| **P4** | `Camera` + ViewProj 상수버퍼 | 월드 좌표가 화면에 정확히 매핑 |
| **P5** | `Renderable` + `RenderSystem` + View | ECS 엔티티들이 그려짐 |
| **P6** | `MovementSystem`(Fixed64) | **엔티티가 움직인다 ← 핵심 마일스톤** |
| **P7** | `PreviousTransform` + 보간 | 50Hz인데 부드럽다 |
| **P8** | 호스트 모드 + 스냅샷 적용 | 렌더되는 클라가 **서버 권위 상태**를 표시 |

이후: 텍스처/스프라이트 → 3D 메시(같은 `IRenderer`, `DrawMesh` + 3D 카메라) → (정 필요하면) GPU 3D 심화.

---

## 9. 파일/모듈 배치 (`tools/Create-CppArtifact.ps1`로 생성)

```
EngineCore/
  Platform/
    IWindow.hpp
    Win32Window.{hpp,cpp}
  Render/
    Color.hpp            (Color, Float2, Mat4 등 표현용 POD)
    IRenderer.hpp
    RenderContext.hpp    (DrawItem)
    Camera.hpp
    RenderSystem.{hpp,cpp}
    Dx11/
      Dx11Renderer.{hpp,cpp}
      Shaders.hlsl  (또는 .cpp에 인라인 문자열)
  Components/
    Renderable.hpp
    PreviousTransform.hpp
```

> `EngineCore/Render/`를 별도 CMake 타깃(`Renderer`)으로 빼서 "엔진 코어 ↔ 렌더"를 물리적으로 분리하는 것도 좋다. 그러면 서버 빌드는 렌더를 링크조차 안 한다.

---

## 10. 미결 결정 / 트레이드오프

| 결정 | 옵션 | 1차 권장 |
|---|---|---|
| 2D vs 3D 먼저 | 2D 탑다운(XZ→화면) / 3D 원근 | **2D** (가장 빠른 "보인다") |
| 그리기 단위 | 쿼드/스프라이트 / 메시 | **컬러 쿼드** (텍스처·메시는 후속) |
| 셰이더 컴파일 | 런타임 `D3DCompile` / 사전 컴파일 | **런타임** (의존 0, 나중에 전환) |
| 렌더 스레드 | 단일 / 분리 | **단일** (분리는 거의 불필요) |
| 렌더 타깃 분리 | EngineCore에 포함 / 별도 `Renderer` 타깃 | **별도 타깃** (서버가 안 링크) |
| 보간 | v1부터 / 나중 | **나중(P7)** — v1은 cur만 |

---

## 한 줄 요약

**Win32Window(HWND) → `IRenderer`(DX11: device/swapchain/RTV/셰이더/동적VB) → `RenderSystem`이 `View<Transform,Renderable>`를 보간해 DrawItem으로 → 배칭 Draw → Present.** 엔진 코어는 DX11을 모르고, 게임플레이는 Fixed64 결정론을 유지하며, 렌더는 경계에서만 float을 쓴다.
