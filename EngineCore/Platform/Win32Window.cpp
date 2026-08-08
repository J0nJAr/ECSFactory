#include "Platform/Win32Window.hpp"

// windows.h는 여기서만 들어온다(헤더에 노출하지 않는 이유는 Win32Window.hpp 주석 참조).
// 두 매크로는 windows.h를 include하기 "전에" 정의해야 효과가 있다.
//  - WIN32_LEAN_AND_MEAN: OLE/RPC/소켓 등 안 쓰는 하위 헤더를 잘라내 컴파일을 줄인다.
//  - NOMINMAX: min/max 매크로를 막는다. 이게 없으면 <algorithm>의 std::min/std::max가
//              토큰 단위로 깨진다. 이 파일이 지금 그걸 안 쓰더라도, 나중에 누가
//              쓰기 시작했을 때 원인을 알 수 없는 에러가 나는 걸 미리 막아 둔다.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// UNICODE/_UNICODE: 이 파일은 W계열 API(CreateWindowExW 등)만 부르는데, IDC_ARROW 같은
// 리소스 매크로는 이 정의가 없으면 A계열(char*)로 전개되어 타입이 안 맞는다.
// "W만 쓴다"는 결정을 매크로 레벨까지 일관되게 만드는 스위치다.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <string>

#include "Base/Logger.hpp"

namespace EF {

    namespace {

        // 윈도우 클래스 이름. 프로세스 안에서 유일하면 되고, 다른 프로그램과 겹치지만 않으면 된다.
        constexpr wchar_t kWindowClassName[] = L"EFWin32WindowClass";

        // 창 스타일. WS_OVERLAPPEDWINDOW = 타이틀바 + 시스템 메뉴 + 최소/최대화 + 크기 조절 테두리.
        // AdjustWindowRectEx가 "이 스타일이 클라이언트 영역 밖에 얼마를 더 먹는가"를 계산하므로,
        // 스타일과 크기 계산은 반드시 같은 값을 써야 한다. 그래서 상수로 묶어 둔다.
        constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;
        constexpr DWORD kWindowExStyle = 0;

        // UTF-8 → UTF-16. Win32의 W계열 API가 요구하는 형식이다.
        //
        // A계열(CreateWindowExA)을 쓰면 변환이 필요 없어 보이지만, 그건 시스템 ANSI
        // 코드페이지로 해석되어 한국어 제목이 깨진다. 이 저장소는 한국어 문자열을 쓰므로
        // W계열 + 명시적 UTF-8 변환이 맞다.
        std::wstring Utf8ToWide(const char* utf8) {
            if (utf8 == nullptr || *utf8 == '\0') {
                return std::wstring();
            }

            const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
            if (needed <= 1) {   // 1은 널 종료만 있는 경우
                return std::wstring();
            }

            std::wstring wide(static_cast<size_t>(needed - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), needed);
            return wide;
        }

    }

    // 창의 Win32 상태 전부. 헤더에는 전방 선언만 나가 있다.
    //
    // WndProc이 GWLP_USERDATA에서 되찾는 대상이 바로 이 구조체다. Win32Window*가 아니라
    // Impl*을 박아두는 이유: WndProc은 이 .cpp 안에만 있으면 되는데, Win32Window의
    // static 멤버로 선언하려면 헤더에 LRESULT/WPARAM 같은 Win32 타입이 새어 나간다.
    // Impl은 어차피 여기서 정의되므로 그런 문제가 없다.
    struct Win32Window::Impl {
        HWND hwnd = nullptr;
        HINSTANCE instance = nullptr;

        // 클라이언트 영역 크기. WM_SIZE가 갱신하는 진실의 원천이다.
        uint32_t width = 0;
        uint32_t height = 0;

        // ConsumeResized()가 읽고 내리는 소비형 플래그.
        bool resized = false;

        // 창이 파괴되었거나 WM_QUIT을 봤다. PumpMessages()의 반환값이 된다.
        bool closed = false;

        static bool EnsureWindowClass(HINSTANCE instance);
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

        LRESULT HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    };

