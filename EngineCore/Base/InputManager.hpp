#pragma once

// -----------------------------------------------------------------------------
// 플랫폼 판별
//
// 이 블록만은 헤더에 남긴다. "이 빌드가 어느 플랫폼인가"는 구현 파일 하나의 사정이
// 아니라 이 헤더를 include하는 쪽도 물을 수 있는 질문이고, .cpp로 내려버리면 같은
// 판별을 두 곳에서 반복하게 된다. 판별은 매크로 정의일 뿐이라 어떤 시스템 헤더도
// 끌어오지 않으므로 헤더에 있어도 오염이 없다.
//
// 현재 Windows 외 분기(LINUX / MAC)는 "이름만 있는" 상태다 — 판별은 되지만 대응하는
// 입력 구현이 없다(InputManager.cpp 참조). 그래도 지우지 않는 이유는, 지금 지우면
// 나중에 어디에 무엇을 끼워야 하는지가 코드에서 사라지기 때문이다.
// -----------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
    #define EF_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define EF_PLATFORM_LINUX
#elif defined(__APPLE__)
    #define EF_PLATFORM_MAC
#else
    #error "Unsupported platform"
#endif

#include "Defines.hpp"

namespace EF {

    // 키보드·마우스 입력을 프레임 단위로 샘플링해서 "지금 눌림 / 방금 눌림 / 방금 뗌"을
    // 알려주는 전역 파사드.
    //
    // 책임:
    //  - `Update()`가 불릴 때마다 플랫폼에서 키·마우스 상태를 한 번 읽어 이전 프레임분과
    //    함께 보관한다. 그 두 스냅샷의 차이가 Down/Up 엣지다.
    //  - 질의(`GetKey` 등)는 그 보관된 스냅샷만 본다. 질의가 OS를 다시 부르지 않으므로
    //    한 프레임 안에서 몇 번을 물어도 같은 답이 나온다. 엣지 판정이 성립하려면
    //    이 성질이 필요하다.
    //
    // 이 타입이 하지 않는 일:
    //  - 시뮬레이션에 값을 넣는 것. 여기서 읽은 키를 결정론 입력(`InputCommand`)으로
    //    바꾸는 경계 변환은 [Gameplay/InputMapping.hpp]가 맡는다.
    //  - 창 소유·메시지 펌프. `Update()`를 언제 부를지는 앱 루프가 정한다
    //    (`GameEngine::FrameUpdate` 맨 앞, 또는 클라 앱의 프레임 시작).
    //  - 키코드 정의. 가상 키코드 상수는 [Gameplay/InputMapping.hpp]의 `Keys`에 있다.
    //
    // ⚠️ 이 헤더는 <windows.h>를 포함하지 않는다. 일부러 그렇게 했다.
    //    이 파일은 `EngineCore`에 들어가고 `EngineCore`는 **dedicated server도 링크한다**.
    //    windows.h는 min/max를 매크로로 덮고 Rectangle 같은 흔한 이름을 매크로로
    //    점령하며, 무엇보다 winsock2.h보다 먼저 들어오면 winsock1과 충돌한다. 그
    //    오염은 이 헤더를 include하는 모든 번역 단위로 번진다.
    //    (실제로 사고가 났었다 — GameClient/main.cpp가 이 헤더를 네트워크 헤더보다
    //     먼저 include하자 "winsock2.h must be included before windows.h" 경고가 났고,
    //     include 순서를 손으로 맞춰 넘겼다. 순서에 의존하는 상태 자체가 함정이라
    //     Win32Window.hpp / Dx11Renderer.hpp와 같은 방식으로 정리했다.)
    //
    //    저 둘은 숨길 상태가 인스턴스마다 있어서 PIMPL을 썼지만, 여기는 인스턴스가
    //    없는 전역 파사드라 더 단순하게 끝난다: **데이터 멤버를 아예 두지 않고**
    //    상태 전체를 InputManager.cpp 안의 파일 지역 변수로 내렸다.
    //
    // ⚠️ 지금의 Windows 구현은 `GetAsyncKeyState`를 쓴다. 그건 프로세스가 아니라
    //    **시스템 전역**의 키 상태다. 그래서 창을 두 개 띄워 놓으면 어느 창이 포커스든
    //    양쪽이 똑같이 반응한다 — 한 쪽에서 W를 누르면 두 클라이언트가 같이 움직인다.
    //    입력을 수집하는 쪽이 `IWindow::HasFocus()`로 걸러야 한다(그 함수 주석에 같은
    //    경고가 있다). 근본 해결은 창 메시지(WM_KEYDOWN/WM_CHAR) 기반으로 옮기는
    //    것이고 트랙 U6에서 어차피 필요해진다. 이번 변경의 범위는 아니다.
    //
    // ⚠️ 상태가 전역이라 **프로세스에 하나뿐**이다. 두 클라이언트를 한 프로세스에
    //    띄우는 경우(host 모드 실험 등) 둘이 같은 상태를 본다. 위 GetAsyncKeyState
    //    한계와 뿌리가 같은 문제고, 같은 시점에 함께 해결될 것이다.
    class Input {
    public:
        // 게임 루프 시작 시 1회 호출. 보관 중인 스냅샷을 전부 0으로 되돌린다.
        //
        // 여러 번 불러도 안전하다. 다만 "이전 프레임" 스냅샷까지 지우므로, 호출 직후
        // 첫 `Update()`에서는 이미 눌려 있던 키도 Down 엣지로 보인다.
        static void Initialize();

        // 매 프레임(FrameUpdate) 가장 처음에 호출.
        // 현재 스냅샷을 '이전'으로 밀고 플랫폼에서 새 상태를 읽는다.
        //
        // ⚠️ 이걸 프레임당 두 번 부르면 두 번째 호출에서 Down/Up 엣지가 사라진다
        //    (이전 = 현재가 되어 차이가 0). 프레임당 정확히 한 번만 부를 것.
        static void Update();

        // 키가 "눌려있는" 상태인가? (Hold) — 연발 사격, 이동
        //
        // keyCode는 Win32 가상 키코드 공간이다(0~255). 상수는
        // [Gameplay/InputMapping.hpp]의 `Keys`를 쓴다.
        // 범위를 벗어난 키코드는 false — 예전 구현은 배열을 그대로 인덱싱해서
        // 범위 밖 값이 들어오면 미정의 동작이었다. 정상 호출부의 동작은 그대로다.
        [[nodiscard]] static bool GetKey(int keyCode);

        // 이번 프레임에 "방금" 눌렀는가? (Down) — 스킬 사용, 점프, UI 클릭
        [[nodiscard]] static bool GetKeyDown(int keyCode);

        // 이번 프레임에 "방금" 떼었는가? (Up) — 차지(Charge) 스킬 발사
        [[nodiscard]] static bool GetKeyUp(int keyCode);

        // button — 0: 좌클릭, 1: 우클릭, 2: 휠클릭. 그 밖의 값은 false.
        [[nodiscard]] static bool GetMouseButton(int button);
        [[nodiscard]] static bool GetMouseButtonDown(int button);

        // ⚠️ 현재는 **모니터 절대 좌표**다. 창 클라이언트 좌표가 아니다.
        //    변환하려면 HWND가 필요한데(ScreenToClient), 이 클래스는 창을 모른다.
        //    창 메시지 기반 입력으로 옮길 때 함께 정리된다(위 U6 주석).
        static void GetMousePosition(float& x, float& y);
    };

}
