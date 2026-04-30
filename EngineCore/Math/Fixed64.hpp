#pragma once

#include <compare>
#include <cstdint>

#include "Base/Defines.hpp"
#include "Internal.hpp"

namespace EF {

    class Fixed64 {
    public:
        static constexpr int32_t FractionalBits = Fixed64FractionalBits;
        static constexpr int64_t Scale = Fixed64Scale;

    public:
        constexpr Fixed64() noexcept = default;

        explicit constexpr Fixed64(int32_t value) noexcept
            : m_value(static_cast<int64_t>(value) << FractionalBits) {}

        EF_FORCEINLINE static constexpr Fixed64 FromRaw(int64_t raw) noexcept {
            Fixed64 v;
            v.m_value = raw;
            return v;
        }

        EF_FORCEINLINE static constexpr Fixed64 FromInt(int32_t value) noexcept {
            return FromRaw(static_cast<int64_t>(value) << FractionalBits);
        }

        /*
        * 실수에서 변환 (초기화 혹은 렌더링 시에만 권장)
        */
        EF_FORCEINLINE static Fixed64 FromFloat(float value) noexcept {
            return FromDouble(static_cast<double>(value));
        }

        EF_FORCEINLINE static Fixed64 FromDouble(double value) noexcept {
            return FromRaw(static_cast<int64_t>(value * static_cast<double>(Scale)));
        }



    public:
        [[nodiscard]] EF_FORCEINLINE constexpr int64_t Raw() const noexcept {
            return m_value;
        }

        [[nodiscard]] EF_FORCEINLINE constexpr float ToFloat() const noexcept {
            return static_cast<float>(m_value) / static_cast<float>(Scale);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr double ToDouble() const noexcept {
            return static_cast<double>(m_value) / static_cast<double>(Scale);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr int32_t ToIntFloor() const noexcept {
            return static_cast<int32_t>(m_value >> FractionalBits);
        }

    public:
        [[nodiscard]] EF_FORCEINLINE constexpr Fixed64 operator+(const Fixed64& rhs) const noexcept {
            return FromRaw(m_value + rhs.m_value);
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Fixed64 operator-(const Fixed64& rhs) const noexcept {
            return FromRaw(m_value - rhs.m_value);
        }

        [[nodiscard]] EF_FORCEINLINE Fixed64 operator*(const Fixed64& rhs) const noexcept {
            return FromRaw(Internal::MulQ32(m_value, rhs.m_value));
        }

        [[nodiscard]] EF_FORCEINLINE Fixed64 operator/(const Fixed64& rhs) const noexcept {
            return FromRaw(Internal::DivQ32(m_value, rhs.m_value));
        }

        [[nodiscard]] EF_FORCEINLINE constexpr Fixed64 operator-() const noexcept {
            return FromRaw(-m_value);
        }

        EF_FORCEINLINE constexpr Fixed64& operator+=(const Fixed64& rhs) noexcept {
            m_value += rhs.m_value;
            return *this;
        }

        EF_FORCEINLINE constexpr Fixed64& operator-=(const Fixed64& rhs) noexcept {
            m_value -= rhs.m_value;
            return *this;
        }

        EF_FORCEINLINE Fixed64& operator*=(const Fixed64& rhs) noexcept {
            m_value = Internal::MulQ32(m_value, rhs.m_value);
            return *this;
        }

        EF_FORCEINLINE Fixed64& operator/=(const Fixed64& rhs) noexcept {
            m_value = Internal::DivQ32(m_value, rhs.m_value);
            return *this;
        }

        auto operator<=>(const Fixed64& rhs) const noexcept = default;

    public:
        [[nodiscard]] EF_FORCEINLINE static constexpr Fixed64 Zero() noexcept { return FromRaw(0); }
        [[nodiscard]] EF_FORCEINLINE static constexpr Fixed64 One() noexcept { return FromRaw(Scale); }
        [[nodiscard]] EF_FORCEINLINE static constexpr Fixed64 Epsilon() noexcept { return FromRaw(1); }

    private:
        int64_t m_value = 0;
    };

    [[nodiscard]] EF_FORCEINLINE Fixed64 operator*(int32_t lhs, const Fixed64& rhs) noexcept {
        return Fixed64::FromInt(lhs) * rhs;
    }
}
