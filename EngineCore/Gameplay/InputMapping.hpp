#pragma once

#include <cstdint>
#include <functional>
#include <span>

#include "Gameplay/PlayerMovement.hpp"
#include "Math/Fixed64.hpp"
#include "Network/InputCommand.hpp"

// -----------------------------------------------------------------------------
// InputMapping — "사람이 만진 하드웨어" 를 "와이어에 실리는 InputCommand" 로 바꾸는
// 순수 함수 모음. 로드맵 I1의 앞 절반이다.
//
// 책임:
//  - 눌린 키 → `kInputMove*` 비트마스크
//  - 마우스 픽셀 델타(float) → 조준 각도(Fixed64)
//  - 위 둘을 `InputCommand` 한 개로 조립
//
// 이 파일이 하지 않는 일:
//  - 창·메시지 펌프·`Input::Update()` 호출. 여기 함수들은 전부 인자로 받은 것만 본다.
//  - `NetworkSyncSystem::QueueLocalInput` 호출. 언제 몇 tick 뒤에 넣을지는 배선 쪽 문제다.
//  - 조준 각도를 시뮬레이션에 적용하는 일. `ApplyConfirmedInputs`가 확정 입력으로
//    받아서 하는 일이고, 아직 이동 비트만 소비한다.
//
// ─────────────────────────────────────────────────────────────────────────────
// ⚠️ 여기가 시뮬레이션의 경계다 (이 파일에서 제일 중요한 이야기)
//
// 마우스 델타는 화면 픽셀이고 float다. 시뮬레이션 값은 `Fixed64`다.
// float→Fixed64 변환은 반올림 방식·FPU 설정·컴파일러 최적화에 따라 기계마다 1 LSB쯤
// 다를 수 있다. 그런데도 **결정론은 깨지지 않는다.** 이유는 하나다:
//
//     변환 결과가 `InputCommand.aimYaw/aimPitch`에 실려 와이어를 타고,
//     서버가 확정한 뒤 모든 peer에게 **같은 비트로** 되돌아오기 때문이다.
//
// 즉 각 클라가 자기 마우스를 어떻게 해석하든, 시뮬레이션에 들어가는 값은 전원이 공유한
// 그 한 벌뿐이다. 위험한 것은 변환 자체가 아니라 **변환된 값을 전파하기 전에 로컬에서
// 먼저 시뮬레이션에 써버리는 것**이다. 그러면 그 클라만 다른 입력으로 돌게 되고 디싱크한다.
//
// 그래서 이 파일의 float은 전부 "경계 바깥" 값이고, 나가는 것은 전부 `Fixed64`다.
// (docs/design/roadmap.md I1, docs/design/deterministic-input-lockstep.md)
// ─────────────────────────────────────────────────────────────────────────────

namespace EF {

    // -------------------------------------------------------------------------
    // 가상 키코드
    //
    // ⚠️ 이 헤더는 <windows.h>를 포함하지 않는다. 일부러 그렇다.
    //    이 파일은 `EngineCore`에 들어가고 `EngineCore`는 **dedicated server도 링크한다**.
    //    서버 빌드에 windows.h가 딸려 들어가면 min/max가 매크로로 덮이고 Rectangle 같은
    //    흔한 이름이 매크로에 점령당한다 — 그 오염이 이 헤더를 include하는 모든 번역
    //    단위로 번진다. Win32Window.hpp가 HWND를 숨긴 것과 같은 이유다(그 파일 주석 참조).
    //
    // 그래서 필요한 값만 숫자로 직접 박는다. 출처는 Win32 Virtual-Key Codes다:
    //  - 영문자 키는 대문자 ASCII 코드를 그대로 쓴다('A' = 0x41 ... 'Z' = 0x5A).
    //  - 방향키는 VK_LEFT/UP/RIGHT/DOWN = 0x25/0x26/0x27/0x28.
    // 이 값들은 `Input::GetKey(int)`([Base/InputManager.hpp])가 기대하는 것과 같은 공간이다.
    //
    // 나중에 다른 플랫폼이 생기면 이 상수 테이블이 "엔진 키코드"가 되고 플랫폼 계층이
    // 자기 코드를 여기로 번역하게 될 것이다. 지금은 Win32 값과 1:1이라 번역이 항등이다.
    // -------------------------------------------------------------------------
    namespace Keys {
        inline constexpr int kA = 0x41;
        inline constexpr int kD = 0x44;
        inline constexpr int kS = 0x53;
        inline constexpr int kW = 0x57;

