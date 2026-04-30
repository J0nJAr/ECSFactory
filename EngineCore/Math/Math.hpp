#pragma once

#include <cmath>
#include <cstdint>

#include "Base/Assert.hpp"
#include "Fixed64.hpp"
#include "Quaternion.hpp"
#include "Vector.hpp"

namespace EF::Math {
    template<typename T>
    constexpr const T& Min(const T& a, const T& b) noexcept {
        return (b < a) ? b : a;
    }

    template<typename T>
    constexpr const T& Max(const T& a, const T& b) noexcept {
        return (a < b) ? b : a;
    }

    template<typename T>
    constexpr T Clamp(const T& value, const T& minValue, const T& maxValue) noexcept {
        EF_ASSERT(!(maxValue < minValue), "Clamp: minValue must be <= maxValue.");
        return Min(Max(value, minValue), maxValue);
    }

    template<typename T>
    constexpr T Abs(const T& value) noexcept {
        return (value < T{}) ? -value : value;
    }

    template<typename T>
    constexpr int Sign(const T& value) noexcept {
        if (value < T{}) {
            return -1;
        }
        if (T{} < value) {
            return 1;
        }
        return 0;
    }

    template<typename T>
    constexpr bool IsBetween(const T& value, const T& minValue, const T& maxValue) noexcept {
        return !(value < minValue) && !(maxValue < value);
    }

    template<typename T>
    constexpr T Saturate(const T& value) noexcept {
        return Clamp(value, T{}, T{1});
    }

    inline constexpr Fixed64 Saturate(const Fixed64& value) noexcept {
        return Clamp(value, Fixed64::Zero(), Fixed64::One());
    }

    inline Fixed64 Lerp(const Fixed64& a, const Fixed64& b, const Fixed64& t) noexcept {
        return a + (b - a) * t;
    }

    inline float Lerp(const float& a, const float& b, const float& t) noexcept {
        return a + (b - a) * t;
    }

    inline double Lerp(const double& a, const double& b, const double& t) noexcept {
        return a + (b - a) * t;
    }

    [[nodiscard]] Fixed64 Pi() noexcept;
    [[nodiscard]] Fixed64 HalfPi() noexcept;
    [[nodiscard]] Fixed64 TwoPi() noexcept;
    [[nodiscard]] Fixed64 DegreesToRadians(const Fixed64& degrees) noexcept;
    [[nodiscard]] Fixed64 RadiansToDegrees(const Fixed64& radians) noexcept;

    [[nodiscard]] Fixed64 Sqrt(const Fixed64& value) noexcept;
    [[nodiscard]] Fixed64 Sin(const Fixed64& radians) noexcept;
    [[nodiscard]] Fixed64 Cos(const Fixed64& radians) noexcept;

