#include "Gameplay/PlayerMovement.hpp"

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "ECS/View.hpp"
#include "ECS/World.hpp"
#include "Math/Vector.hpp"

namespace EF {
    namespace {
        // clientId가 조종하는 엔티티를 찾는다. 없으면 kNullEntity.
        //
        // 뷰 순회로 찾는 이유: 이 ECS에는 "이 컴포넌트를 가진 엔티티 하나 찾기"가 없고,
        // 플레이어 수는 많아야 수십이라 선형 탐색이 문제되지 않는다.
        // 인덱스를 따로 들면 그 인덱스도 결정론적으로 유지해야 하는 상태가 하나 더 늘어난다.
        EntityId FindPlayerEntityImpl(World& world, uint32_t clientId) {
            EntityId found = kNullEntity;
            for (auto [entity, control] : world.CreateView<PlayerControl>()) {
                if (control.clientId == clientId) {
                    found = entity;
                    break;
                }
            }
            return found;
        }

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

    EntityId FindPlayerEntityFor(World& world, uint32_t clientId) {
        return FindPlayerEntityImpl(world, clientId);
    }

    void ApplySessionEvents(World& world, std::span<const SessionEvent> events) {
        for (const SessionEvent& event : events) {
            switch (event.kind) {
            case SessionEvent::Kind::Join: {
                // 이미 있으면 만들지 않는다. 이벤트가 중복 적용되면 엔티티가 둘이 되고,
                // 그건 peer마다 다르게 나타나지 않아서 해시로도 안 잡힌다.
                // (스냅샷에 이미 담겨 온 뒤 같은 이벤트를 또 받는 경로가 실제로 있다.)
                if (FindPlayerEntityImpl(world, event.clientId) != kNullEntity) {
                    break;
                }

                const EntityId entity = world.CreateEntity();

                // clientId는 1부터 발급된다. 첫 클라가 원점에 서도록 1을 빼서 슬롯 번호로 쓴다.
                const int32_t spawnSlot = static_cast<int32_t>(event.clientId) - 1;
                world.AddComponent<Transform>(entity, Transform{
                    .position = Vector3(Fixed64::FromInt(spawnSlot * kPlayerSpawnSpacingUnits),
                                        Fixed64::Zero(),
                                        Fixed64::Zero()),
                    .rotation = Quaternion::Identity()
                });
                world.AddComponent<PlayerControl>(entity, PlayerControl{ .clientId = event.clientId });
                break;
            }

            case SessionEvent::Kind::Leave: {
                const EntityId entity = FindPlayerEntityImpl(world, event.clientId);
                if (entity != kNullEntity) {
                    world.DestroyEntity(entity);
                }
                break;
            }
            }
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
