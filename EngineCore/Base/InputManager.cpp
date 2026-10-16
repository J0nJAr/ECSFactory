#include "Base/InputManager.hpp"

#include <cstring>

// windows.h는 이 파일에서만 들어온다(헤더에 노출하지 않는 이유는 InputManager.hpp 주석 참조).
// 두 매크로는 windows.h를 include하기 "전에" 정의해야 효과가 있다. 근거는
// Platform/Win32Window.cpp와 같다:
//  - WIN32_LEAN_AND_MEAN: OLE/RPC/**소켓** 등 안 쓰는 하위 헤더를 잘라낸다.
//    소켓을 잘라내는 게 여기서는 특히 중요하다 — windows.h가 딸려오는 winsock1(구버전)이
//    EngineCore의 winsock2 사용과 충돌해 온 게 애초에 이 파일이 생긴 이유다.
//  - NOMINMAX: min/max 매크로를 막는다.
#if defined(EF_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace EF {

    namespace {
        // -------------------------------------------------------------------------
        // 입력 상태의 소유
        //
        // 예전에는 이 배열들이 `Input` 클래스의 `inline static` 멤버였다. C++17의
        // inline static은 "구현이 헤더에만 있는 클래스"가 정의를 한 번만 갖게 하려고
        // 쓰는 장치다. 구현이 .cpp로 내려온 지금은 그 이유가 사라졌고, 상태도 같이
        // 내려오는 게 맞다.
        //
        // 고른 것: 익명 네임스페이스의 파일 지역 변수. 클래스에는 데이터 멤버가
        // **하나도 없다**.
        //  - 정의가 이 번역 단위 하나뿐이므로 중복 정의(다중 정의) 링크 오류가 원천적으로
        //    생길 수 없다.
        //  - 배열 크기(256 / 3) 같은 구현 세부가 헤더에서 사라진다. 헤더는 순수하게
        //    "무엇을 물어볼 수 있는가"만 말한다.
        //
        // 버린 대안:
        //  - **클래스 안에 `static bool s_keyStates[256];` 선언 + .cpp에 정의**.
        //    가장 흔한 방식이고 동작도 한다. 하지만 선언과 정의를 두 곳에서 맞춰야 하고
        //    (한쪽만 고치면 링크 에러), 크기가 헤더에 남는다. 클래스가 상태에 접근하는
        //    유일한 코드가 어차피 이 파일뿐이라 멤버로 둘 이득이 없다.
        //  - **`inline static` 유지**. 링크는 문제없지만 상태가 헤더에 남아, "구현을
        //    .cpp로 가둔다"는 이번 변경의 목적이 반만 달성된다.
        //
        // ⚠️ 전역 상태다. 프로세스에 하나뿐이고 스레드 안전하지 않다.
        //    Update()와 질의를 같은 스레드(= 메인 루프)에서만 부를 것.
        // -------------------------------------------------------------------------

        // 가상 키코드 공간이 0~255라서 256이다(Win32 Virtual-Key Codes).
        constexpr int kKeyCount = 256;
        // 좌 / 우 / 휠 클릭. 그 이상(X버튼 등)은 아직 다루지 않는다.
        constexpr int kMouseButtonCount = 3;

        bool g_keyStates[kKeyCount];
        bool g_previousKeyStates[kKeyCount];

        bool g_mouseStates[kMouseButtonCount];
        bool g_previousMouseStates[kMouseButtonCount];

        float g_mouseX = 0.0f;
        float g_mouseY = 0.0f;

        // 범위를 벗어난 키코드·버튼 번호를 걸러낸다.
        //
        // 예전 구현은 인덱스를 검사 없이 그대로 썼다. 호출부가 `Keys::` 상수만 쓰면
        // 문제가 없지만, 값이 UI나 설정 파일에서 흘러들어오는 순간 배열 밖 읽기(미정의
        // 동작)가 된다. "없는 키는 눌려 있지 않다"로 정의해 두면 그 경로가 조용히
        // 안전해지고, 정상 호출부의 동작은 하나도 바뀌지 않는다.
        [[nodiscard]] bool InRange(int index, int count) {
            return index >= 0 && index < count;
        }
    }

    void Input::Initialize() {
        std::memset(g_keyStates, 0, sizeof(g_keyStates));
        std::memset(g_previousKeyStates, 0, sizeof(g_previousKeyStates));
        std::memset(g_mouseStates, 0, sizeof(g_mouseStates));
        std::memset(g_previousMouseStates, 0, sizeof(g_previousMouseStates));
        g_mouseX = 0.0f;
        g_mouseY = 0.0f;
    }

    void Input::Update() {
#if defined(EF_PLATFORM_WINDOWS)
        // 1. 현재 상태를 '이전 상태'로 복사.
        //    Down/Up 엣지는 오로지 이 두 스냅샷의 차이로 정의된다. 그래서 질의 함수는
        //    OS를 다시 부르지 않고, 한 프레임 안에서는 몇 번을 물어도 답이 같다.
        std::memcpy(g_previousKeyStates, g_keyStates, sizeof(g_keyStates));
        std::memcpy(g_previousMouseStates, g_mouseStates, sizeof(g_mouseStates));

        // 2. 키보드 전체 상태 갱신 (가상 키코드 0~255).
        //    ⚠️ GetAsyncKeyState는 시스템 전역 키 상태다 — 포커스와 무관하다.
        //       한계와 대응은 InputManager.hpp 주석 참조.
        //    0x8000 비트가 "지금 눌려 있음". 하위 비트(0x0001, "마지막 호출 이후 눌린 적
        //    있음")는 쓰지 않는다 — 그 비트는 읽으면 지워지므로, 같은 프레임에 다른 코드가
        //    먼저 읽었는지에 따라 답이 달라진다. 결정론적으로 다루기 어렵다.
        for (int i = 0; i < kKeyCount; ++i) {
            g_keyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
        }

        // 3. 마우스 버튼 상태 갱신 (0: 좌클릭, 1: 우클릭, 2: 휠클릭).
        //    ⚠️ VK_LBUTTON/VK_RBUTTON은 "주 버튼 / 보조 버튼"이라 사용자가 마우스를
        //       왼손잡이 설정으로 바꾸면 물리 버튼과 뒤바뀐다. 지금은 OS 설정을 그대로
        //       따르는 게 맞다고 보고 별도 처리를 하지 않는다.
        g_mouseStates[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        g_mouseStates[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        g_mouseStates[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

        // 4. 마우스 좌표 갱신 (현재는 모니터 절대 좌표).
        POINT p;
        if (GetCursorPos(&p)) {
            // TODO: 창 클라이언트 좌표로 바꾸려면 HWND가 필요하다(ScreenToClient).
            //       이 클래스는 창을 모르므로, 창 메시지 기반 입력(U6)으로 옮길 때
            //       함께 정리한다.
            g_mouseX = static_cast<float>(p.x);
            g_mouseY = static_cast<float>(p.y);
        }
        // GetCursorPos가 실패하면(예: 세션 잠금) 좌표를 **갱신하지 않고 직전 값을
        // 유지**한다. 0으로 초기화하면 커서가 화면 좌상단으로 순간이동한 것처럼 보인다.
#else
        // Windows 외 플랫폼: 입력 백엔드가 아직 없다. 스냅샷을 그대로 두면 모든 질의가
        // "아무것도 안 눌림"으로 답한다 — 링크는 되고 조작만 안 되는 상태다.
        // (헤더의 플랫폼 판별은 살아 있으니, 백엔드가 생기면 여기에 분기를 채우면 된다.)
#endif
    }

    bool Input::GetKey(int keyCode) {
        return InRange(keyCode, kKeyCount) && g_keyStates[keyCode];
    }

    bool Input::GetKeyDown(int keyCode) {
        return InRange(keyCode, kKeyCount)
            && g_keyStates[keyCode] && !g_previousKeyStates[keyCode];
    }

    bool Input::GetKeyUp(int keyCode) {
        return InRange(keyCode, kKeyCount)
            && !g_keyStates[keyCode] && g_previousKeyStates[keyCode];
    }

    bool Input::GetMouseButton(int button) {
        return InRange(button, kMouseButtonCount) && g_mouseStates[button];
    }

    bool Input::GetMouseButtonDown(int button) {
        return InRange(button, kMouseButtonCount)
            && g_mouseStates[button] && !g_previousMouseStates[button];
    }

    void Input::GetMousePosition(float& x, float& y) {
        x = g_mouseX;
        y = g_mouseY;
    }

}