    [[nodiscard]] EF_FORCEINLINE Fixed64 Dot(const Vector2& lhs, const Vector2& rhs) noexcept {
        return lhs.x * rhs.x + lhs.y * rhs.y;
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Dot(const Vector3& lhs, const Vector3& rhs) noexcept {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Cross(const Vector3& lhs, const Vector3& rhs) noexcept {
        return Vector3(
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x
        );
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 LengthSq(const Vector2& value) noexcept {
        return Dot(value, value);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 LengthSq(const Vector3& value) noexcept {
        return Dot(value, value);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 DistanceSq(const Vector2& lhs, const Vector2& rhs) noexcept {
        return LengthSq(lhs - rhs);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 DistanceSq(const Vector3& lhs, const Vector3& rhs) noexcept {
        return LengthSq(lhs - rhs);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Length(const Vector2& value) noexcept {
        return Sqrt(LengthSq(value));
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Length(const Vector3& value) noexcept {
        return Sqrt(LengthSq(value));
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Distance(const Vector2& lhs, const Vector2& rhs) noexcept {
        return Sqrt(DistanceSq(lhs, rhs));
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Distance(const Vector3& lhs, const Vector3& rhs) noexcept {
        return Sqrt(DistanceSq(lhs, rhs));
    }

    [[nodiscard]] EF_FORCEINLINE constexpr bool IsZero(const Vector2& value) noexcept {
        return value == Vector2::Zero();
    }

    [[nodiscard]] EF_FORCEINLINE constexpr bool IsZero(const Vector3& value) noexcept {
        return value == Vector3::Zero();
    }

    [[nodiscard]] EF_FORCEINLINE constexpr bool IsZero(const Quaternion& value) noexcept {
        return value.x == Fixed64::Zero() &&
               value.y == Fixed64::Zero() &&
               value.z == Fixed64::Zero() &&
               value.w == Fixed64::Zero();
    }

    [[nodiscard]] EF_FORCEINLINE Vector2 Lerp(const Vector2& a, const Vector2& b, const Fixed64& t) noexcept {
        return a + (b - a) * t;
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Lerp(const Vector3& a, const Vector3& b, const Fixed64& t) noexcept {
        return a + (b - a) * t;
    }

    [[nodiscard]] EF_FORCEINLINE bool TryNormalize(const Vector2& value, Vector2& normalized) noexcept {
        const Fixed64 length = Length(value);
        if (length == Fixed64::Zero()) {
            normalized = Vector2::Zero();
            return false;
        }

        normalized = value / length;
        return true;
    }

    [[nodiscard]] EF_FORCEINLINE bool TryNormalize(const Vector3& value, Vector3& normalized) noexcept {
        const Fixed64 length = Length(value);
        if (length == Fixed64::Zero()) {
            normalized = Vector3::Zero();
            return false;
        }

        normalized = value / length;
        return true;
    }

    [[nodiscard]] EF_FORCEINLINE bool TryNormalize(const Quaternion& value, Quaternion& normalized) noexcept {
        const Fixed64 lengthSq = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
        if (lengthSq == Fixed64::Zero()) {
            normalized = Quaternion::Identity();
            return false;
        }

        normalized = value / Sqrt(lengthSq);
        return true;
    }

    [[nodiscard]] EF_FORCEINLINE Vector2 Normalized(const Vector2& value) noexcept {
        Vector2 normalized = Vector2::Zero();
        static_cast<void>(TryNormalize(value, normalized));
        return normalized;
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Normalized(const Vector3& value) noexcept {
        Vector3 normalized = Vector3::Zero();
        static_cast<void>(TryNormalize(value, normalized));
        return normalized;
    }

    [[nodiscard]] EF_FORCEINLINE Quaternion Normalized(const Quaternion& value) noexcept {
        Quaternion normalized = Quaternion::Identity();
        static_cast<void>(TryNormalize(value, normalized));
        return normalized;
    }

    EF_FORCEINLINE void Normalize(Vector2& value) noexcept {
        value = Normalized(value);
    }

    EF_FORCEINLINE void Normalize(Vector3& value) noexcept {
        value = Normalized(value);
    }

    EF_FORCEINLINE void Normalize(Quaternion& value) noexcept {
        value = Normalized(value);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Dot(const Quaternion& lhs, const Quaternion& rhs) noexcept {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 LengthSq(const Quaternion& value) noexcept {
        return Dot(value, value);
    }

    [[nodiscard]] EF_FORCEINLINE Fixed64 Length(const Quaternion& value) noexcept {
        return Sqrt(LengthSq(value));
    }

    [[nodiscard]] EF_FORCEINLINE constexpr Quaternion Conjugate(const Quaternion& value) noexcept {
        return Quaternion(-value.x, -value.y, -value.z, value.w);
    }

    [[nodiscard]] EF_FORCEINLINE Quaternion Inverse(const Quaternion& value) noexcept {
        const Fixed64 lengthSq = LengthSq(value);
        EF_ASSERT(lengthSq != Fixed64::Zero(), "Cannot invert a zero-length quaternion.");
        if (lengthSq == Fixed64::Zero()) {
            return Quaternion::Identity();
        }

        return Conjugate(value) / lengthSq;
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 RotateVector(const Quaternion& rotation, const Vector3& value) noexcept {
        const Quaternion pure(value.x, value.y, value.z, Fixed64::Zero());
        const Quaternion rotated = rotation * pure * Conjugate(rotation);
        return Vector3(rotated.x, rotated.y, rotated.z);
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 InverseRotateVector(const Quaternion& rotation, const Vector3& value) noexcept {
        return RotateVector(Inverse(rotation), value);
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Right(const Quaternion& rotation) noexcept {
        return RotateVector(rotation, Vector3::Right());
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Up(const Quaternion& rotation) noexcept {
        return RotateVector(rotation, Vector3::Up());
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 Forward(const Quaternion& rotation) noexcept {
        return RotateVector(rotation, Vector3::Forward());
    }

    [[nodiscard]] EF_FORCEINLINE Quaternion FromAxisAngle(const Vector3& axis, const Fixed64& angleRadians) noexcept {
        Vector3 normalizedAxis = Vector3::Zero();
        if (!TryNormalize(axis, normalizedAxis)) {
            return Quaternion::Identity();
        }

        const Fixed64 halfAngle = angleRadians / Fixed64::FromInt(2);
        const Fixed64 sinHalf = Sin(halfAngle);
        const Fixed64 cosHalf = Cos(halfAngle);
        return Quaternion(
            normalizedAxis.x * sinHalf,
            normalizedAxis.y * sinHalf,
            normalizedAxis.z * sinHalf,
            cosHalf
        );
    }

    [[nodiscard]] EF_FORCEINLINE Quaternion IntegrateAngularVelocityLocal(
        const Quaternion& currentRotation,
        const Vector3& angularVelocityLocal,
        const Fixed64& deltaTime) noexcept {
        const Vector3 omega = angularVelocityLocal * deltaTime;
        const Fixed64 angle = Length(omega);
        if (angle == Fixed64::Zero()) {
            return currentRotation;
        }

        const Quaternion delta = FromAxisAngle(omega / angle, angle);
        return Normalized(currentRotation * delta);
    }

    namespace Approx {
        [[nodiscard]] Quaternion FromEulerRadians(const Vector3& eulerRadians) noexcept;
        [[nodiscard]] Quaternion FromEulerDegrees(const Vector3& eulerDegrees) noexcept;
        [[nodiscard]] Vector3 ToEulerRadians(const Quaternion& value) noexcept;
        [[nodiscard]] Quaternion Lerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept;
        [[nodiscard]] Quaternion Nlerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept;
        [[nodiscard]] Quaternion Slerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept;
        [[nodiscard]] Quaternion LookRotation(const Vector3& forward, const Vector3& upHint = Vector3::UnitY()) noexcept;
        [[nodiscard]] bool NearlyEqual(const Fixed64& lhs, const Fixed64& rhs, const Fixed64& tolerance) noexcept;
        [[nodiscard]] bool NearlyEqual(const Vector3& lhs, const Vector3& rhs, const Fixed64& tolerance) noexcept;
        [[nodiscard]] bool NearlyEqual(const Quaternion& lhs, const Quaternion& rhs, const Fixed64& tolerance) noexcept;
    }
}
