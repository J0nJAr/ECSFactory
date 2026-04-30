#include "Math/Math.hpp"
#include "TestHarness.hpp"

namespace {
    constexpr EF::Fixed64 kTolerance = EF::Fixed64::FromRaw(8);
}

TEST_CASE(Math, Vector2ArithmeticUsesFixed64Semantics) {
    const EF::Vector2 a(EF::Fixed64::FromInt(2), EF::Fixed64::FromInt(3));
    const EF::Vector2 b(EF::Fixed64::FromInt(4), EF::Fixed64::FromInt(-1));

    const EF::Vector2 sum = a + b;
    EXPECT_TRUE(sum == EF::Vector2(EF::Fixed64::FromInt(6), EF::Fixed64::FromInt(2)));

    const EF::Vector2 scaled = a * EF::Fixed64::FromInt(2);
    EXPECT_TRUE(scaled == EF::Vector2(EF::Fixed64::FromInt(4), EF::Fixed64::FromInt(6)));
}

TEST_CASE(Math, Vector3DotCrossAndDistanceProduceExpectedValues) {
    const EF::Vector3 x = EF::Vector3::UnitX();
    const EF::Vector3 y = EF::Vector3::UnitY();
    const EF::Vector3 z = EF::Vector3::UnitZ();

    EXPECT_TRUE(EF::Math::Dot(x, y) == EF::Fixed64::Zero());
    EXPECT_TRUE(EF::Math::Cross(x, y) == z);
    EXPECT_TRUE(EF::Math::DistanceSq(x, y) == EF::Fixed64::FromInt(2));
}

TEST_CASE(Math, VectorNormalizeHandlesZeroSafely) {
    EF::Vector3 normalized = EF::Vector3::One();
    const bool success = EF::Math::TryNormalize(EF::Vector3::Zero(), normalized);

    EXPECT_FALSE(success);
    EXPECT_TRUE(normalized == EF::Vector3::Zero());
}

TEST_CASE(Math, QuaternionIdentityAndConjugateBehaveCorrectly) {
    const EF::Quaternion identity = EF::Quaternion::Identity();
    const EF::Quaternion q(
        EF::Fixed64::FromInt(1),
        EF::Fixed64::FromInt(2),
        EF::Fixed64::FromInt(3),
        EF::Fixed64::FromInt(4));

    EXPECT_TRUE((identity * q) == q);
    EXPECT_TRUE((q * identity) == q);

    const EF::Quaternion conjugated = EF::Math::Conjugate(q);
    EXPECT_TRUE(conjugated == EF::Quaternion(
        EF::Fixed64::FromInt(-1),
        EF::Fixed64::FromInt(-2),
        EF::Fixed64::FromInt(-3),
        EF::Fixed64::FromInt(4)));
}

TEST_CASE(Math, QuaternionRotatesForwardAroundYAxis) {
    const EF::Quaternion yaw90 = EF::Math::FromAxisAngle(EF::Vector3::UnitY(), EF::Math::HalfPi());
    const EF::Vector3 rotated = EF::Math::RotateVector(yaw90, EF::Vector3::Forward());

    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(rotated, EF::Vector3::UnitX(), kTolerance));
}

TEST_CASE(Math, TrigMatchesCardinalAngles) {
    EXPECT_TRUE(EF::Math::Sin(EF::Fixed64::Zero()) == EF::Fixed64::Zero());
    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(EF::Math::Sin(EF::Math::HalfPi()), EF::Fixed64::One(), kTolerance));
    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(EF::Math::Cos(EF::Math::Pi()), -EF::Fixed64::One(), kTolerance));
    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(EF::Math::Sin(EF::Math::Pi()), EF::Fixed64::Zero(), EF::Fixed64::FromRaw(32)));
}

TEST_CASE(Math, SqrtMatchesPerfectSquares) {
    EXPECT_TRUE(EF::Math::Sqrt(EF::Fixed64::FromInt(0)) == EF::Fixed64::Zero());
    EXPECT_TRUE(EF::Math::Sqrt(EF::Fixed64::FromInt(4)) == EF::Fixed64::FromInt(2));
    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(
        EF::Math::Sqrt(EF::Fixed64::FromInt(2)),
        EF::Fixed64::FromDouble(1.41421356237),
        EF::Fixed64::FromRaw(4)));
}

TEST_CASE(Math, IntegrateAngularVelocityLocalProducesExpectedYaw) {
    const EF::Quaternion result = EF::Math::IntegrateAngularVelocityLocal(
        EF::Quaternion::Identity(),
        EF::Vector3(EF::Fixed64::Zero(), EF::Math::HalfPi(), EF::Fixed64::Zero()),
        EF::Fixed64::One());

    EXPECT_TRUE(EF::Math::Approx::NearlyEqual(
        EF::Math::Forward(result),
        EF::Vector3::UnitX(),
        EF::Fixed64::FromRaw(48)));
}
