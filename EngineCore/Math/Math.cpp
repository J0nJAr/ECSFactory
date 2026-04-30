#include "Math.hpp"

#include <array>

namespace {
    constexpr int kSinTableSegments = 256;

    constexpr std::array<int64_t, kSinTableSegments + 1> kQuarterSinRaw = {
        0, 402, 804, 1206, 1608, 2010, 2412, 2814, 3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
        6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218, 9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391,
        12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534, 15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639,
        19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699, 22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708,
        25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656, 28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
        30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347, 33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075,
        36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716, 39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264,
        41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713, 44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056,
        46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288, 48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404,
        50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398, 52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
        54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004, 56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607,
        57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071, 59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392,
        60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568, 61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596,
        62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473, 63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197,
        64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766, 64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
        65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436, 65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535,
        65536
    };

    constexpr int64_t kPiRaw = 205887;
    constexpr int64_t kHalfPiRaw = 102944;
    constexpr int64_t kTwoPiRaw = 411775;
    constexpr int64_t kDegToRadRaw = 1144;
    constexpr int64_t kRadToDegRaw = 3754936;

    [[nodiscard]] constexpr int64_t NormalizeRadiansRaw(int64_t raw) noexcept {
        raw %= kTwoPiRaw;
        if (raw < 0) {
            raw += kTwoPiRaw;
        }
        return raw;
    }

    [[nodiscard]] EF_FORCEINLINE EF::Fixed64 InterpolateQuarterSine(int64_t quarterAngleRaw) noexcept {
        const __int128 scaledIndexRaw =
            static_cast<__int128>(quarterAngleRaw) * kSinTableSegments * EF::Fixed64::Scale / kHalfPiRaw;
        const int64_t index = static_cast<int64_t>(scaledIndexRaw >> EF::Fixed64::FractionalBits);
        if (index >= kSinTableSegments) {
            return EF::Fixed64::FromRaw(kQuarterSinRaw[kSinTableSegments]);
        }

        const EF::Fixed64 fraction = EF::Fixed64::FromRaw(
            static_cast<int64_t>(scaledIndexRaw & (EF::Fixed64::Scale - 1)));
        const EF::Fixed64 start = EF::Fixed64::FromRaw(kQuarterSinRaw[index]);
        const EF::Fixed64 end = EF::Fixed64::FromRaw(kQuarterSinRaw[index + 1]);
        return start + (end - start) * fraction;
    }
}

namespace EF::Math {
    Fixed64 Pi() noexcept { return Fixed64::FromRaw(kPiRaw); }
    Fixed64 HalfPi() noexcept { return Fixed64::FromRaw(kHalfPiRaw); }
    Fixed64 TwoPi() noexcept { return Fixed64::FromRaw(kTwoPiRaw); }

    Fixed64 DegreesToRadians(const Fixed64& degrees) noexcept {
        return degrees * Fixed64::FromRaw(kDegToRadRaw);
    }

    Fixed64 RadiansToDegrees(const Fixed64& radians) noexcept {
        return radians * Fixed64::FromRaw(kRadToDegRaw);
    }

    Fixed64 Sqrt(const Fixed64& value) noexcept {
        EF_ASSERT(value >= Fixed64::Zero(), "Sqrt expects a non-negative Fixed64.");
        if (value <= Fixed64::Zero()) {
            return Fixed64::Zero();
        }

        const unsigned __int128 scaled = static_cast<unsigned __int128>(value.Raw()) << Fixed64::FractionalBits;
        unsigned __int128 x = scaled;
        unsigned __int128 y = (x + 1u) >> 1;

        while (y < x) {
            x = y;
            y = (x + scaled / x) >> 1;
        }

        return Fixed64::FromRaw(static_cast<int64_t>(x));
    }

    Fixed64 Sin(const Fixed64& radians) noexcept {
        const int64_t normalized = NormalizeRadiansRaw(radians.Raw());
        const bool secondHalf = normalized >= kPiRaw;
        const int64_t halfNormalized = secondHalf ? normalized - kPiRaw : normalized;
        const bool mirrored = halfNormalized > kHalfPiRaw;
        const int64_t quarterAngle = mirrored ? (kPiRaw - halfNormalized) : halfNormalized;
        const Fixed64 result = InterpolateQuarterSine(quarterAngle);
        return secondHalf ? -result : result;
    }

    Fixed64 Cos(const Fixed64& radians) noexcept {
        return Sin(HalfPi() - radians);
    }

    namespace Approx {
        namespace {
            [[nodiscard]] inline Quaternion FromUnitRotationMatrix(
                double m00, double m01, double m02,
                double m10, double m11, double m12,
                double m20, double m21, double m22) noexcept {
                double x = 0.0;
                double y = 0.0;
                double z = 0.0;
                double w = 1.0;

                const double trace = m00 + m11 + m22;
                if (trace > 0.0) {
                    const double s = 0.5 / std::sqrt(trace + 1.0);
                    w = 0.25 / s;
                    x = (m21 - m12) * s;
                    y = (m02 - m20) * s;
                    z = (m10 - m01) * s;
                } else if (m00 > m11 && m00 > m22) {
                    const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
                    w = (m21 - m12) / s;
                    x = 0.25 * s;
                    y = (m01 + m10) / s;
                    z = (m02 + m20) / s;
                } else if (m11 > m22) {
                    const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
                    w = (m02 - m20) / s;
                    x = (m01 + m10) / s;
                    y = 0.25 * s;
                    z = (m12 + m21) / s;
                } else {
                    const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
                    w = (m10 - m01) / s;
                    x = (m02 + m20) / s;
                    y = (m12 + m21) / s;
                    z = 0.25 * s;
                }

                return Normalized(Quaternion(
                    Fixed64::FromDouble(x),
                    Fixed64::FromDouble(y),
                    Fixed64::FromDouble(z),
                    Fixed64::FromDouble(w)));
            }
        }

