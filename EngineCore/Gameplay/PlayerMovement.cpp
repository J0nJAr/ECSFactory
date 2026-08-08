#include "Gameplay/PlayerMovement.hpp"

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "ECS/View.hpp"
#include "ECS/World.hpp"
#include "Math/Vector.hpp"

namespace EF {
    namespace {
        Vector3 MoveDelta(uint64_t buttonMask) {
            Vector3 delta = Vector3::Zero();
            if (buttonMask & kInputMoveRight) {
                delta += Vector3::UnitX();
            }
            if (buttonMask & kInputMoveLeft) {
                delta -= Vector3::UnitX();
            }
            if (buttonMask & kInputMoveForward) {
                delta += Vector3::UnitZ();
            }
            if (buttonMask & kInputMoveBack) {
                delta -= Vector3::UnitZ();
            }
            return delta;
        }
    }

    void ApplyConfirmedInputs(World& world, std::span<const InputCommand> inputs) {
        const Fixed64 step = Fixed64::FromInt(1); // tick당 1유닛(결정론 고정 스텝)

        auto view = world.CreateView<Transform, PlayerControl>();
        view.ForEach([&](Transform& transform, PlayerControl& control) {
            for (const InputCommand& input : inputs) {
                if (input.clientId != control.clientId) {
                    continue;
                }
                transform.position += MoveDelta(input.buttonMask) * step;
                break;
            }
        });
    }
}
