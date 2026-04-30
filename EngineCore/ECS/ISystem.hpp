#pragma once

#include <cstdint>

#include "Math/Fixed64.hpp"

namespace EF {
    class Registry;
    class World;

    class ISystem {
    public:
        virtual ~ISystem() = default;

        virtual void Initialize(World& world) = 0;
        virtual void FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) = 0;
        virtual void FrameUpdate(World& world, float frameDeltaSeconds) = 0;
        virtual void Shutdown(World& world) = 0;
    };
}
