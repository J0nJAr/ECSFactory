#include "TestHarness.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <type_traits>

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "ECS/World.hpp"
#include "Gameplay/InputMapping.hpp"
#include "Gameplay/PlayerMovement.hpp"
#include "Math/Math.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Vector.hpp"
#include "Network/InputCommand.hpp"

namespace {
    // 눌린 키 집합을 흉내내는 가짜 키보드.
    //
    // 이 테스트 파일에 창도 GPU도 windows.h도 등장하지 않는다는 점이 요점이다 —
    // `KeyPressedFn`이 std::function이라 캡처 람다 하나면 되고, 테스트끼리 공유하는
    // 전역 상태가 없어서 실행 순서에 의존하지 않는다.
    EF::KeyPressedFn Pressed(std::initializer_list<int> keys) {
        const std::set<int> down(keys);
        return [down](int keyCode) { return down.count(keyCode) != 0; };
    }

    // Fixed64는 ostream 출력이 없어서 EXPECT_EQ(스트리밍)를 못 쓴다. 비교는 EXPECT_TRUE로.
    bool NearlyEqualRaw(EF::Fixed64 lhs, EF::Fixed64 rhs, int64_t toleranceLsb) {
        const int64_t diff = lhs.Raw() - rhs.Raw();
        return (diff < 0 ? -diff : diff) <= toleranceLsb;
    }

    // 고정소수점 1 LSB에 해당하는 라디안. 포맷(EF_FIXED64_FRACTIONAL_BITS)이 바뀌어도
    // 테스트가 따라가도록 Scale에서 유도한다.
    double LsbRadians() {
        return 1.0 / static_cast<double>(EF::Fixed64::Scale);
    }

    // 감도(float)를 거쳐 온 값의 허용오차. float의 상대오차가 ~1.2e-7이라 1 rad 근처에서
    // Q32.32 LSB의 수백 배가 된다 — "1 LSB 이내"로 잡으면 감도가 정확한 구현도 떨어진다.
    // 그래서 절대오차 1e-6 rad(≈ 0.00006°)로 잡는다. 포맷이 거칠면 최소 1 LSB.
    int64_t SensitivityToleranceRaw() {
        const int64_t derived = EF::Fixed64::Scale / 1000000;
        return derived > 0 ? derived : 1;
    }

    EF::World MakeWorldWithPlayer(uint32_t clientId) {
        EF::World world;
        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(0, 0, 0),
            .rotation = EF::Quaternion::Identity()
        });
        world.AddComponent<EF::PlayerControl>(entity, EF::PlayerControl{ .clientId = clientId });
        return world;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. 키 → 비트마스크
// ─────────────────────────────────────────────────────────────────────────────

// WASD와 방향키가 **같은** 비트를 만든다. 바인딩 표가 한 동작에 여러 키를 매다는
// 구조인지, 아니면 한 벌만 지원하는지를 여기서 가른다.
TEST_CASE(InputMapping, WasdAndArrowKeysProduceTheSameMoveBits) {
    using namespace EF;

    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kW})), kInputMoveForward);
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kArrowUp})), kInputMoveForward);

    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kS})), kInputMoveBack);
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kArrowDown})), kInputMoveBack);

    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kA})), kInputMoveLeft);
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kArrowLeft})), kInputMoveLeft);

    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kD})), kInputMoveRight);
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kArrowRight})), kInputMoveRight);

    // 같은 동작의 두 키를 동시에 눌러도 OR이라 결과가 같다(비트가 두 번 켜지지 않는다).
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kW, Keys::kArrowUp})), kInputMoveForward);

    // 아무것도 안 누르면 0. 바인딩되지 않은 키는 마스크에 영향이 없다.
    EXPECT_EQ(BuildButtonMask(Pressed({})), static_cast<uint64_t>(0));
    EXPECT_EQ(BuildButtonMask(Pressed({0x1B /* VK_ESCAPE */, 0x20 /* VK_SPACE */})),
              static_cast<uint64_t>(0));
}

// 대각선: 서로 다른 축의 키 두 개는 비트 두 개를 켠다.
TEST_CASE(InputMapping, DiagonalCombinationSetsBothAxisBits) {
    using namespace EF;

    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kW, Keys::kD})),
              kInputMoveForward | kInputMoveRight);
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kArrowDown, Keys::kArrowLeft})),
              kInputMoveBack | kInputMoveLeft);

    // 네 방향을 다 눌러도 네 비트가 전부 나간다 — 매핑은 아무것도 해석하지 않는다.
    EXPECT_EQ(BuildButtonMask(Pressed({Keys::kW, Keys::kA, Keys::kS, Keys::kD})),
              kInputMoveForward | kInputMoveLeft | kInputMoveBack | kInputMoveRight);
}