        inline constexpr int kArrowLeft  = 0x25;
        inline constexpr int kArrowUp    = 0x26;
        inline constexpr int kArrowRight = 0x27;
        inline constexpr int kArrowDown  = 0x28;
    }

    // -------------------------------------------------------------------------
    // 키 상태를 읽는 방법
    //
    // 고른 것: `std::function<bool(int)>` 하나를 인자로 받는다.
    //   - 호출부(클라 앱)는 `BuildButtonMask(Input::GetKey)`로 그냥 꽂으면 된다.
    //     `Input::GetKey`가 static 멤버 함수라 `bool(*)(int)`로 붙는다.
    //   - 테스트는 캡처 있는 람다를 넘겨 "눌린 키 집합"을 흉내낸다. 창도 GPU도
    //     전역 상태도 필요 없다.
    //
    // 버린 대안:
    //   - **생 함수 포인터 `bool(*)(int)`** — 할당이 없어 더 가볍지만 캡처를 못 한다.
    //     테스트가 눌린 키 집합을 파일 전역에 두어야 하고, 그러면 테스트끼리 상태를
    //     공유해 실행 순서에 의존하게 된다. 그건 이 하네스에서 특히 나쁘다.
    //   - **템플릿 인자로 받는 호출 가능 객체** — 제일 빠르지만 구현 전체가 헤더로
    //     올라온다. 이 파일의 요점은 "가상 키코드 표와 경계 변환을 .cpp 한 곳에
    //     가둔다"인데 그게 무너진다.
    //   - **`IKeyboard` 추상 클래스** — 테스트마다 목(mock) 클래스를 만들어야 해서
    //     질의 하나 받자고 치르는 비용이 크다.
    //   - **눌린 키를 미리 샘플링한 스냅샷 구조체** — 호출부가 스냅샷을 채우려면
    //     결국 키 상태 질의를 돌려야 한다. 계층만 하나 늘고 얻는 게 없다.
    //
    // 비용은 프레임당 std::function 하나이고, 이 함수는 프레임당 한 번 불린다.
    // -------------------------------------------------------------------------
    using KeyPressedFn = std::function<bool(int keyCode)>;

    // 키 하나가 켜는 `buttonMask` 비트. 바인딩을 "표(데이터)"로 둔 이유는,
    // 한 동작에 여러 키를 매다는 것(WASD와 방향키)과 나중의 키 리바인딩이
    // 코드 수정이 아니라 표 교체로 끝나기 때문이다.
    struct KeyBinding {
        int keyCode = 0;
        uint64_t buttonBit = 0;

        bool operator==(const KeyBinding&) const = default;
    };

    // 기본 바인딩: WASD + 방향키. 같은 비트를 두 키가 공유하는 것이 정상이다.
    // 반환하는 span은 정적 저장 기간을 가진 배열을 가리키므로 수명 걱정이 없다.
    [[nodiscard]] std::span<const KeyBinding> DefaultKeyBindings() noexcept;

    // 눌린 키들을 OR 해서 `InputCommand.buttonMask`를 만든다.
    //
    // ⚠️ 반대 방향 키를 동시에 누르면 **두 비트가 모두 켜진 채로 나간다.** 여기서
    //    상쇄하지 않는다. 상쇄는 `ApplyConfirmedInputs`가 +X와 -X를 더해서 0을
    //    만드는 식으로 시뮬레이션 안에서 일어난다(Gameplay/PlayerMovement.cpp).
    //    이유: 상쇄 규칙은 게임플레이 규칙이고, 게임플레이 규칙은 서버·모든 클라가
    //    똑같이 돌리는 확정 입력 경로 안에 있어야 한다. 여기서 미리 정리해 버리면
    //    "무엇이 눌렸는가"라는 사실이 클라 쪽 해석에 섞여 사라진다.
    [[nodiscard]] uint64_t BuildButtonMask(
        const KeyPressedFn& isKeyPressed,
        std::span<const KeyBinding> bindings = DefaultKeyBindings());

    // -------------------------------------------------------------------------
    // 조준
    // -------------------------------------------------------------------------

    // 이번 프레임의 마우스 이동량(화면 픽셀). y는 **화면 좌표계라 아래로 증가한다.**
    struct MouseDelta {
        float x = 0.0f;
        float y = 0.0f;
    };

