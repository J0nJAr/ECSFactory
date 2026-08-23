#include "Gameplay/InputMapping.hpp"

#include <cmath>

#include "Math/Math.hpp"

namespace EF {
    namespace {
        // 기본 바인딩 표. 한 동작에 두 키(WASD와 방향키)가 걸려 있고, 둘 중 아무거나
        // 눌러도 같은 비트가 켜진다. 둘 다 눌러도 OR이라 결과는 같다.
        //
        // 축 방향은 ApplyConfirmedInputs와 맞춘다: +X가 오른쪽, +Z가 앞이다
        // (Gameplay/PlayerMovement.cpp의 MoveDelta).
        constexpr KeyBinding kDefaultBindings[] = {
            { Keys::kD,          kInputMoveRight },
            { Keys::kArrowRight, kInputMoveRight },
            { Keys::kA,          kInputMoveLeft },
            { Keys::kArrowLeft,  kInputMoveLeft },
            { Keys::kW,          kInputMoveForward },
            { Keys::kArrowUp,    kInputMoveForward },
            { Keys::kS,          kInputMoveBack },
            { Keys::kArrowDown,  kInputMoveBack },
        };

        // ---------------------------------------------------------------------
        // 이상값 방어
        //
        // 왜 필요한가: `Fixed64::FromDouble`은 `static_cast<int64_t>(value * Scale)`이다.
        // value가 NaN이거나 무한대면 이 캐스트는 **정의되지 않은 동작**이고, 실제로는
        // 보통 INT64_MIN이 나온다. 그 값이 조준 누산기에 한 번 들어가면 이후 모든
        // 프레임이 오염되고, 게다가 그게 `InputCommand`에 실려 서버를 거쳐 전원에게
        // 중계된다 — 내 드라이버 딸꾹질 하나가 세션 전체의 결함이 된다.
        //
        // 대응은 "무시"다. 클램프가 아니라 0으로 떨어뜨린다.
        //  - 비유한 값은 의미가 없으니 "이번 프레임엔 마우스가 안 움직였다"로 읽는다.
        //    조준 한 프레임이 빠지는 것은 사람 눈에 보이지 않는다.
        //  - 비현실적으로 큰 유한 값(alt-tab 직후의 커서 점프, raw input 글리치)도
        //    클램프가 아니라 버린다. 클램프하면 여전히 화면이 홱 돌아가는데, 그건
        //    사람 손이 만든 값이 아니므로 존중할 이유가 없다.
        //
        // assert로 죽이지 않는 이유: 이 값들은 드라이버·OS에서 오는 외부 입력이다.
        // "프로그래머 실수"라고 단정할 수 없는 값에 EF_BREAK을 걸면 디버그 빌드가
        // 하드웨어 딸꾹질에 죽는다.
        // ---------------------------------------------------------------------

        // 한 프레임에 사람이 만들 수 있는 픽셀 이동의 상한.
        // 50Hz 기준 4096px/frame = 204,800px/s. 4K 화면을 초당 50번 가로지르는 속도라
        // 어떤 플릭에이밍도 여기 못 닿는다. 이 위는 전부 글리치로 본다.
        constexpr double kMaxPlausiblePixelsPerFrame = 4096.0;

        // 감도가 비상식적으로 커도 곱셈 결과가 int64에서 놀도록 잡는 최종 안전선.
        // 64 rad ≈ 한 프레임에 10바퀴. 위의 픽셀 상한과 정상 감도라면 절대 안 닿는다.
        constexpr double kMaxAimRadiansPerFrame = 64.0;

        // 픽셀 델타 × 감도 → 라디안(double). 여기서 나온 값은 유한하고 범위 안이
        // 보장되므로, 이 뒤로는 Fixed64 변환이 안전하다.
        double SanitizeRadians(float deltaPixels, float radiansPerPixel) noexcept {
            const double pixels = static_cast<double>(deltaPixels);
            const double sensitivity = static_cast<double>(radiansPerPixel);

            // NaN·±inf는 여기서 전부 걸러진다. 이 검사가 먼저여야 아래 비교들이 의미를 갖는다
            // (NaN은 어떤 비교에도 false라서 클램프가 조용히 통과해 버린다).
            if (!std::isfinite(pixels) || !std::isfinite(sensitivity)) {
                return 0.0;
            }

            // 감도 0 이하는 호출부의 설정 실수다. 기본값으로 몰래 바꿔치기하면 버그가
            // 숨으므로, "회전 없음"으로 두어 증상이 드러나게 한다.
            if (sensitivity <= 0.0) {
                return 0.0;
            }

            if (std::fabs(pixels) > kMaxPlausiblePixelsPerFrame) {
                return 0.0;
            }

            const double radians = pixels * sensitivity;
            if (!std::isfinite(radians)) {
                return 0.0;
            }
            if (radians > kMaxAimRadiansPerFrame) {
                return kMaxAimRadiansPerFrame;
            }
            if (radians < -kMaxAimRadiansPerFrame) {
                return -kMaxAimRadiansPerFrame;
            }
            return radians;
        }

        // 라디안(double)을 Fixed64와 "아직 못 담은 나머지"로 쪼갠다.
        // 불변식: radians == value + residual, residual ∈ [0, 1/Scale) (floor를 쓰는 이유).
        //
        // residual이 음수로 새지 않는 근거: `Fixed64::Scale`은 2의 거듭제곱이라
        // double에서 `radians * Scale`과 `raw / Scale`이 지수 조작뿐이라 **정확하다**.
        // 그래서 floor가 만든 raw는 반드시 raw/Scale <= radians이고 나머지는 0 이상이다.
        // (Scale이 2의 거듭제곱이 아닌 포맷으로 바꾼다면 이 근거가 무너지니 재검토할 것.)
        struct FixedSplit {
            Fixed64 value = Fixed64::Zero();
            double residual = 0.0;
        };

