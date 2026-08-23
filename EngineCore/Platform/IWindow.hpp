#pragma once

#include <cstdint>

namespace EF {

    // 플랫폼 창 추상화.
    //
    // 엔진 코어는 HWND도 Win32도 모른다. 렌더 백엔드(Dx11Renderer)만 NativeHandle()을
    // 자기가 아는 타입으로 되돌려 쓴다. IRenderer 뒤에 DX11을 숨긴 것과 같은 패턴이다.
    //
    // 창 메시지 펌프를 엔진이 아니라 "앱"이 소유하는 이유: 서버는 창이 없다.
    // GameEngine이 창을 알면 dedicated server 빌드가 오염된다.
    // (docs/design/rendering-dx11.md §3)
    class IWindow {
    public:
        virtual ~IWindow() = default;

        virtual bool Create(const char* title, uint32_t width, uint32_t height) = 0;
        virtual void Destroy() = 0;

        // 창 제목을 실행 중에 바꾼다. title은 UTF-8로 해석한다(이 저장소는 한국어를 쓴다).
        //
        // 왜 이게 렌더러가 아니라 창 인터페이스에 있는가:
        // 제목 표시줄은 우리가 그리는 그림이 아니라 OS가 소유한 "플랫폼 창의 속성"이다.
        // 렌더 백엔드는 클라이언트 영역 안쪽만 알고, 그 바깥(테두리·타이틀바)에는
        // 손이 닿지 않는다. 창을 만든 쪽만 바꿀 수 있으니 창 인터페이스가 제자리다.
        //
        // 지금의 용도는 결정론 디버깅이다. 클라이언트를 둘 띄워 각 창 제목에 tick과
        // 월드 해시를 찍으면, 폰트·UI 계층 없이도 두 시뮬레이션이 갈라졌는지 눈으로
        // 확인할 수 있다. 화면 안에 글자를 그리는 진짜 디버그 오버레이(트랙 U5)가
        // 들어오면 이 용도는 사라지지만, 제목 변경 자체는 창의 정당한 기능이라 남는다.
        //
        // ⚠️ 매 프레임 무조건 부르지 말 것. 구현은 창에 메시지를 보내는 동기 호출이라
        //    (Win32에서는 SetWindowText = WM_SETTEXT) 내용이 그대로여도 비용은 다 낸다.
        //    호출부가 직전에 넣은 문자열을 들고 있다가 "달라졌을 때만" 부르는 게 규약이다.
        virtual void SetTitle(const char* title) = 0;

        // 쌓인 창 메시지를 전부 처리한다.
        // 창이 닫혔으면 false를 돌려준다 — 이게 앱 루프의 종료 조건이다.
        virtual bool PumpMessages() = 0;

        // 플랫폼 핸들(Win32에서는 HWND). 렌더 백엔드가 캐스팅해서 쓴다.
        [[nodiscard]] virtual void* NativeHandle() const = 0;

        [[nodiscard]] virtual uint32_t Width() const = 0;
        [[nodiscard]] virtual uint32_t Height() const = 0;

        // 마지막 호출 이후 창 크기가 바뀌었는지. true면 렌더러가 Resize를 해야 한다.
        //
        // 조회하면 플래그가 내려가는 "소비형"이다. 상태를 남겨두면
        // 호출부마다 언제 지울지를 따로 관리해야 해서, 소비형이 실수가 적다.
        virtual bool ConsumeResized() = 0;
    };

}
