#include "Fixed64.hpp"
#include "Base/Assert.hpp"
#include "Internal.hpp"

namespace EF {

    Fixed64 Fixed64::FromFloat(float value) noexcept {
        return FromDouble(static_cast<double>(value));
    }

    Fixed64 Fixed64::FromDouble(double value) noexcept {
        return FromRaw(static_cast<int64_t>(value * static_cast<double>(Scale)));
    }

    Fixed64 Fixed64::operator*(const Fixed64& rhs) const noexcept {
        return FromRaw(Internal::MulQ32(m_value, rhs.m_value));
    }

    Fixed64 Fixed64::operator/(const Fixed64& rhs) const noexcept {
        return FromRaw(Internal::DivQ32(m_value, rhs.m_value));
    }

}
