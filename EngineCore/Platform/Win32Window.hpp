#pragma once

#include <memory>

#include "Platform/IWindow.hpp"

namespace EF {

    // IWindow의 Win32 구현. HWND 하나와 그 창의 메시지 펌프를 소유한다.
    //
    // 책임:
    //  - 윈도우 클래스 등록 + 창 생성(요청 크기 = 클라이언트 영역 크기)
    //  - 쌓인 창 메시지를 소진하고, 창이 닫혔는지 알려준다
    //  - 렌더 백엔드에 넘길 네이티브 핸들과 현재 클라이언트 크기 제공
    //
    // 이 타입이 하지 않는 일:
    //  - 렌더링. HWND를 만들 뿐 D3D는 모른다(Dx11Renderer가 NativeHandle()을 받아 쓴다).
    //  - 루프 소유. `while (PumpMessages()) { ... }`를 도는 건 클라 앱이다
    //    (서버는 창이 없다 — docs/design/rendering-dx11.md §3).
    //  - 입력 상태 보관. 지금은 창 메시지 중 크기·종료만 본다.
    //
    // ⚠️ 스레드 규칙: 창은 자기를 만든 스레드에서만 펌프할 수 있다(Win32 규칙).
    //    Create / PumpMessages / Destroy를 전부 같은 스레드(= 메인)에서 부를 것.
    //
    // ⚠️ 이 헤더는 <windows.h>를 포함하지 않는다. 일부러 그렇게 했다.
    //    windows.h는 min/max를 매크로로 정의해서 std::min/std::max를 부수고,
    //    Rectangle·CreateWindow 같은 흔한 이름을 매크로로 점령한다. 그게 이 헤더를
    //    include하는 모든 번역 단위로 번진다. Dx11Renderer.hpp가 DX11 타입을 숨긴 것과
    //    같은 이유이고, 같은 방법(PIMPL)을 쓴다.
    //
    //    `void* m_hwnd` 하나만 드는 방법도 있었다. 그러지 않은 이유는, 숨겨야 할 Win32
    //    상태가 HWND만이 아니기 때문이다 — HINSTANCE, 창 스타일(DWORD), WndProc이
    //    갱신하는 크기·종료 플래그가 함께 있다. 이걸 void*와 원시 타입으로 흩어 두면
    //    .cpp가 캐스팅 범벅이 되고, 무엇보다 WndProc이 "인스턴스 상태"를 한 덩어리로
    //    되찾을 수 없다. Impl 하나로 묶으면 GWLP_USERDATA에 그 포인터만 박으면 된다.
    class Win32Window final : public IWindow {
    public:
        Win32Window();
        ~Win32Window() override;

        Win32Window(const Win32Window&) = delete;
        Win32Window& operator=(const Win32Window&) = delete;

        // 클라이언트 영역이 width x height가 되는 창을 만들고 화면에 띄운다.
        // title은 UTF-8로 해석한다(이 저장소는 한국어 문자열을 쓴다).
        // 이미 창이 있으면 아무것도 하지 않고 false.
        bool Create(const char* title, uint32_t width, uint32_t height) override;

        // 멱등하다. 두 번 불러도, 사용자가 이미 X로 닫은 뒤에 불러도 안전하다.
        void Destroy() override;

        // 제목을 UTF-8로 해석해 바꾼다. Create()와 똑같은 변환 경로를 쓴다.
        //
        // 창이 없으면(Create 전이거나 이미 Destroy·사용자가 X로 닫은 뒤) 아무것도 하지
        // 않는다 — Destroy()와 같은 "핸들이 없으면 할 일도 없다" 규칙이다. 디버그 표시용
        // 호출이 창 수명 경계에서 터지면 곤란하고, 그렇다고 호출부마다 살아 있는지
        // 검사하게 만들 이유도 없어서 여기서 흡수한다.
        //
        // ⚠️ 매 프레임 호출 금지. 내부가 SetWindowTextW = WM_SETTEXT 동기 전송이다
        //    (근거는 IWindow::SetTitle 주석).
        void SetTitle(const char* title) override;

        bool PumpMessages() override;

        // HWND. 창이 없으면 nullptr.
        [[nodiscard]] void* NativeHandle() const override;

        [[nodiscard]] uint32_t Width() const override;
        [[nodiscard]] uint32_t Height() const override;

        bool ConsumeResized() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

}
