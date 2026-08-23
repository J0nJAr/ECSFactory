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

// 결함 재현: version만 비교하면 "파괴됐지만 아직 재사용되지 않은 index"가
// 살아 있는 것으로 판정된다. ResolveEntityId는 그 index의 **현재** version을 실어 주므로
// 예전 id로는 걸리지 않던 함정이 여기서 열린다.
TEST_CASE(EntityManager, ResolvedIdForFreedIndexIsNotAlive) {
    EF::EntityManager manager;

    const EF::EntityId entity = manager.CreateEntity();
    const EF::EntityIndex index = EF::GetEntityIndex(entity);

    manager.DestroyEntity(entity);

    // 재사용 전이라 versions[index]는 그대로 남아 있고, resolved의 version은 그 최신값이다.
    const EF::EntityId resolved = manager.ResolveEntityId(index);
    EXPECT_EQ(EF::GetEntityIndex(resolved), index);
    EXPECT_FALSE(manager.IsAlive(resolved));

    // 손으로 만든 id도 마찬가지여야 한다. 어떤 version을 넣든 죽은 index는 죽은 것이다.
    EXPECT_FALSE(manager.IsAlive(EF::CreateEntityId(index, EF::GetEntityVersion(resolved))));
}

// 재사용된 뒤에는 다시 살아 있어야 한다 — 생존 판정이 "한 번 죽으면 영원히 죽음"으로
// 굳어버리면 free list 재사용 자체가 망가진다.
TEST_CASE(EntityManager, ReusedIndexBecomesAliveAgainWithBumpedVersion) {
    EF::EntityManager manager;

    const EF::EntityId first = manager.CreateEntity();
    const EF::EntityIndex index = EF::GetEntityIndex(first);
    manager.DestroyEntity(first);

    const EF::EntityId reused = manager.CreateEntity();
    EXPECT_EQ(EF::GetEntityIndex(reused), index);

    EXPECT_TRUE(manager.IsAlive(reused));
    EXPECT_TRUE(manager.IsAlive(manager.ResolveEntityId(index)));
    EXPECT_FALSE(manager.IsAlive(first));  // 예전 version은 여전히 죽은 id다
}

// 생존 플래그는 free list에서 유도되는 캐시다. 스냅샷 복원 뒤에도 둘이 일치해야 한다 —
// 어긋나면 클라만 "죽은 index를 살아 있다"고 보고 결정론이 갈라진다.
TEST_CASE(EntityManager, RestoreFromSnapshotRebuildsAliveState) {
    EF::EntityManager source;

    const EF::EntityId alive = source.CreateEntity();
    const EF::EntityId dead = source.CreateEntity();
    const EF::EntityIndex deadIndex = EF::GetEntityIndex(dead);
    source.DestroyEntity(dead);

    EF::EntityManager restored;
    EXPECT_TRUE(restored.RestoreFrom(source.CreateSnapshot()));

    EXPECT_TRUE(restored.IsAlive(alive));
    EXPECT_FALSE(restored.IsAlive(restored.ResolveEntityId(deadIndex)));

    // 복원 결과가 원본과 같은 id를 이어서 뽑아야 한다(free list 순서까지 복원됐다는 뜻).
    EXPECT_EQ(restored.CreateEntity(), source.CreateEntity());
}
