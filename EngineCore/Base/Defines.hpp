
#ifndef DEFINES_HPP
#define DEFINES_HPP

#include <cstdint>

#if defined(_MSC_VER)
#define EF_FORCEINLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define EF_FORCEINLINE inline __attribute__((always_inline))
#else
#define EF_FORCEINLINE inline
#endif

#ifndef EF_FIXED64_FRACTIONAL_BITS
#define EF_FIXED64_FRACTIONAL_BITS 32
#endif

#ifndef EF_MATH_HANDEDNESS
#define EF_MATH_HANDEDNESS 1
#endif

#ifndef EF_MATH_FORWARD_SIGN
#define EF_MATH_FORWARD_SIGN 1
#endif

namespace EF {
    static_assert(EF_FIXED64_FRACTIONAL_BITS > 0, "EF_FIXED64_FRACTIONAL_BITS must be positive.");
    static_assert(EF_MATH_HANDEDNESS == 1 || EF_MATH_HANDEDNESS == -1, "EF_MATH_HANDEDNESS must be 1 or -1.");
    static_assert(EF_MATH_FORWARD_SIGN == 1 || EF_MATH_FORWARD_SIGN == -1, "EF_MATH_FORWARD_SIGN must be 1 or -1.");

    inline constexpr int32_t Fixed64FractionalBits = EF_FIXED64_FRACTIONAL_BITS;
    inline constexpr int64_t Fixed64Scale = 1LL << Fixed64FractionalBits;
    inline constexpr int32_t MathHandedness = EF_MATH_HANDEDNESS;
    inline constexpr int32_t MathForwardSign = EF_MATH_FORWARD_SIGN;
}

#endif // DEFINES_HPP