        Quaternion FromEulerRadians(const Vector3& eulerRadians) noexcept {
            const double hx = eulerRadians.x.ToDouble() * 0.5;
            const double hy = eulerRadians.y.ToDouble() * 0.5;
            const double hz = eulerRadians.z.ToDouble() * 0.5;

            const double sx = std::sin(hx);
            const double cx = std::cos(hx);
            const double sy = std::sin(hy);
            const double cy = std::cos(hy);
            const double sz = std::sin(hz);
            const double cz = std::cos(hz);

            return Normalized(Quaternion(
                Fixed64::FromDouble(sx * cy * cz - cx * sy * sz),
                Fixed64::FromDouble(cx * sy * cz + sx * cy * sz),
                Fixed64::FromDouble(cx * cy * sz - sx * sy * cz),
                Fixed64::FromDouble(cx * cy * cz + sx * sy * sz)));
        }

        Quaternion FromEulerDegrees(const Vector3& eulerDegrees) noexcept {
            return FromEulerRadians(Vector3(
                DegreesToRadians(eulerDegrees.x),
                DegreesToRadians(eulerDegrees.y),
                DegreesToRadians(eulerDegrees.z)));
        }

        Vector3 ToEulerRadians(const Quaternion& value) noexcept {
            const Quaternion normalized = Normalized(value);
            const double x = normalized.x.ToDouble();
            const double y = normalized.y.ToDouble();
            const double z = normalized.z.ToDouble();
            const double w = normalized.w.ToDouble();

            const double sinrCosp = 2.0 * (w * x + y * z);
            const double cosrCosp = 1.0 - 2.0 * (x * x + y * y);
            const double roll = std::atan2(sinrCosp, cosrCosp);

            const double sinp = 2.0 * (w * y - z * x);
            const double pitch = std::abs(sinp) >= 1.0
                ? std::copysign(3.14159265358979323846 / 2.0, sinp)
                : std::asin(sinp);

            const double sinyCosp = 2.0 * (w * z + x * y);
            const double cosyCosp = 1.0 - 2.0 * (y * y + z * z);
            const double yaw = std::atan2(sinyCosp, cosyCosp);

            return Vector3(Fixed64::FromDouble(roll), Fixed64::FromDouble(pitch), Fixed64::FromDouble(yaw));
        }

        Quaternion Lerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept {
            return a + (b - a) * t;
        }

        Quaternion Nlerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept {
            return Normalized(Lerp(a, b, t));
        }

        Quaternion Slerp(const Quaternion& a, const Quaternion& b, const Fixed64& t) noexcept {
            Quaternion end = b;
            Fixed64 dot = Dot(a, b);

            if (dot < Fixed64::Zero()) {
                end = -end;
                dot = -dot;
            }

            if (dot > Fixed64::FromDouble(0.9995)) {
                return Nlerp(a, end, t);
            }

            const double theta0 = std::acos(dot.ToDouble());
            const double theta = theta0 * t.ToDouble();
            const double sinTheta = std::sin(theta);
            const double sinTheta0 = std::sin(theta0);

            const Fixed64 s0 = Fixed64::FromDouble(std::cos(theta) - dot.ToDouble() * sinTheta / sinTheta0);
            const Fixed64 s1 = Fixed64::FromDouble(sinTheta / sinTheta0);
            return Normalized((a * s0) + (end * s1));
        }

        Quaternion LookRotation(const Vector3& forward, const Vector3& upHint) noexcept {
            const Vector3 f = Normalized(forward);
            if (IsZero(f)) {
                return Quaternion::Identity();
            }

            const Vector3 up = Normalized(upHint);
            if (IsZero(up)) {
                return Quaternion::Identity();
            }

            const Vector3 right = Normalized(MathHandedness > 0 ? Cross(up, f) : Cross(f, up));
            if (IsZero(right)) {
                return Quaternion::Identity();
            }

            const Vector3 correctedUp = MathHandedness > 0 ? Cross(f, right) : Cross(right, f);
            const Vector3 basisForward = MathForwardSign > 0 ? f : -f;

            return FromUnitRotationMatrix(
                right.x.ToDouble(), correctedUp.x.ToDouble(), basisForward.x.ToDouble(),
                right.y.ToDouble(), correctedUp.y.ToDouble(), basisForward.y.ToDouble(),
                right.z.ToDouble(), correctedUp.z.ToDouble(), basisForward.z.ToDouble());
        }

        bool NearlyEqual(const Fixed64& lhs, const Fixed64& rhs, const Fixed64& tolerance) noexcept {
            return Abs(lhs - rhs) <= tolerance;
        }

        bool NearlyEqual(const Vector3& lhs, const Vector3& rhs, const Fixed64& tolerance) noexcept {
            return NearlyEqual(lhs.x, rhs.x, tolerance) &&
                   NearlyEqual(lhs.y, rhs.y, tolerance) &&
                   NearlyEqual(lhs.z, rhs.z, tolerance);
        }

        bool NearlyEqual(const Quaternion& lhs, const Quaternion& rhs, const Fixed64& tolerance) noexcept {
            return NearlyEqual(lhs.x, rhs.x, tolerance) &&
                   NearlyEqual(lhs.y, rhs.y, tolerance) &&
                   NearlyEqual(lhs.z, rhs.z, tolerance) &&
                   NearlyEqual(lhs.w, rhs.w, tolerance);
        }
    }
}