// 반대 키를 동시에 누르면?
//
// 매핑 계층은 **상쇄하지 않고 두 비트를 다 켠 채로 내보낸다.** 그리고 상쇄는
// 시뮬레이션(`ApplyConfirmedInputs`)이 +X와 -X를 더해 0을 만드는 방식으로 일어난다.
// 이게 맞는 이유: 상쇄는 게임플레이 규칙이고, 게임플레이 규칙은 서버와 모든 클라가
// 똑같이 돌리는 확정 입력 경로 안에 있어야 한다. 클라가 미리 정리해서 보내면
// "무엇이 눌렸는가"라는 사실이 클라별 해석에 섞여 사라진다.
//
// 이 테스트는 그 두 단계를 한 줄기로 이어서 못 박는다.
TEST_CASE(InputMapping, OppositeKeysKeepBothBitsAndCancelInsideTheSimulation) {
    using namespace EF;

    const uint64_t mask = BuildButtonMask(Pressed({Keys::kA, Keys::kD}));

    // 1단계: 매핑은 상쇄하지 않는다.
    EXPECT_EQ(mask, kInputMoveLeft | kInputMoveRight);
    EXPECT_TRUE((mask & kInputMoveLeft) != 0);
    EXPECT_TRUE((mask & kInputMoveRight) != 0);

    // 2단계: 시뮬레이션이 상쇄한다.
    World world = MakeWorldWithPlayer(7);
    const AimState aim{};
    const std::array<InputCommand, 1> inputs{ MakeInputCommand(7, 0, 0, mask, aim) };
    ApplyConfirmedInputs(world, inputs);

    auto view = world.CreateView<Transform>();
    view.ForEach([](EntityIndex, Transform& transform) {
        EXPECT_TRUE(transform.position == Vector3(0, 0, 0));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. 조준 — 경계 변환의 재현성
// ─────────────────────────────────────────────────────────────────────────────

// 같은 입력이면 항상 같은 Fixed64가 나온다(같은 바이너리 안에서). 그리고 값 자체가
// 기대한 크기다 — 감도를 곱하는 걸 잊거나 축을 뒤바꾼 구현이 여기서 걸린다.
TEST_CASE(InputMapping, SamePixelDeltaAlwaysProducesTheSameFixed64) {
    using namespace EF;

    const Fixed64 first = PixelsToRadians(100.0f, 0.01f);
    const Fixed64 second = PixelsToRadians(100.0f, 0.01f);
    EXPECT_TRUE(first.Raw() == second.Raw());

    // 100px * 0.01 rad/px = 1 rad. (0.01f는 이진수로 정확히 표현되지 않으므로
    // 허용오차는 1 LSB가 아니라 float 상대오차에서 온 SensitivityToleranceRaw()다.)
    EXPECT_TRUE(NearlyEqualRaw(first, Fixed64::One(), SensitivityToleranceRaw()));

    // 부호가 그대로 따라간다.
    EXPECT_TRUE(NearlyEqualRaw(PixelsToRadians(-100.0f, 0.01f), -Fixed64::One(),
                               SensitivityToleranceRaw()));

    // 0 픽셀은 정확히 0이다(floor가 -1 LSB를 만들지 않는지 확인).
    EXPECT_TRUE(PixelsToRadians(0.0f, 0.01f) == Fixed64::Zero());
}

// 같은 델타 열(列)을 서로 다른 두 AimState에 먹이면 비트 단위로 같은 결과가 나온다.
// "클라마다 미세하게 달라도 된다"는 것은 **와이어를 타기 때문**이지, 같은 코드가
// 제멋대로여도 된다는 뜻이 아니다. 그 최소 보장을 여기서 잡는다.
TEST_CASE(InputMapping, IntegrateAimIsReproducibleForTheSameDeltaSequence) {
    using namespace EF;

    const AimSensitivity sensitivity{};
    const std::array<MouseDelta, 5> deltas{
        MouseDelta{ 3.5f, -2.25f },
        MouseDelta{ -11.0f, 7.75f },
        MouseDelta{ 0.125f, 0.0f },
        MouseDelta{ 60.0f, 40.0f },
        MouseDelta{ -0.5f, -0.5f },
    };

    AimState a{};
    AimState b{};
    for (const MouseDelta& delta : deltas) {
        a = IntegrateAim(a, delta, sensitivity);
        b = IntegrateAim(b, delta, sensitivity);
    }

    EXPECT_TRUE(a.yaw.Raw() == b.yaw.Raw());
    EXPECT_TRUE(a.pitch.Raw() == b.pitch.Raw());
    EXPECT_TRUE(a == b);

    // 마우스를 오른쪽으로 순수하게 밀면 yaw가 +로 간다. 그리고 화면 y는 아래로
    // 증가하는데 pitch는 위가 +이므로, +y 델타는 pitch를 -로 내려야 한다.
    const AimState right = IntegrateAim(AimState{}, MouseDelta{ 100.0f, 0.0f }, sensitivity);
    EXPECT_TRUE(right.yaw > Fixed64::Zero());

    const AimState down = IntegrateAim(AimState{}, MouseDelta{ 0.0f, 100.0f }, sensitivity);
    EXPECT_TRUE(down.pitch < Fixed64::Zero());

    // invertPitch를 켜면 정확히 반대가 된다.
    AimSensitivity inverted = sensitivity;
    inverted.invertPitch = true;
    const AimState invertedDown = IntegrateAim(AimState{}, MouseDelta{ 0.0f, 100.0f }, inverted);
    EXPECT_TRUE(invertedDown.pitch > Fixed64::Zero());
}

// 누적 오차 처리 — 1 LSB에 못 미치는 미세한 움직임이 **영원히 삼켜지지 않는다.**
//
// 나머지를 버리는 구현이면 한 프레임에 0이 나오고 몇 번을 반복해도 계속 0이다(데드존).
// 이월하는 구현이면 부스러기가 쌓여 결국 1 LSB를 넘는다. 그 차이를 잡는 테스트다.
// 값은 Fixed64::Scale에서 유도하므로 고정소수점 포맷을 바꿔도 그대로 유효하다.
TEST_CASE(InputMapping, SubLsbMotionIsCarriedInsteadOfBeingDiscarded) {
    using namespace EF;

    // 프레임당 LSB의 1/100만큼만 도는 감도.
    AimSensitivity sensitivity{};
    sensitivity.yawRadiansPerPixel = static_cast<float>(LsbRadians() / 100.0);
    const MouseDelta delta{ 1.0f, 0.0f };

    // 한 프레임만으로는 1 LSB에 못 미쳐 각도가 0이다.
    const AimState afterOne = IntegrateAim(AimState{}, delta, sensitivity);
    EXPECT_TRUE(afterOne.yaw == Fixed64::Zero());
    EXPECT_TRUE(afterOne.yawResidual > 0.0);   // 버려지지 않고 남아 있다

    // 100프레임을 넘기면 부스러기가 1 LSB를 채워 실제로 돌아간다.
    AimState state{};
    for (int i = 0; i < 150; ++i) {
        state = IntegrateAim(state, delta, sensitivity);
    }
    EXPECT_TRUE(state.yaw > Fixed64::Zero());

    // 이월분은 항상 1 LSB 미만으로 유지된다(무한정 쌓이지 않는다).
    EXPECT_TRUE(state.yawResidual < LsbRadians());
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. 조준 — 범위 규칙
// ─────────────────────────────────────────────────────────────────────────────

// pitch 상한: 마우스를 아무리 밀어도 ±89°를 넘지 않고, 반대로 밀면 곧바로 풀린다.
// (델타가 아니라 누적된 절대값을 자르기 때문에 "붙어 있다가 즉시 반응"이 나온다.)
TEST_CASE(InputMapping, PitchIsClampedToTheConfiguredLimit) {
    using namespace EF;

    const AimSensitivity sensitivity{};
    const Fixed64 limit = PitchLimitRadians();

    AimState up{};
    for (int i = 0; i < 200; ++i) {
        up = IntegrateAim(up, MouseDelta{ 0.0f, -500.0f }, sensitivity);   // 계속 위로
    }
    EXPECT_TRUE(up.pitch == limit);

    AimState down{};
    for (int i = 0; i < 200; ++i) {
        down = IntegrateAim(down, MouseDelta{ 0.0f, 500.0f }, sensitivity);
    }
    EXPECT_TRUE(down.pitch == -limit);

    // 상한에 붙은 상태에서 반대로 조금 밀면 그만큼 즉시 내려온다.
    const AimState released = IntegrateAim(up, MouseDelta{ 0.0f, 100.0f }, sensitivity);
    EXPECT_TRUE(released.pitch < limit);

    // 상한 자체는 90°보다 작다 — 짐벌 경계에 딱 붙지 않기 위한 여유.
    EXPECT_TRUE(limit < Math::HalfPi());
    EXPECT_TRUE(ClampPitchRadians(Math::Pi()) == limit);
    EXPECT_TRUE(ClampPitchRadians(-Math::Pi()) == -limit);
    EXPECT_TRUE(ClampPitchRadians(Fixed64::Zero()) == Fixed64::Zero());
}

// yaw는 감긴다. 몇 바퀴를 돌려도 [-π, π] 안에 있고, 한 바퀴 차이는 같은 각도다.
//
// 구간이 반열린 [-π, π)가 아니라 닫힌 [-π, π]인 이유는 WrapAngleRadians 주석 참조 —
// 2π의 raw 값이 홀수라 대칭이면서 폭이 정확히 2π인 반열린 구간이 존재하지 않는다.
TEST_CASE(InputMapping, YawWrapsIntoCanonicalRange) {
    using namespace EF;

    const Fixed64 pi = Fixed64::FromRaw(Math::TwoPi().Raw() / 2);

    AimState state{};
    for (int i = 0; i < 500; ++i) {
        state = IntegrateAim(state, MouseDelta{ 400.0f, 0.0f }, AimSensitivity{});
    }
    EXPECT_TRUE(state.yaw >= -pi);
    EXPECT_TRUE(state.yaw <= pi);

    // 감기 함수 자체의 경계 조건.
    EXPECT_TRUE(WrapAngleRadians(Fixed64::Zero()) == Fixed64::Zero());
    EXPECT_TRUE(WrapAngleRadians(Math::TwoPi()) == Fixed64::Zero());
    EXPECT_TRUE(WrapAngleRadians(-Math::TwoPi()) == Fixed64::Zero());
    EXPECT_TRUE(WrapAngleRadians(pi) == pi);            // 양 끝은 그대로 남는다
    EXPECT_TRUE(WrapAngleRadians(-pi) == -pi);
    EXPECT_TRUE(NearlyEqualRaw(WrapAngleRadians(Math::TwoPi() + Fixed64::One()), Fixed64::One(), 1));

    // 범위를 재는 것만으로는 "아무것도 안 하는" 구현도 통과한다.
    // 한 바퀴 차이가 실제로 같은 각도로 접히는지까지 본다.
    const Fixed64 someAngle = Fixed64::One();
    EXPECT_TRUE(NearlyEqualRaw(WrapAngleRadians(someAngle + Math::TwoPi()),
                               WrapAngleRadians(someAngle), 1));
    EXPECT_TRUE(NearlyEqualRaw(WrapAngleRadians(someAngle - Math::TwoPi()),
                               WrapAngleRadians(someAngle), 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. 이상값 방어
//
// `Fixed64::FromDouble`은 `static_cast<int64_t>(value * Scale)`이라 NaN·무한대에서
// 정의되지 않은 동작이다(실측으로는 보통 INT64_MIN). 그 값이 누산기에 한 번 들어가면
// 이후 모든 프레임이 오염되고, 게다가 InputCommand에 실려 전원에게 중계된다.
// 그래서 경계에서 막는다. 이 테스트가 그 방어선이다.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE(InputMapping, NonFiniteAndImplausibleInputsAreIgnored) {
    using namespace EF;

    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    constexpr float kInf = std::numeric_limits<float>::infinity();
    const AimSensitivity sensitivity{};

    // 경계 변환 자체가 0을 낸다(INT64_MIN이 아니라).
    EXPECT_TRUE(PixelsToRadians(kNaN, 0.01f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(kInf, 0.01f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(-kInf, 0.01f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(1.0f, kNaN) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(1.0f, kInf) == Fixed64::Zero());

    // 감도가 0 이하인 것은 호출부 설정 실수다. 기본값으로 몰래 바꾸지 않고 0을 낸다.
    EXPECT_TRUE(PixelsToRadians(100.0f, 0.0f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(100.0f, -0.01f) == Fixed64::Zero());

    // 사람 손이 만들 수 없는 크기는 클램프가 아니라 폐기한다 — 클램프하면 여전히
    // 화면이 홱 돌아가는데, 그건 존중할 값이 아니기 때문이다.
    EXPECT_TRUE(PixelsToRadians(1.0e9f, 0.01f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(-1.0e30f, 0.01f) == Fixed64::Zero());
    EXPECT_TRUE(PixelsToRadians(std::numeric_limits<float>::max(), 0.01f) == Fixed64::Zero());

    // 누산기도 오염되지 않는다: 정상 프레임 → 이상 프레임 → 값이 그대로여야 한다.
    const AimState healthy = IntegrateAim(AimState{}, MouseDelta{ 40.0f, 25.0f }, sensitivity);
    for (const MouseDelta& bad : { MouseDelta{ kNaN, kNaN },
                                   MouseDelta{ kInf, -kInf },
                                   MouseDelta{ 1.0e30f, -1.0e30f },
                                   MouseDelta{ kNaN, 0.0f } }) {
        const AimState after = IntegrateAim(healthy, bad, sensitivity);
        EXPECT_TRUE(after.yaw.Raw() == healthy.yaw.Raw());
        EXPECT_TRUE(after.pitch.Raw() == healthy.pitch.Raw());
        EXPECT_TRUE(std::isfinite(after.yawResidual));
        EXPECT_TRUE(std::isfinite(after.pitchResidual));
    }

    // 감도 쪽이 망가져도 마찬가지.
    AimSensitivity broken{};
    broken.yawRadiansPerPixel = kNaN;
    broken.pitchRadiansPerPixel = -1.0f;
    const AimState afterBroken = IntegrateAim(healthy, MouseDelta{ 40.0f, 25.0f }, broken);
    EXPECT_TRUE(afterBroken.yaw.Raw() == healthy.yaw.Raw());
    EXPECT_TRUE(afterBroken.pitch.Raw() == healthy.pitch.Raw());
}

// ─────────────────────────────────────────────────────────────────────────────
// 5. 조립
// ─────────────────────────────────────────────────────────────────────────────

// 와이어로 나가는 것은 Fixed64 두 개뿐이다 — 이월 나머지(double)는 새어 나가지 않는다.
// 이게 "결정론 경계"가 실제로 지켜진다는 뜻이다.
TEST_CASE(InputMapping, MakeInputCommandCarriesOnlyFixedPointAim) {
    using namespace EF;

    // 먼저 일반 감도로 실제 각도를 만든다.
    AimState aim = IntegrateAim(AimState{}, MouseDelta{ 33.0f, -17.0f }, AimSensitivity{});
    EXPECT_TRUE(aim.yaw != Fixed64::Zero());
    EXPECT_TRUE(aim.pitch != Fixed64::Zero());

    // 그 다음 1 LSB에 못 미치는 한 프레임을 더해 이월 나머지를 실제로 남긴다.
    //
    // 기본 감도(0.0025f)로는 나머지가 안 생긴다 — 재미있는 우연인데,
    // 0.0025f는 float에서 정확히 M × 2^-32 꼴이라 정수 픽셀을 곱하면
    // Q32.32 격자에 딜 맞게 떨어진다. 그래서 일부러 더 잘게 감는 감도를 쓴다.
    AimSensitivity fine{};
    fine.yawRadiansPerPixel = static_cast<float>(LsbRadians() / 100.0);
    fine.pitchRadiansPerPixel = fine.yawRadiansPerPixel;
    aim = IntegrateAim(aim, MouseDelta{ 1.0f, 1.0f }, fine);
    EXPECT_TRUE(aim.yawResidual != 0.0);
    EXPECT_TRUE(aim.pitchResidual != 0.0);

    const uint64_t mask = BuildButtonMask(Pressed({Keys::kW, Keys::kD}));
    const InputCommand command = MakeInputCommand(42, 1234, 7, mask, aim);

    EXPECT_EQ(command.clientId, static_cast<uint32_t>(42));
    EXPECT_EQ(command.tick, static_cast<uint64_t>(1234));
    EXPECT_EQ(command.sequence, static_cast<uint32_t>(7));
    EXPECT_EQ(command.buttonMask, kInputMoveForward | kInputMoveRight);
    EXPECT_TRUE(command.aimYaw.Raw() == aim.yaw.Raw());
    EXPECT_TRUE(command.aimPitch.Raw() == aim.pitch.Raw());

    // InputCommand는 trivially-copyable해야 와이어·스냅샷 경로에서 안전하다.
    // (double 나머지가 여기 딸려왔다면 그 자체로는 안 깨지지만, 위 두 줄이 실패한다.)
    static_assert(std::is_trivially_copyable_v<InputCommand>,
                  "InputCommand는 raw 바이트로 다뤄지는 와이어 구조체다.");
}
