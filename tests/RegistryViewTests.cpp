#include <vector>

#include "TestHarness.hpp"

#include "ECS/Registry.hpp"

namespace {
    struct Position {
        int value;

        Position(int newValue) : value(newValue) {}
    };

    struct Velocity {
        int value;

        Velocity(int newValue) : value(newValue) {}
    };

    struct Sleeping {};
}

TEST_CASE(RegistryView, ViewWithRequiredComponentsFiltersEntities) {
    EF::Registry registry;

    const EF::EntityId a = registry.CreateEntity();
    const EF::EntityId b = registry.CreateEntity();

    registry.AddComponent<Position>(a, 10);
    registry.AddComponent<Velocity>(a, 20);
    registry.AddComponent<Position>(b, 30);

    auto view = registry.CreateView<Position, Velocity>();
    int visited = 0;
    int sum = 0;

    view.ForEach([&](Position& position, Velocity& velocity) {
        ++visited;
        sum += position.value + velocity.value;
    });

    EXPECT_EQ(visited, 1);
    EXPECT_EQ(sum, 30);
}

TEST_CASE(RegistryView, ViewWithoutExcludedComponentsSkipsExcludedEntities) {
    EF::Registry registry;

    const EF::EntityId awake = registry.CreateEntity();
    const EF::EntityId sleeping = registry.CreateEntity();

    registry.AddComponent<Position>(awake, 10);
    registry.AddComponent<Velocity>(awake, 20);

    registry.AddComponent<Position>(sleeping, 30);
    registry.AddComponent<Velocity>(sleeping, 40);
    registry.AddComponent<Sleeping>(sleeping);

    auto view = registry.CreateView<Position, Velocity>(EF::Without<Sleeping>{});
    int visited = 0;
    std::vector<int> totals;

    view.ForEach([&](Position& position, Velocity& velocity) {
        ++visited;
        totals.push_back(position.value + velocity.value);
    });

    EXPECT_EQ(visited, 1);
    EXPECT_EQ(totals.size(), std::size_t{1});
    EXPECT_EQ(totals[0], 30);
}

TEST_CASE(RegistryView, IteratorReturnsEntityIdAndComponentReferences) {
    EF::Registry registry;

    const EF::EntityId entity = registry.CreateEntity();
    registry.AddComponent<Position>(entity, 7);
    registry.AddComponent<Velocity>(entity, 9);

    auto view = registry.CreateView<Position, Velocity>();
    auto it = view.begin();
    auto [resolvedEntity, position, velocity] = *it;

    EXPECT_EQ(resolvedEntity, entity);
    EXPECT_EQ(position.value, 7);
    EXPECT_EQ(velocity.value, 9);
}
