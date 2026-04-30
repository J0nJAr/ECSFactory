
#pragma once

#include "Math/Vector.hpp"
#include "Math/Quaternion.hpp"

namespace EF {
    struct Transform
    {
        Vector3 position;
        Quaternion rotation;
    };
}