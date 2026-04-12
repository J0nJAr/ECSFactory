#pragma once
#include <compare>
#include <cstdint>

namespace EF {

    class Fixed64 {
    public:
        static constexpr int32_t FractionalBits = 16;
        static constexpr int64_t Scale = 1LL << FractionalBits;

    public:
        constexpr Fixed64() noexcept : m_value(0) {}

        static constexpr Fixed64 FromRaw(int64_t raw) noexcept {
            Fixed64 v;
            v.m_value = raw;
            return v;
        }

        static constexpr Fixed64 FromInt(int32_t value) noexcept {
            return FromRaw(static_cast<int64_t>(value) << FractionalBits);
        }

        /*
        * 실수에서 변환 (초기화 혹은 렌더링 시에만 권장)
        */
        static Fixed64 FromFloat(float value) noexcept;
        static Fixed64 FromDouble(double value) noexcept;

    public:
        constexpr int64_t Raw() const noexcept {
            return m_value;
        }

        constexpr float ToFloat() const noexcept {
            return static_cast<float>(m_value) / static_cast<float>(Scale);
        }

        constexpr double ToDouble() const noexcept {
            return static_cast<double>(m_value) / static_cast<double>(Scale);
        }

        constexpr int32_t ToIntFloor() const noexcept {
            return static_cast<int32_t>(m_value >> FractionalBits);
        }

    public:
        constexpr Fixed64 operator+ (const Fixed64& rhs) const noexcept {
            return FromRaw(m_value + rhs.m_value);
        }

        constexpr Fixed64 operator- (const Fixed64& rhs) const noexcept {
            return FromRaw(m_value - rhs.m_value);
        }

        Fixed64 operator* (const Fixed64& rhs) const noexcept;
        Fixed64 operator/ (const Fixed64& rhs) const noexcept;

        constexpr Fixed64 operator-() const noexcept {
            return FromRaw(-m_value);
        }

        constexpr Fixed64& operator+=(const Fixed64& rhs) noexcept {
            m_value += rhs.m_value;
            return *this;
        }

        constexpr Fixed64& operator-=(const Fixed64& rhs) noexcept {
            m_value -= rhs.m_value;
            return *this;
        }

        Fixed64& operator*=(const Fixed64& rhs) noexcept {
            *this = *this * rhs;
            return *this;
        }

        Fixed64& operator/=(const Fixed64& rhs) noexcept {
            *this = *this / rhs;
            return *this;
        }

        auto operator<=>(const Fixed64& rhs) const noexcept = default;

    public:
        static constexpr Fixed64 Zero() noexcept { return FromRaw(0); }
        static constexpr Fixed64 One() noexcept { return FromRaw(Scale); }

    private:
        int64_t m_value;
    };
}