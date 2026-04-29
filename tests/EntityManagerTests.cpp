#include "TestHarness.hpp"

#include "ECS/EntitiyManager.hpp"

TEST_CASE(EntityManager, DestroyedEntityIsNotAlive) {
    EF::EntityManager manager;

    const EF::EntityId entity = manager.CreateEntity();
    EXPECT_TRUE(manager.IsAlive(entity));

    manager.DestroyEntity(entity);

    EXPECT_FALSE(manager.IsAlive(entity));
}

TEST_CASE(EntityManager, ReusedIndexGetsNewVersion) {
    EF::EntityManager manager;

    const EF::EntityId first = manager.CreateEntity();
    const EF::EntityIndex firstIndex = EF::GetEntityIndex(first);
    const EF::EntityVersion firstVersion = EF::GetEntityVersion(first);

    manager.DestroyEntity(first);

    const EF::EntityId second = manager.CreateEntity();

    EXPECT_EQ(EF::GetEntityIndex(second), firstIndex);
    EXPECT_EQ(EF::GetEntityVersion(second), firstVersion + 1);
    EXPECT_FALSE(manager.IsAlive(first));
    EXPECT_TRUE(manager.IsAlive(second));
}
