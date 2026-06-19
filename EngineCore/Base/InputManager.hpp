#pragma once

#if defined(_WIN32) || defined(_WIN64)
    #define EF_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define EF_PLATFORM_LINUX
#elif defined(__APPLE__)
    #define EF_PLATFORM_MAC
#else
    #error "Unsupported platform"
#endif

#if defined(EF_PLATFORM_WINDOWS)
    #include <windows.h>
#endif

#include "Defines.hpp"
namespace EF {
/** 
 * 입력을 담당하는 클래스. 플랫폼별로 구현이 달라질 수 있으며, 키보드, 마우스, 게임패드 등의 입력을 처리합니다.
 * 우선은 Windows 플랫폼에서 키보드와 마우스 입력을 처리하는 기본적인 인터페이스를 제공합니다. 향후 필요에 따라 게임패드 지원이나 다른 플랫폼에 대한 구현도 추가할 수 있습니다.
 */
class Input{
public:
        // 게임 루프 시작 시 1회 호출
        static void Initialize() {
            memset(s_keyStates, 0, sizeof(s_keyStates));
            memset(s_previousKeyStates, 0, sizeof(s_previousKeyStates));
            memset(s_mouseStates, 0, sizeof(s_mouseStates));
            memset(s_previousMouseStates, 0, sizeof(s_previousMouseStates));
        }

        // 매 프레임(FrameUpdate) 가장 처음에 호출
        static void Update() {
#ifdef EF_PLATFORM_WINDOWS
            // 1. 현재 상태를 '이전 상태'로 복사
            memcpy(s_previousKeyStates, s_keyStates, sizeof(s_keyStates));
            memcpy(s_previousMouseStates, s_mouseStates, sizeof(s_mouseStates));

            // 2. 키보드 전체 상태 갱신 (가상 키코드 0~255)
            for (int i = 0; i < 256; ++i) {
                s_keyStates[i] = (GetAsyncKeyState(i) & 0x8000) != 0;
            }

            // 3. 마우스 버튼 상태 갱신 (0: 좌클릭, 1: 우클릭, 2: 휠클릭)
            s_mouseStates[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
            s_mouseStates[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
            s_mouseStates[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

            // 4. 마우스 좌표 갱신 (현재는 모니터 절대 좌표. 향후 ScreenToClient 연동 필요)
            POINT p;
            if (GetCursorPos(&p)) {
                // TODO: 윈도우 창 모드일 경우 HWND를 받아와서 Client 좌표로 변환해야 함
                // ScreenToClient(hwnd, &p); 
                s_mouseX = static_cast<float>(p.x);
                s_mouseY = static_cast<float>(p.y);
            }
#endif
        }

        // 키가 "눌려있는" 상태인가? (Hold) - 연발 사격, 이동
        static bool GetKey(int keyCode) {
            return s_keyStates[keyCode];
        }

        // 이번 프레임에 "방금" 눌렀는가? (Down) - 스킬 사용, 점프, UI 클릭
        static bool GetKeyDown(int keyCode) {
            return s_keyStates[keyCode] && !s_previousKeyStates[keyCode];
        }

        // 이번 프레임에 "방금" 떼었는가? (Up) - 차지(Charge) 스킬 발사
        static bool GetKeyUp(int keyCode) {
            return !s_keyStates[keyCode] && s_previousKeyStates[keyCode];
        }

        static bool GetMouseButton(int button) {
            return s_mouseStates[button];
        }

        static bool GetMouseButtonDown(int button) {
            return s_mouseStates[button] && !s_previousMouseStates[button];
        }

        static void GetMousePosition(float& x, float& y) {
            x = s_mouseX;
            y = s_mouseY;
        }

    private:
        // C++17 inline static을 사용하여 cpp 파일에 별도 정의(instantiation) 방지
        inline static bool s_keyStates[256];
        inline static bool s_previousKeyStates[256];
        
        inline static bool s_mouseStates[3];
        inline static bool s_previousMouseStates[3];
        
        inline static float s_mouseX = 0.0f;
        inline static float s_mouseY = 0.0f;
    };

}