    // 픽셀당 회전량. 사람이 만지는 설정값이라 float이고, 경계 바깥 값이다.
    struct AimSensitivity {
        float yawRadiansPerPixel = 0.0025f;    // 400px ≈ 1 rad. 흔한 FPS 기본값 근처.
        float pitchRadiansPerPixel = 0.0025f;

        // 마우스를 아래로 밀면 위를 보는 "인버트" 설정. 기본은 비인버트.
        bool invertPitch = false;
    };

    // 클라이언트가 들고 있는 **절대** 조준 상태.
    //
    // 왜 절대인가 (델타를 버린 이유):
    //   불변식 4 — 입력이 결측이면 서버가 **직전 입력을 hold** 해서 중계한다.
    //   조준을 델타로 실으면 hold는 "직전 회전량을 한 번 더 적용"이 되어, 패킷 하나
    //   잃을 때마다 시야가 저 혼자 더 돌아간다. 절대값이면 hold는 "시야가 그대로 멈춤"이고,
    //   다음 패킷이 도착하는 순간 정확한 값으로 복구된다. 필드 이름이 `aimYawDelta`가
    //   아니라 `aimYaw`인 것도 이 뜻이다.
    //
    // ⚠️ 이 상태는 **클라이언트 로컬**이고 월드 상태가 아니다. 스냅샷·해시 대상이 아니며
    //    (그래서 double 멤버가 있어도 결정론에 영향이 없다), 와이어에 나가는 것은
    //    `yaw`/`pitch` 두 `Fixed64`뿐이다.
    struct AimState {
        Fixed64 yaw = Fixed64::Zero();      // [-π, π]로 감긴 값
        Fixed64 pitch = Fixed64::Zero();    // 위가 +. ±PitchLimitRadians()로 잘린 값

        // 아직 Fixed64의 1 LSB에 못 미쳐 버려질 뻔한 나머지. 다음 프레임으로 이월된다.
        // 자세한 근거는 IntegrateAim 주석 참조. **절대 직렬화하지 말 것.**
        double yawResidual = 0.0;
        double pitchResidual = 0.0;

        bool operator==(const AimState&) const = default;
    };

    // 픽셀 델타 하나를 라디안 `Fixed64`로 바꾸는 경계 변환. 상태가 없는 순수 함수다.
    //
    // 반올림은 **floor**다(round가 아니다). 이유는 IntegrateAim의 나머지 이월이
    //   `radians == raw/Scale + residual`,  `residual ∈ [0, 1/Scale)`
    // 라는 한 줄 불변식으로 성립하기 때문이다. round를 쓰면 residual이 부호를 갖고
    // 0 근처에서 이월 로직이 비대칭해진다. 두 경로가 다른 반올림을 쓰면 읽는 사람이
    // 헷갈리므로 여기서도 floor로 맞춘다. 최대 오차는 1 LSB(기본 Q32.32에서 2.3e-10 rad)다.
    //
    // 이상값(NaN·무한대·비현실적으로 큰 값)은 0을 돌려준다. 근거는 .cpp 상단 참조.
    [[nodiscard]] Fixed64 PixelsToRadians(float deltaPixels, float radiansPerPixel) noexcept;

    // 조준 상태를 한 프레임 전진시킨다. 상태를 인자로 받아 새 상태를 돌려주므로
    // (제자리 수정이 아니라) 같은 (state, delta, sensitivity)면 항상 같은 결과다 —
    // 테스트가 쉬워지고, 롤백이 생겼을 때 과거 상태를 다시 굴리기도 쉽다.
    //
    // 누적 오차 처리 — **버리지 않고 이월한다.**
    //   프레임마다 `pixels * sensitivity`를 Fixed64로 내릴 때 1 LSB 미만이 잘려나간다.
    //   그 부스러기를 버리는 쪽(더 단순함)을 안 고른 이유는, 느린 마우스 조작이 통째로
    //   삼켜지는 데드존이 생기기 때문이다. 예를 들어 Fixed64를 Q16.16으로 바꾸면
    //   LSB가 1.5e-5 rad이고, 감도 0.0025에서 픽셀 6/1000 미만의 움직임은 매 프레임
    //   0으로 내려앉아 **아무리 오래 밀어도 시야가 안 돌아간다.** 나머지를 이월하면
    //   부스러기가 쌓여 결국 1 LSB를 넘고 정상적으로 돌아간다(고전적인 subpixel 누적).
    //   기본 Q32.32에서는 LSB가 2.3e-10 rad이라 실질 차이가 없지만, 고정소수점 포맷은
    //   `EF_FIXED64_FRACTIONAL_BITS`로 바꿀 수 있는 값이므로 포맷에 기대지 않는다.
    //
    //   이월용 나머지는 double이고 로컬 전용이다. 결정론과 무관하다 — 이 상태에서
    //   나가는 것은 이미 Fixed64로 확정된 `yaw`/`pitch`뿐이고, 그 값이 와이어를 탄다.
    [[nodiscard]] AimState IntegrateAim(
        const AimState& state,
        const MouseDelta& delta,
        const AimSensitivity& sensitivity) noexcept;