        FixedSplit SplitToFixed(double radians) noexcept {
            constexpr double scale = static_cast<double>(Fixed64::Scale);

            // SanitizeRadians가 |radians| <= kMaxAimRadiansPerFrame을 보장하므로
            // radians * scale은 int64 범위 안이다(64 * 2^32 ≈ 2.7e11).
            const int64_t raw = static_cast<int64_t>(std::floor(radians * scale));

            FixedSplit split;
            split.value = Fixed64::FromRaw(raw);
            split.residual = radians - static_cast<double>(raw) / scale;
            return split;
        }
    }

    std::span<const KeyBinding> DefaultKeyBindings() noexcept {
        return std::span<const KeyBinding>(kDefaultBindings);
    }

    uint64_t BuildButtonMask(const KeyPressedFn& isKeyPressed, std::span<const KeyBinding> bindings) {
        if (!isKeyPressed) {
            return 0;
        }

        uint64_t mask = 0;
        for (const KeyBinding& binding : bindings) {
            if (isKeyPressed(binding.keyCode)) {
                mask |= binding.buttonBit;
            }
        }
        return mask;
    }

    Fixed64 PixelsToRadians(float deltaPixels, float radiansPerPixel) noexcept {
        return SplitToFixed(SanitizeRadians(deltaPixels, radiansPerPixel)).value;
    }

    Fixed64 WrapAngleRadians(Fixed64 radians) noexcept {
        const int64_t twoPi = Math::TwoPi().Raw();

        // Math::Pi()가 아니라 twoPi의 절반을 쓴다. 두 상수는 각각 따로 반올림되어
        // 만들어지므로 2*Pi() == TwoPi()가 보장되지 않고, 그 1 LSB 차이가 경계에서
        // 값을 구간 밖으로 밀어낸다.
        const int64_t pi = twoPi / 2;

        // 정수 나머지 연산이라 정확하고 O(1)이다. 반복 뺄셈으로 감으면 루프 횟수가
        // 입력 크기에 비례해서, 이상값 하나에 프레임이 멈출 수 있다.
        int64_t wrapped = radians.Raw() % twoPi;    // (-2π, 2π)
        if (wrapped > pi) {
            wrapped -= twoPi;
        } else if (wrapped < -pi) {
            wrapped += twoPi;
        }
        return Fixed64::FromRaw(wrapped);
    }

    Fixed64 PitchLimitRadians() noexcept {
        return Math::DegreesToRadians(Fixed64::FromInt(kPitchLimitDegrees));
    }

    Fixed64 ClampPitchRadians(Fixed64 radians) noexcept {
        const Fixed64 limit = PitchLimitRadians();
        return Math::Clamp(radians, -limit, limit);
    }

    AimState IntegrateAim(
        const AimState& state,
        const MouseDelta& delta,
        const AimSensitivity& sensitivity) noexcept {
        AimState next = state;

        // yaw: 화면 x가 오른쪽으로 증가하고 yaw도 오른쪽으로 증가한다(부호 그대로).
        const FixedSplit yawStep =
            SplitToFixed(SanitizeRadians(delta.x, sensitivity.yawRadiansPerPixel) + state.yawResidual);
        next.yaw = WrapAngleRadians(state.yaw + yawStep.value);
        next.yawResidual = yawStep.residual;

        // pitch: 화면 y는 **아래로** 증가하는데 pitch는 위가 +다. 그래서 한 번 뒤집는다.
        // invertPitch가 켜지면 그 뒤집기를 취소한다(= 마우스를 아래로 밀면 위를 본다).
        const float pitchPixels = sensitivity.invertPitch ? delta.y : -delta.y;
        const FixedSplit pitchStep =
            SplitToFixed(SanitizeRadians(pitchPixels, sensitivity.pitchRadiansPerPixel) + state.pitchResidual);

        // 누적된 **절대** pitch를 자른다. 델타를 자르는 게 아니라서, 상한에 붙어 있다가
        // 반대로 밀면 곧바로 그만큼 움직인다(FPS에서 기대하는 동작).
        next.pitch = ClampPitchRadians(state.pitch + pitchStep.value);

        // 상한에 걸려 값이 잘렸어도 나머지는 계속 이월한다. 이월분은 다음 프레임에
        // 다시 더해지고, 여전히 상한 밖이면 또 잘릴 뿐이라 부작용이 없다.
        next.pitchResidual = pitchStep.residual;

        return next;
    }

    InputCommand MakeInputCommand(
        uint32_t clientId,
        uint64_t applyTick,
        uint32_t sequence,
        uint64_t buttonMask,
        const AimState& aim) noexcept {
        InputCommand command;
        command.clientId = clientId;
        command.tick = applyTick;
        command.sequence = sequence;
        command.buttonMask = buttonMask;
        command.aimYaw = aim.yaw;
        command.aimPitch = aim.pitch;
        // aim.yawResidual / pitchResidual은 의도적으로 싣지 않는다.
        // 로컬 변환의 부스러기일 뿐이고, 시뮬레이션이 보는 값은 위의 두 Fixed64뿐이다.
        return command;
    }
}
