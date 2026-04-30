#pragma once

#include <compare>
#include <cstdint>

#include "Base/Defines.hpp"
#include "Fixed64.hpp"

namespace EF {
    struct Vector2 {
        Fixed64 x = Fixed64::Zero();
        Fixed64 y = Fixed64::Zero();

        constexpr Vector2() noexcept = default;
        constexpr Vector2(Fixed64 xValue, Fixed64 yValue) noexcept : x(xValue), y(yValue) {}
        explicit constexpr Vector2(int32_t xValue, int32_t yValue) noexcept
            : x(xValue), y(yValue) {}

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector2 Zero() noexcept {
            return Vector2(Fixed64::Zero(), Fixed64::Zero());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector2 One() noexcept {
            return Vector2(Fixed64::One(), Fixed64::One());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector2 UnitX() noexcept {
            return Vector2(Fixed64::One(), Fixed64::Zero());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector2 UnitY() noexcept {
            return Vector2(Fixed64::Zero(), Fixed64::One());
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector2 operator+(const Vector2& rhs) const noexcept {
            return Vector2(x + rhs.x, y + rhs.y);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector2 operator-(const Vector2& rhs) const noexcept {
            return Vector2(x - rhs.x, y - rhs.y);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector2 operator-() const noexcept {
            return Vector2(-x, -y);
        }

        [[nodiscard]] EF_FORCEINLINE Vector2 operator*(const Fixed64& scalar) const noexcept {
            return Vector2(x * scalar, y * scalar);
        }

        [[nodiscard]] EF_FORCEINLINE Vector2 operator/(const Fixed64& scalar) const noexcept {
            return Vector2(x / scalar, y / scalar);
        }

        EF_FORCEINLINE constexpr Vector2& operator+=(const Vector2& rhs) noexcept {
            x += rhs.x;
            y += rhs.y;
            return *this;
        }

        EF_FORCEINLINE constexpr Vector2& operator-=(const Vector2& rhs) noexcept {
            x -= rhs.x;
            y -= rhs.y;
            return *this;
        }

        EF_FORCEINLINE Vector2& operator*=(const Fixed64& scalar) noexcept {
            x *= scalar;
            y *= scalar;
            return *this;
        }

        EF_FORCEINLINE Vector2& operator/=(const Fixed64& scalar) noexcept {
            x /= scalar;
            y /= scalar;
            return *this;
        }

        auto operator<=>(const Vector2& rhs) const noexcept = default;
    };

    struct Vector3 {
        Fixed64 x = Fixed64::Zero();
        Fixed64 y = Fixed64::Zero();
        Fixed64 z = Fixed64::Zero();

        constexpr Vector3() noexcept = default;
        constexpr Vector3(Fixed64 xValue, Fixed64 yValue, Fixed64 zValue) noexcept
            : x(xValue), y(yValue), z(zValue) {}
        explicit constexpr Vector3(int32_t xValue, int32_t yValue, int32_t zValue) noexcept
            : x(xValue), y(yValue), z(zValue) {}

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 Zero() noexcept {
            return Vector3(Fixed64::Zero(), Fixed64::Zero(), Fixed64::Zero());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 One() noexcept {
            return Vector3(Fixed64::One(), Fixed64::One(), Fixed64::One());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 UnitX() noexcept {
            return Vector3(Fixed64::One(), Fixed64::Zero(), Fixed64::Zero());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 UnitY() noexcept {
            return Vector3(Fixed64::Zero(), Fixed64::One(), Fixed64::Zero());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 UnitZ() noexcept {
            return Vector3(Fixed64::Zero(), Fixed64::Zero(), Fixed64::One());
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 Right() noexcept {
            return UnitX();
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 Up() noexcept {
            return UnitY();
        }

        [[nodiscard]] EF_FORCEINLINE static constexpr Vector3 Forward() noexcept {
            return Vector3(Fixed64::Zero(), Fixed64::Zero(), Fixed64(MathForwardSign));
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector3 operator+(const Vector3& rhs) const noexcept {
            return Vector3(x + rhs.x, y + rhs.y, z + rhs.z);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector3 operator-(const Vector3& rhs) const noexcept {
            return Vector3(x - rhs.x, y - rhs.y, z - rhs.z);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Vector3 operator-() const noexcept {
            return Vector3(-x, -y, -z);
        }

        [[nodiscard]] EF_FORCEINLINE Vector3 operator*(const Fixed64& scalar) const noexcept {
            return Vector3(x * scalar, y * scalar, z * scalar);
        }

        [[nodiscard]] EF_FORCEINLINE Vector3 operator/(const Fixed64& scalar) const noexcept {
            return Vector3(x / scalar, y / scalar, z / scalar);
        }

        EF_FORCEINLINE constexpr Vector3& operator+=(const Vector3& rhs) noexcept {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            return *this;
        }

        EF_FORCEINLINE constexpr Vector3& operator-=(const Vector3& rhs) noexcept {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            return *this;
        }

        EF_FORCEINLINE Vector3& operator*=(const Fixed64& scalar) noexcept {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            return *this;
        }

        EF_FORCEINLINE Vector3& operator/=(const Fixed64& scalar) noexcept {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            return *this;
        }

        auto operator<=>(const Vector3& rhs) const noexcept = default;
    };

    [[nodiscard]] EF_FORCEINLINE Vector2 operator*(const Fixed64& scalar, const Vector2& value) noexcept {
        return value * scalar;
    }

    [[nodiscard]] EF_FORCEINLINE Vector3 operator*(const Fixed64& scalar, const Vector3& value) noexcept {
        return value * scalar;
    }
}