    // yaw를 [-π, π]로 감는다.
    //
    // ⚠️ 반열린 구간이 아니라 **닫힌** 구간인 이유: 고정소수점에서 2π의 raw 값은
    //    홀수일 수 있고(기본 Q32.32에서 실제로 홀수다), 그러면 폭이 정확히 2π이면서
    //    동시에 대칭인 반열린 구간을 만들 수가 없다(한쪽 끝이 1 LSB 어긯난다).
    //    닫힌 구간은 2π의 raw가 짝수일 때만 ±π 둘이 같은 각도를 가리키는 중복을 허용하는데,
    //    그것이 "가끔 구간 밖으로 나가는 것"보다 훨씬 덜 위험하다.
    //
    // 정확도 때문은 아니다 — 고정소수점은 float과 달리 값이 커져도 정밀도가 안 떨어지고,
    // Q32.32의 표현 범위(±2.1e9 rad)를 넘기려면 초당 100 rad로 8개월을 돌려야 한다.
    // 감는 진짜 이유는 (1) 소비자가 각도를 비교·보간할 때 정규화를 다시 안 해도 되고,
    // (2) 값이 정수부 상한에서 멀리 떨어져 있어 나중에 이 값에 곱셈이 들어가도
    // 오버플로를 걱정할 일이 없기 때문이다.
    [[nodiscard]] Fixed64 WrapAngleRadians(Fixed64 radians) noexcept;

    // pitch 상한(라디안). ±90°가 아니라 ±kPitchLimitDegrees인 이유는 아래 참조.
    inline constexpr int kPitchLimitDegrees = 89;
    [[nodiscard]] Fixed64 PitchLimitRadians() noexcept;

    // pitch를 ±PitchLimitRadians()로 자른다.
    //
    // 상한이 필요한가? — **필요하다.** yaw·pitch로 만든 시야 방향은 pitch가 ±90°에
    // 닿는 순간 위 벡터와 평행해져 카메라 기저(basis)를 만들 수 없고(짐벌), 넘어가면
    // 화면이 뒤집힌다. 마우스는 위아래로 무한히 밀 수 있으므로 어딘가에서 막아야 한다.
    // 정확히 90°가 아니라 89°인 것은 경계에서 딱 걸리는 대신 여유(1°)를 두어
    // 부동/고정소수점 오차가 어느 쪽으로 튀든 안전하게 만들기 위해서다.
    //
    // ⚠️ 여기서 자른 값이 그대로 와이어에 실리므로 **모든 peer가 이미 유효한 pitch를
    //    받는다** — 결정론상 다시 자를 필요는 없다. 그래도 시뮬레이션이 조준을 실제로
    //    소비하기 시작하면 서버가 한 번 더 잘라야 한다. 조작된 클라가 범위 밖 값을
    //    보낼 수 있고, 서버 권위형에서 그건 클라를 믿는 것이 되기 때문이다.
    //    (그 재클램프는 Fixed64 순수 연산이라 결정론에 안전하다.)
    [[nodiscard]] Fixed64 ClampPitchRadians(Fixed64 radians) noexcept;

    // 위에서 만든 조각들을 와이어 구조체 하나로 조립한다.
    // 어떤 필드가 실제로 나가는지를 한 곳에 모아 두는 것이 목적이다 —
    // `AimState`의 이월 나머지가 여기로 새어 나가지 않는다는 사실이 눈으로 보인다.
    [[nodiscard]] InputCommand MakeInputCommand(
        uint32_t clientId,
        uint64_t applyTick,
        uint32_t sequence,
        uint64_t buttonMask,
        const AimState& aim) noexcept;

}