    // 윈도우 클래스를 등록한다. 여러 번 불러도 안전하다.
    //
    // "이미 등록했나"를 static bool로 기억하지 않고 등록을 시도한 뒤 실패 코드를 보는 이유:
    // 상태를 안 들면 틀릴 수도 없다. RegisterClassEx는 같은 이름이 이미 있으면
    // ERROR_CLASS_ALREADY_EXISTS로 실패하는데, 창이 둘 이상인 경우 그건 정상 경로다.
    //
    // 짝이 되는 UnregisterClass는 부르지 않는다. 그 클래스의 창이 하나라도 살아 있으면
    // 실패하는데다, 프로세스가 끝나면 OS가 알아서 회수한다. 여기서 굳이 참조 카운트를
    // 만들 값어치가 없다.
    bool Win32Window::Impl::EnsureWindowClass(HINSTANCE instance) {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);

        // CS_HREDRAW | CS_VREDRAW: 폭/높이가 바뀌면 클라이언트 영역 전체를 무효화한다.
        // 부분 갱신은 스왑체인이 통째로 다시 그리는 구조에서 의미가 없다.
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &Impl::WndProc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        // 배경 브러시 없음. GDI가 창을 먼저 칠하지 않게 한다 —
        // 어차피 매 프레임 스왑체인이 전체를 덮으므로, OS가 먼저 칠하면 리사이즈 중에
        // 그 배경색이 한 프레임 비쳐 깜빡인다.
        windowClass.hbrBackground = nullptr;
        windowClass.lpszClassName = kWindowClassName;

        if (RegisterClassExW(&windowClass) != 0) {
            return true;
        }

        return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    // Win32가 부르는 창 프로시저.
    //
    // ── 왜 이 우회가 필요한가 ────────────────────────────────────────────────
    // WndProc은 OS가 직접 호출하는 C 함수 포인터(WNDPROC)다. 즉 비정적 멤버 함수일 수
    // 없고, `this`가 없다. 그런데 메시지를 처리하려면 인스턴스 상태(크기, 종료 플래그)에
    // 닿아야 한다. HWND는 인자로 오지만 HWND → 우리 객체를 이어 줄 다리가 필요하다.
    //
    // 표준 해법:
    //   1) CreateWindowEx의 마지막 인자(lpParam)에 인스턴스 포인터를 넘긴다.
    //   2) 그 포인터가 창의 첫 메시지인 WM_NCCREATE에 CREATESTRUCT::lpCreateParams로 실려 온다.
    //   3) 그때 SetWindowLongPtr(hwnd, GWLP_USERDATA, ptr)로 그 HWND에 붙여 둔다.
    //   4) 이후 모든 메시지에서 GetWindowLongPtr로 되찾는다.
    //
    // 대안이었던 전역 std::map<HWND, Impl*>를 안 쓴 이유: USERDATA는 커널이 창마다 주는
    // 포인터 크기 슬롯이라 조회가 O(1)이고, 전역 상태·잠금·정리 코드가 전혀 필요 없다.
    // 창이 여러 개가 되어도 그대로 동작한다.
    //
    // ⚠️ WM_NCCREATE보다 먼저 오는 메시지가 있다(WM_GETMINMAXINFO 등). 그때는 USERDATA가
    //    아직 비어 있으므로 반드시 null 검사를 하고 DefWindowProc으로 넘겨야 한다.
    LRESULT CALLBACK Win32Window::Impl::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            auto* self = static_cast<Impl*>(create->lpCreateParams);

            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

