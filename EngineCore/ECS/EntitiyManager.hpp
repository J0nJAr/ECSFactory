#pragma once

#include <cstddef>
#include <vector>

#include "Core.hpp"

namespace EF {

    /**
     * EntityManager의 내부 상태를 그대로 담은 스냅샷.
     *
     * 결정론 lockstep에서 스폰은 네트워크로 복제하지 않는다 — 모든 peer가 같은 확정 입력을
     * 같은 순서로 적용하면 각자 알아서 같은 엔티티를 만든다. 단, 그게 성립하려면
     * **양쪽 EntityManager의 상태가 같아야** 한다. 이 구조체가 나르는 것이 바로 그 상태다.
     *
     * ### 왜 "살아 있는 엔티티 목록"이 아니라 내부 3필드를 통째로 나르는가
     *
     * - `versions`는 **죽은 index의 것까지** 필요하다. free list에서 회수된 index의 version은
     *   `m_versions[index]`를 그대로 읽어 쓰므로, 죽은 index의 version을 모르면 그 index가
     *   재사용되는 순간 `EntityId`의 version이 서버와 달라진다.
     * - `freeIndices`는 **순서가 의미를 가진다.** `CreateEntity()`가 뒤에서 pop 하므로
     *   free list의 정렬 순서가 곧 "다음에 어떤 index가 나오는가"다. 살아 있는 엔티티
     *   집합만으로는 이 순서를 복원할 수 없다.
     *
     * 반대 방향은 유도가 된다: 살아 있는 index 집합 = `[0, nextEntityIndex)` − `freeIndices`.
     * 그래서 이 3필드 표현이 "살아 있는 (index, version) 목록 + free list"보다 **작으면서
     * 동시에 필요충분**하다. 살아 있는 엔티티의 version은 `versions[index]`에 들어 있다.
     *
     * ### 불변식
     * `nextEntityIndex == versions.size()`. `CreateEntity()`가 새 index를 뽑는 경로에서만
     * 둘이 함께 늘고, `Clear()`가 둘을 함께 0으로 되돌리기 때문이다. `RestoreFrom()`이
     * 이 불변식을 검사한다 — 와이어에서 온 값을 믿지 않기 위해 중복해서 싣는다.
     */
    struct EntitySnapshot {
        /// 다음에 새로 뽑을 index(= 지금까지 존재한 적 있는 index의 개수).
        EntityIndex nextEntityIndex = 0;
        /// index 0..nextEntityIndex-1 각각의 현재 version. 죽은 index의 것도 포함한다.
        std::vector<EntityVersion> versions;
        /// 재사용 대기 중인 index. **순서가 의미를 가진다**(LIFO).
        std::vector<EntityIndex> freeIndices;
    };

    class EntityManager {
    public:
        EntityId CreateEntity();
        void DestroyEntity(EntityId entity);
        [[nodiscard]] bool IsAlive(EntityId entity) const;
        [[nodiscard]] EntityId ResolveEntityId(EntityIndex index) const;
        [[nodiscard]] EntityIndex EntityCapacity() const noexcept;
        void Clear();

        /// @brief 현재 상태를 스냅샷으로 뜬다.
        [[nodiscard]] EntitySnapshot CreateSnapshot() const;

        /// @brief 스냅샷으로 상태를 통째로 되돌린다.
        ///
        /// `Clear()` 후 재구성하는 게 아니라 3필드를 직접 대입한다 — `CreateEntity()`를
        /// 반복 호출해서는 free list의 순서를 재현할 수 없기 때문이다.
        /// @return 스냅샷이 자기모순(불변식 위반 / 범위 밖 free index / 중복 free index)이면
        ///         아무것도 바꾸지 않고 false. 신뢰할 수 없는 와이어 데이터를 받는 경로다.
        [[nodiscard]] bool RestoreFrom(const EntitySnapshot& snapshot);

    private:
        EntityIndex m_nextEntityIndex = 0;
        std::vector<EntityIndex> m_freeIndices;
        std::vector<EntityVersion> m_versions;
    };

    inline EntityId EntityManager::CreateEntity() {
        EntityVersion version;
        EntityIndex index;

        if (!m_freeIndices.empty()) {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
            version = m_versions[index];
        } else {
            index = m_nextEntityIndex++;
            m_versions.push_back(0);
            version = 0;
        }

        return CreateEntityId(index, version);
    }

    inline void EntityManager::DestroyEntity(EntityId entity) {
        if (!IsAlive(entity)) {
            return;
        }

        const EntityIndex index = GetEntityIndex(entity);
        ++m_versions[index];
        m_freeIndices.push_back(index);
    }

    inline bool EntityManager::IsAlive(EntityId entity) const {
        const EntityIndex index = GetEntityIndex(entity);
        if (index >= m_versions.size()) {
            return false;
        }

        return m_versions[index] == GetEntityVersion(entity);
    }

    inline EntityId EntityManager::ResolveEntityId(EntityIndex index) const {
        if (index >= m_versions.size()) {
            return kNullEntity;
        }

        return CreateEntityId(index, m_versions[index]);
    }

    inline EntityIndex EntityManager::EntityCapacity() const noexcept {
        return static_cast<EntityIndex>(m_versions.size());
    }

    inline void EntityManager::Clear() {
        m_nextEntityIndex = 0;
        m_freeIndices.clear();
        m_versions.clear();
    }

    inline EntitySnapshot EntityManager::CreateSnapshot() const {
        return EntitySnapshot{
            .nextEntityIndex = m_nextEntityIndex,
            .versions = m_versions,
            .freeIndices = m_freeIndices
        };
    }

    inline bool EntityManager::RestoreFrom(const EntitySnapshot& snapshot) {
        const std::size_t capacity = snapshot.versions.size();

        // 불변식 검사: nextEntityIndex는 곧 versions의 길이다.
        if (static_cast<std::size_t>(snapshot.nextEntityIndex) != capacity) {
            return false;
        }

        // free index는 범위 안이어야 하고 중복이 없어야 한다.
        // 중복이 있으면 같은 index가 두 번 살아나서 서로 다른 엔티티가 겹친다.
        std::vector<bool> isFree(capacity, false);
        for (EntityIndex index : snapshot.freeIndices) {
            if (static_cast<std::size_t>(index) >= capacity || isFree[index]) {
                return false;
            }
            isFree[index] = true;
        }

        m_nextEntityIndex = snapshot.nextEntityIndex;
        m_versions = snapshot.versions;
        m_freeIndices = snapshot.freeIndices;
        return true;
    }
}
