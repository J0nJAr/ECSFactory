#include "TestHarness.hpp"

#include <array>
#include <cstdint>

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "ECS/World.hpp"
#include "Gameplay/PlayerMovement.hpp"
#include "Math/Quaternion.hpp"
#include "Math/Vector.hpp"
#include "Network/InputCommand.hpp"

namespace {
    EF::World MakeWorldWithPlayer(uint32_t clientId) {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::PlayerControl>();

        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(0, 0, 0),
            .rotation = EF::Quaternion::Identity()
        });
        world.AddComponent<EF::PlayerControl>(entity, EF::PlayerControl{ .clientId = clientId });
        return world;
    }
}

TEST_CASE(Gameplay, MoveInputAdvancesTransformDeterministically) {
    EF::World world = MakeWorldWithPlayer(1);

    std::array<EF::InputCommand, 1> inputs{
        EF::InputCommand{ .clientId = 1, .buttonMask = EF::kInputMoveRight }
    };
    EF::ApplyConfirmedInputs(world, inputs);
    EF::ApplyConfirmedInputs(world, inputs);

    // +X로 2 tick 이동 → position == (2, 0, 0)
    auto view = world.CreateView<EF::Transform>();
    view.ForEach([](EF::EntityIndex, EF::Transform& transform) {
        EXPECT_TRUE(transform.position.x == EF::Fixed64::FromInt(2));
        EXPECT_TRUE(transform.position.y == EF::Fixed64::Zero());
        EXPECT_TRUE(transform.position.z == EF::Fixed64::Zero());
    });
}

TEST_CASE(Gameplay, OppositeButtonsCancelOut) {
    EF::World world = MakeWorldWithPlayer(3);

    std::array<EF::InputCommand, 1> inputs{
        EF::InputCommand{ .clientId = 3, .buttonMask = EF::kInputMoveForward | EF::kInputMoveBack }
    };
    EF::ApplyConfirmedInputs(world, inputs);

    auto view = world.CreateView<EF::Transform>();
    view.ForEach([](EF::EntityIndex, EF::Transform& transform) {
        EXPECT_TRUE(transform.position == EF::Vector3(0, 0, 0));
    });
}

TEST_CASE(Gameplay, SameInputsProduceSameHashOnTwoWorlds) {
    EF::World a = MakeWorldWithPlayer(7);
    EF::World b = MakeWorldWithPlayer(7);

    const EF::InputCommand sequence[] = {
        EF::InputCommand{ .clientId = 7, .buttonMask = EF::kInputMoveForward },
        EF::InputCommand{ .clientId = 7, .buttonMask = EF::kInputMoveRight },
        EF::InputCommand{ .clientId = 7, .buttonMask = EF::kInputMoveForward | EF::kInputMoveRight }
    };
    for (const EF::InputCommand& command : sequence) {
        std::array<EF::InputCommand, 1> one{ command };
        EF::ApplyConfirmedInputs(a, one);
        EF::ApplyConfirmedInputs(b, one);
    }

    // 같은 입력을 같은 순서로 넣으면 두 월드 상태가 비트 단위로 일치한다.
    EXPECT_EQ(a.ComputeHash(), b.ComputeHash());
}

TEST_CASE(Gameplay, InputOnlyAffectsMatchingClientEntity) {
    EF::World world;
    world.RegisterSnapshotComponent<EF::Transform>();
    world.RegisterSnapshotComponent<EF::PlayerControl>();

    const EF::EntityId one = world.CreateEntity();
    world.AddComponent<EF::Transform>(one, EF::Transform{ .position = EF::Vector3(0, 0, 0), .rotation = EF::Quaternion::Identity() });
    world.AddComponent<EF::PlayerControl>(one, EF::PlayerControl{ .clientId = 1 });

    const EF::EntityId two = world.CreateEntity();
    world.AddComponent<EF::Transform>(two, EF::Transform{ .position = EF::Vector3(0, 0, 0), .rotation = EF::Quaternion::Identity() });
    world.AddComponent<EF::PlayerControl>(two, EF::PlayerControl{ .clientId = 2 });

    // clientId 1만 움직이는 입력.
    std::array<EF::InputCommand, 1> inputs{
        EF::InputCommand{ .clientId = 1, .buttonMask = EF::kInputMoveRight }
    };
    EF::ApplyConfirmedInputs(world, inputs);

    auto view = world.CreateView<EF::Transform, EF::PlayerControl>();
    view.ForEach([](EF::Transform& transform, EF::PlayerControl& control) {
        if (control.clientId == 1) {
            EXPECT_TRUE(transform.position.x == EF::Fixed64::FromInt(1));
        } else {
            EXPECT_TRUE(transform.position.x == EF::Fixed64::Zero());
        }
    });
}