            // CreateWindowEx가 아직 반환하지 않았으므로 Create()는 HWND를 모른다.
            // 창 생성 도중에 오는 메시지(WM_SIZE 등)도 hwnd를 쓸 수 있게 여기서 채워 둔다.
            self->hwnd = hwnd;

            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr) {
            return DefWindowProcW(hwnd, message, wParam, lParam);
        }

        return self->HandleMessage(hwnd, message, wParam, lParam);
    }

    // 인스턴스 상태에 접근할 수 있는 진짜 메시지 처리부.
    //
    // 📌 앞으로 메시지는 여기 switch에 case를 추가하는 것으로 늘린다. 지금 비어 있는 자리:
    //    - WM_CHAR      → 문자 큐에 push. 콘솔/IP 입력에 필요하다(트랙 U6).
    //                     키 상태 폴링으로는 Shift·키보드 레이아웃·키 반복을 못 다룬다.
    //                     PumpMessages()가 이미 TranslateMessage를 부르고 있어서
    //                     WM_CHAR은 지금도 생성되고 있다 — 받아 담기만 하면 된다.
    //    - WM_MOUSEMOVE / WM_*BUTTON* → lParam의 좌표는 이미 "클라이언트 좌표"다.
    //                     UI 히트테스트가 요구하는 게 정확히 이것이라, 전역 커서 좌표를
    //                     ScreenToClient로 되돌리는 것보다 이 경로가 맞다.
    //    (docs/design/ui-layer.md §6)
    //    새 상태가 필요하면 Impl에 필드를 늘리면 된다 — 헤더는 건드릴 필요가 없다.
    LRESULT Win32Window::Impl::HandleMessage(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_SIZE: {
            // 최소화는 0x0 크기로 온다. 이걸 그대로 반영하면 Width()/Height()가 0이 되어
            // 종횡비 계산(0 나눗셈)과 스왑체인 재생성이 터진다. 복원되면 WM_SIZE가 다시
            // 오므로, 최소화 상태는 그냥 무시하는 게 맞다.
            if (wParam == SIZE_MINIMIZED) {
                return 0;
            }

            const uint32_t newWidth = static_cast<uint32_t>(LOWORD(lParam));
            const uint32_t newHeight = static_cast<uint32_t>(HIWORD(lParam));
            if (newWidth == 0 || newHeight == 0) {
                return 0;
            }

            // 같은 크기로 여러 번 오는 경우가 있다(포커스 변화, 최대화 해제 등).
            // 그때마다 resized를 세우면 렌더러가 무의미한 스왑체인 재생성을 한다.
            if (newWidth != width || newHeight != height) {
                width = newWidth;
                height = newHeight;
                resized = true;
            }
            return 0;
        }

        case WM_CLOSE:
            // 사용자가 X를 눌렀다. 기본 동작은 DestroyWindow인데, 명시적으로 부른다 —
            // 나중에 "정말 종료할까요?" 확인이 필요해지면 끼워 넣을 자리가 여기이기 때문이다.
            DestroyWindow(windowHandle);
            return 0;

        case WM_DESTROY:
            // 창이 실제로 사라졌다. 여기서 hwnd를 비워야 Destroy()가 이미 죽은 핸들에
            // DestroyWindow를 또 부르지 않는다(= 멱등성의 한쪽 절반).
            closed = true;
            hwnd = nullptr;

            // 스레드 큐에 WM_QUIT을 넣는다. PumpMessages()가 이걸 보고 false를 돌려준다.
            PostQuitMessage(0);
            return 0;

        default:
            break;
        }

        // 처리하지 않은 메시지는 반드시 기본 처리로 넘긴다.
        // 안 그러면 창 이동·크기 조절·시스템 메뉴 같은 기본 동작이 통째로 죽는다.
        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }

    Win32Window::Win32Window()
        : m_impl(std::make_unique<Impl>()) {
    }

    Win32Window::~Win32Window() {
        Destroy();
    }

    bool Win32Window::Create(const char* title, uint32_t width, uint32_t height) {
        if (m_impl->hwnd != nullptr) {
            LOG_WARN("Win32Window::Create: 이미 창이 있다. 무시한다.");
            return false;
        }

        if (width == 0 || height == 0) {
            LOG_ERROR("Win32Window::Create: 크기가 0이다 ({}x{}).", width, height);
            return false;
        }

        m_impl->instance = GetModuleHandleW(nullptr);
        if (!Impl::EnsureWindowClass(m_impl->instance)) {
            LOG_ERROR("Win32Window::Create: 윈도우 클래스 등록 실패 (GetLastError={}).",
                      static_cast<uint32_t>(GetLastError()));
            return false;
        }

        // ── 요청한 크기를 "클라이언트 영역" 크기로 만든다 ──────────────────────
        // CreateWindowEx가 받는 크기는 테두리와 타이틀바를 포함한 창 전체 크기다.
        // 요청값을 그대로 넘기면 실제로 그림이 들어가는 영역은 그만큼 작아진다
        // (1280x720을 요청했는데 스왑체인은 1264x681 같은 값이 된다).
        // AdjustWindowRectEx가 주어진 스타일에 대해 "바깥 사각형"을 역산해 준다.
        //
        // ⚠️ 이 함수는 DPI를 모른다. 프로세스에 per-monitor DPI 인식을 켜지 않았으므로
        //    배율이 걸린 디스플레이에서는 OS가 창을 통째로 확대하고, 클라이언트 영역의
        //    실제 픽셀 수가 요청과 달라질 수 있다. 픽셀 정확도가 필요해지면
        //    SetProcessDpiAwarenessContext + AdjustWindowRectExForDpi로 바꾼다.
        RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
        AdjustWindowRectEx(&rect, kWindowStyle, FALSE /* 메뉴 없음 */, kWindowExStyle);

        const int outerWidth = static_cast<int>(rect.right - rect.left);
        const int outerHeight = static_cast<int>(rect.bottom - rect.top);

        const std::wstring wideTitle = Utf8ToWide(title);

        // 마지막 인자(lpParam)가 WM_NCCREATE로 실려 간다. WndProc 주석의 1) 단계다.
        const HWND hwnd = CreateWindowExW(
            kWindowExStyle,
            kWindowClassName,
            wideTitle.c_str(),
            kWindowStyle,
            CW_USEDEFAULT, CW_USEDEFAULT,
            outerWidth, outerHeight,
            nullptr,              // 부모 없음
            nullptr,              // 메뉴 없음
            m_impl->instance,
            m_impl.get());

        if (hwnd == nullptr) {
            LOG_ERROR("Win32Window::Create: CreateWindowEx 실패 (GetLastError={}).",
                      static_cast<uint32_t>(GetLastError()));
            return false;
        }

        // 요청값이 아니라 OS가 실제로 준 클라이언트 크기를 진실로 삼는다.
        // (DPI 배율이나 화면보다 큰 요청 때문에 둘이 다를 수 있다.)
        RECT clientRect = {};
        GetClientRect(hwnd, &clientRect);
        m_impl->width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        m_impl->height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
        m_impl->closed = false;

        // 창 생성 도중에도 WM_SIZE가 오므로 resized가 이미 서 있을 수 있다.
        // 하지만 렌더러는 아직 초기화되지도 않았고, 초기화할 때 이 크기를 그대로 읽는다.
        // 첫 프레임에 의미 없는 Resize를 시키지 않도록 여기서 내린다.
        m_impl->resized = false;

        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);

        LOG_INFO("Win32Window: 창 생성 ({}x{} 클라이언트 영역).", m_impl->width, m_impl->height);
        return true;
    }

    void Win32Window::Destroy() {
        if (m_impl->hwnd != nullptr) {
            const HWND hwnd = m_impl->hwnd;

            // 먼저 지우고 부른다. DestroyWindow는 같은 스레드의 WndProc으로 WM_DESTROY를
            // "동기적으로" 보내는데, 그 핸들러도 hwnd를 비운다. 순서를 뒤집어도 결과는
            // 같지만, 이렇게 두면 "핸들이 비어 있다 = 더 파괴할 것이 없다"가 항상 참이다.
            m_impl->hwnd = nullptr;
            DestroyWindow(hwnd);
        }

        m_impl->closed = true;
    }

    bool Win32Window::PumpMessages() {
        MSG message = {};

        // PeekMessage(PM_REMOVE)를 쓴다. GetMessage는 메시지가 올 때까지 블록하는데,
        // 게임 루프는 입력이 하나도 없어도 계속 돌아야 하므로 쓸 수 없다.
        //
        // 첫 인자를 nullptr로 두는 게 중요하다. HWND를 지정하면 그 창의 메시지만 받는데,
        // WM_QUIT은 창이 아니라 "스레드 큐"에 들어가므로 영영 못 본다 = 종료를 놓친다.
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            if (message.message == WM_QUIT) {
                // 큐에 남은 메시지는 버린다. 창이 이미 없으므로 처리할 의미가 없다.
                m_impl->closed = true;
                return false;
            }

            // 키 메시지를 문자 메시지(WM_CHAR)로 번역한다.
            // 지금은 WM_CHAR을 아무도 안 받지만, 트랙 U6(콘솔 텍스트 입력)이 이걸 쓴다.
            // 여기서 빼 두면 나중에 "왜 WM_CHAR이 안 오지"로 한참 헤매게 된다.
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return !m_impl->closed;
    }

    void* Win32Window::NativeHandle() const {
        return static_cast<void*>(m_impl->hwnd);
    }

    uint32_t Win32Window::Width() const {
        return m_impl->width;
    }

    uint32_t Win32Window::Height() const {
        return m_impl->height;
    }

    bool Win32Window::ConsumeResized() {
        const bool wasResized = m_impl->resized;
        m_impl->resized = false;
        return wasResized;
    }

}
