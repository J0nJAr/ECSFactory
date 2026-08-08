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
