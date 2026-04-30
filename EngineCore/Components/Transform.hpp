
#pragma once

#include "Math/Quaternion.hpp"
#include "Math/Vector.hpp"

namespace EF {
    struct Transform
    {
        Vector3 position;
        Quaternion rotation;
    };
}