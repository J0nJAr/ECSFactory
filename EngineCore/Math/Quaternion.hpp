#pragma once

#include <compare>
#include <cstdint>

#include "Base/Defines.hpp"
#include "Fixed64.hpp"
#include "Vector.hpp"

namespace EF {
    struct Quaternion {
        Fixed64 x = Fixed64::Zero();
        Fixed64 y = Fixed64::Zero();
        Fixed64 z = Fixed64::Zero();
        Fixed64 w = Fixed64::One();

        constexpr Quaternion() noexcept = default;
        constexpr Quaternion(Fixed64 xValue, Fixed64 yValue, Fixed64 zValue, Fixed64 wValue) noexcept
            : x(xValue), y(yValue), z(zValue), w(wValue) {}
        explicit constexpr Quaternion(int32_t xValue, int32_t yValue, int32_t zValue, int32_t wValue) noexcept
            : x(xValue), y(yValue), z(zValue), w(wValue) {}

        [[nodiscard]] EF_FORCEINLINE static constexpr Quaternion Identity() noexcept {
            return Quaternion(Fixed64::Zero(), Fixed64::Zero(), Fixed64::Zero(), Fixed64::One());
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Quaternion operator+(const Quaternion& rhs) const noexcept {
            return Quaternion(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Quaternion operator-(const Quaternion& rhs) const noexcept {
            return Quaternion(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Quaternion operator-() const noexcept {
            return Quaternion(-x, -y, -z, -w);
        }

        [[nodiscard]] EF_FORCEINLINE Quaternion operator*(const Fixed64& scalar) const noexcept {
            return Quaternion(x * scalar, y * scalar, z * scalar, w * scalar);
        }

        [[nodiscard]] EF_FORCEINLINE Quaternion operator/(const Fixed64& scalar) const noexcept {
            return Quaternion(x / scalar, y / scalar, z / scalar, w / scalar);
        }

        [[nodiscard]] EF_FORCEINLINE Quaternion operator*(const Quaternion& rhs) const noexcept {
            return Quaternion(
                w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
                w * rhs.y - x * rhs.z + y * rhs.w + z * rhs.x,
                w * rhs.z + x * rhs.y - y * rhs.x + z * rhs.w,
                w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z
            );
        }

        EF_FORCEINLINE constexpr Quaternion& operator+=(const Quaternion& rhs) noexcept {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            w += rhs.w;
            return *this;
        }

        EF_FORCEINLINE constexpr Quaternion& operator-=(const Quaternion& rhs) noexcept {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
            w -= rhs.w;
            return *this;
        }

        EF_FORCEINLINE Quaternion& operator*=(const Fixed64& scalar) noexcept {
            x *= scalar;
            y *= scalar;
            z *= scalar;
            w *= scalar;
            return *this;
        }

        EF_FORCEINLINE Quaternion& operator/=(const Fixed64& scalar) noexcept {
            x /= scalar;
            y /= scalar;
            z /= scalar;
            w /= scalar;
            return *this;
        }

        EF_FORCEINLINE Quaternion& operator*=(const Quaternion& rhs) noexcept {
            *this = *this * rhs;
            return *this;
        }

        auto operator<=>(const Quaternion& rhs) const noexcept = default;
    };

    [[nodiscard]] EF_FORCEINLINE Quaternion operator*(const Fixed64& scalar, const Quaternion& value) noexcept {
        return value * scalar;
    }
}
