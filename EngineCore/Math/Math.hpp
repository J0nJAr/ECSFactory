#pragma once

#include "Base/Assert.hpp"
#include "Fixed64.hpp"

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
}
