#pragma once
#include <cstdint>

#include "Base/Assert.hpp"
#include "Base/Defines.hpp"


#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace EF::Internal {

    struct UInt128Parts {
        uint64_t high;
        uint64_t low;
    };

    EF_FORCEINLINE UInt128Parts MultiplyUnsigned64(uint64_t a, uint64_t b) noexcept {
#if defined(__SIZEOF_INT128__)
        unsigned __int128 result = static_cast<unsigned __int128>(a) * static_cast<unsigned __int128>(b);
        return {
            static_cast<uint64_t>(result >> 64),
            static_cast<uint64_t>(result)
        };
#elif defined(_MSC_VER) && defined(_M_X64)
        uint64_t high = 0;
        uint64_t low = static_cast<uint64_t>(_umul128(a, b, &high));
        return { high, low };
#else
        // fallback: 32비트 조합 곱셈
        const uint64_t a0 = static_cast<uint32_t>(a);
        const uint64_t a1 = a >> 32;
        const uint64_t b0 = static_cast<uint32_t>(b);
        const uint64_t b1 = b >> 32;

        const uint64_t p00 = a0 * b0;
        const uint64_t p01 = a0 * b1;
        const uint64_t p10 = a1 * b0;
        const uint64_t p11 = a1 * b1;

        uint64_t middle = (p00 >> 32) + static_cast<uint32_t>(p01) + static_cast<uint32_t>(p10);
        uint64_t low = (p00 & 0xFFFFFFFFull) | (middle << 32);
        uint64_t high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);

        return { high, low };
#endif
    }

    EF_FORCEINLINE uint64_t TwosComplementAbs(int64_t value) noexcept {
        if (value >= 0) {
            return static_cast<uint64_t>(value);
        }

        // INT64_MIN도 안전하게 절댓값 magnitude로 변환
        return ~static_cast<uint64_t>(value) + 1ull;
    }

    EF_FORCEINLINE int64_t ApplySignedMagnitude(uint64_t magnitude, bool negative) noexcept {
        if (!negative) {
            return static_cast<int64_t>(magnitude);
        }

        return static_cast<int64_t>(~magnitude + 1ull);
    }

    EF_FORCEINLINE int64_t MulQ32(int64_t lhs, int64_t rhs) noexcept {
#if defined(__SIZEOF_INT128__)
        __int128 result = static_cast<__int128>(lhs) * static_cast<__int128>(rhs);
        return static_cast<int64_t>(result >> EF::Fixed64FractionalBits);
#else
        const bool negative = ((lhs < 0) ^ (rhs < 0));

        const uint64_t a = TwosComplementAbs(lhs);
        const uint64_t b = TwosComplementAbs(rhs);

        const UInt128Parts mul = MultiplyUnsigned64(a, b);

        // (high:low) >> 32
        const uint64_t shifted = (mul.high << 32) | (mul.low >> 32);
        return ApplySignedMagnitude(shifted, negative);
#endif
    }

    EF_FORCEINLINE uint64_t DivideUnsigned128By64(uint64_t high, uint64_t low, uint64_t divisor) noexcept {
        EF_ASSERT(divisor != 0, "Division by zero in DivideUnsigned128By64.");

#if defined(__SIZEOF_INT128__)
        unsigned __int128 numerator =
            (static_cast<unsigned __int128>(high) << 64) |
            static_cast<unsigned __int128>(low);

        return static_cast<uint64_t>(numerator / divisor);
#elif defined(_MSC_VER) && defined(_M_X64)
        uint64_t remainder = 0;
        return _udiv128(high, low, divisor, &remainder);
#else
        // 단순 long division fallback
        uint64_t quotient = 0;
        uint64_t rem = 0;

        for (int i = 127; i >= 0; --i) {
            rem <<= 1;

            if (i >= 64) {
                rem |= (high >> (i - 64)) & 1ull;
            }
            else {
                rem |= (low >> i) & 1ull;
            }

            if (rem >= divisor) {
                rem -= divisor;
                if (i < 64) {
                    quotient |= (1ull << i);
                }
            }
        }

        return quotient;
#endif
    }

    EF_FORCEINLINE int64_t DivQ32(int64_t lhs, int64_t rhs) noexcept {
        EF_ASSERT(rhs != 0, "Division by zero in Fixed64.");

#if defined(__SIZEOF_INT128__)
        __int128 numerator = static_cast<__int128>(lhs) << EF::Fixed64FractionalBits;
        return static_cast<int64_t>(numerator / rhs);
#else
        const bool negative = ((lhs < 0) ^ (rhs < 0));

        const uint64_t a = TwosComplementAbs(lhs);
        const uint64_t b = TwosComplementAbs(rhs);

        // (a << 32)를 128비트 [high:low]로 구성
        const uint64_t high = a >> 32;
        const uint64_t low = a << 32;

        const uint64_t quotient = DivideUnsigned128By64(high, low, b);
        return ApplySignedMagnitude(quotient, negative);
#endif
    }

} // namespace EF::Internal
