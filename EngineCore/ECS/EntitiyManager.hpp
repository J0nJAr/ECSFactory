#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
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

    /**
     * `EntitySnapshot` 과 같은 3필드를 **복사 없이** 읽는 뷰. 결정론 해시(`World::ComputeHash`)용.
     *
     * 해시는 매 tick 돈다. `CreateSnapshot()` 은 versions·free list 벡터를 통째로 복사하므로
     * 엔티티가 수만 개인 공장에서 tick마다 쓰기엔 낭비다. 그래서 읽기 전용 span을 따로 연다.
     *
     * ⚠️ 뷰는 다음 `CreateEntity` / `DestroyEntity` / `Clear` / `RestoreFrom` 까지만 유효하다
     *    (벡터가 재할당되면 span이 허공을 가리킨다). 받아서 바로 쓰고 버린다. 보관하지 말 것.
     */
    struct EntityStateView {
        EntityIndex nextEntityIndex = 0;
        std::span<const EntityVersion> versions;
        std::span<const EntityIndex> freeIndices;
    };

    // 엔티티 id의 발급·회수·생존 판정을 맡는다. 컴포넌트는 모른다.
    //
    // id는 index와 version으로 이루어진다. index를 재사용할 때 version을 올려서,
    // 파괴된 엔티티를 가리키던 예전 id가 새 엔티티로 오인되지 않게 한다.
    //
    // 보장:
    //  - 같은 상태에서 같은 횟수로 CreateEntity를 부르면 같은 id 열이 나온다.
    //    free list는 LIFO(뒤에서 pop)다. 이 결정성이 lockstep의 전제다 — 서버와 클라가
    //    같은 확정 입력으로 스폰하면 각자 계산해도 같은 EntityId가 나와야 한다.
    //  - CreateSnapshot / RestoreFrom으로 이 상태 전체가 복제된다.
    //  - IsAlive는 version과 **생존 여부**를 함께 본다. 회수돼 free list에 들어간 index는
    //    아직 재사용되지 않았더라도 죽은 것으로 판정된다(아래 m_alive 주석 참조).
    //
    // 이 타입이 하지 않는 일:
    //  - 컴포넌트를 지우지 않는다. 파괴 시 풀 정리는 Registry의 몫이다.
    class EntityManager {
    public:
        EntityId CreateEntity();
        void DestroyEntity(EntityId entity);
        [[nodiscard]] bool IsAlive(EntityId entity) const;

        /// @brief index에 대응하는 **현재** id를 만든다(version은 지금 값).
        ///
        /// ⚠️ 이건 "그 index를 가리키는 id를 표기"할 뿐 생존을 보증하지 않는다.
        ///    회수된 index에도 id를 돌려준다 — 그 id는 `IsAlive` 에서 false가 된다.
        ///    범위 밖 index만 `kNullEntity` 다.
        [[nodiscard]] EntityId ResolveEntityId(EntityIndex index) const;
        [[nodiscard]] EntityIndex EntityCapacity() const noexcept;
        void Clear();

        /// @brief 현재 상태를 스냅샷으로 뜬다.
        [[nodiscard]] EntitySnapshot CreateSnapshot() const;

        /// @brief 현재 상태를 복사 없이 읽는다. 수명 제약은 `EntityStateView` 주석 참조.
        [[nodiscard]] EntityStateView ViewState() const noexcept {
            return EntityStateView{
                .nextEntityIndex = m_nextEntityIndex,
                .versions = m_versions,
                .freeIndices = m_freeIndices
            };
        }

        /// @brief 스냅샷으로 상태를 통째로 되돌린다.
        ///
        /// `Clear()` 후 재구성하는 게 아니라 3필드를 직접 대입한다 — `CreateEntity()`를
        /// 반복 호출해서는 free list의 순서를 재현할 수 없기 때문이다.
        /// 생존 플래그(`m_alive`)는 free list에서 유도되므로 여기서 다시 만든다.
        /// @return 스냅샷이 자기모순(불변식 위반 / 범위 밖 free index / 중복 free index)이면
        ///         아무것도 바꾸지 않고 false. 신뢰할 수 없는 와이어 데이터를 받는 경로다.
        [[nodiscard]] bool RestoreFrom(const EntitySnapshot& snapshot);

    private:
        EntityIndex m_nextEntityIndex = 0;
        std::vector<EntityIndex> m_freeIndices;
        std::vector<EntityVersion> m_versions;

        /**
         * index별 생존 플래그. `m_versions` 와 길이가 같고, `m_alive[i] == false` 는
         * 정확히 "i가 `m_freeIndices` 안에 있다"와 같은 뜻이다.
         *
         * ### 왜 이 중복 상태를 두는가
         *
         * version만으로는 생존을 판정할 수 없다. `DestroyEntity` 가 version을 올리므로
         * **파괴 전에 발급된** id는 걸러지지만, `ResolveEntityId(index)` 처럼 그 index의
         * *현재* version으로 만든 id는 죽은 index인데도 통과한다. 그런 id로
         * `Registry::AddComponent` 를 부르면 `EF_ASSERT(IsAlive(...))` 가 조용히 넘어가고,
         * 회수 대기 중인 index에 컴포넌트가 붙는다. 그 index가 재사용되는 순간
         * 새 엔티티가 남의 컴포넌트를 물려받는다 — 결정론 시뮬레이션에서는 곧 디싱크다.
         *
         * ### 버린 대안
         *
         * - **`m_freeIndices` 를 선형 탐색.** 상태를 하나도 안 늘려서 제일 정직하지만
         *   O(free list 길이)다. `IsAlive` 는 컴포넌트를 만질 때마다 불리는 경로라
         *   엔티티를 많이 파괴한 월드에서 바로 느려진다. 공장 게임은 대량 생성·파괴가
         *   기본 동작이므로 이건 곤란하다.
         * - **version 홀짝으로 생존 표시**(살아 있으면 짝수, 죽으면 홀수). 추가 메모리가
         *   0이고 판정도 O(1)이지만, 재사용마다 version이 2씩 오르고 "재사용된 id의 version은
         *   직전보다 1 크다"는 기존 계약이 깨진다. version은 스냅샷으로 와이어를 타고
         *   나가는 값이라, 규칙을 바꾸면 옛 스냅샷·옛 문서와의 해석이 어긋난다.
         * - **free list를 `unordered_set` 으로.** 조회는 O(1)이 되지만 free list는
         *   **순서가 의미를 갖는다**(LIFO — 다음에 어떤 index가 나오는가). 순서를 잃으면
         *   결정론 자체가 무너진다.
         *
         * 그래서 "free list는 순서 있는 벡터 그대로 두고, 멤버십만 따로 캐시한다"를 골랐다.
         * 비용은 엔티티당 1바이트, 유지 지점은 넷뿐이다:
         * `CreateEntity` / `DestroyEntity` / `Clear` / `RestoreFrom`.
         *
         * `std::vector<bool>` 이 아니라 `uint8_t` 벡터인 이유는 둘이다.
         * 1. 이건 조회가 압도적으로 많은 자료구조다. 비트 마스킹 없이 바이트 하나를 읽는 쪽이
         *    싸고, 8배 메모리는 엔티티 100만 개에서도 1MB라 문제되지 않는다.
         * 2. `vector<bool>` 은 프록시 참조를 쓰는 별종 특수화다. 여기서는 gcc가 재할당
         *    경로(`_M_insert_aux`)를 두고 허위 `-Warray-bounds` 경고를 내기까지 했다.
         *    비트 밀도를 위해 특수화의 함정을 떠안을 이유가 없다.
         *
         * ⚠️ 유도 상태이므로 **와이어에는 싣지 않는다.** `EntitySnapshot` 은 free list를
         *    이미 나르고, `RestoreFrom` 이 거기서 이걸 다시 만든다. 따로 실으면
         *    두 표현이 어긋난 스냅샷을 검증해야 하는 부담만 는다.
         */
        std::vector<uint8_t> m_alive;
    };

    inline EntityId EntityManager::CreateEntity() {
        EntityVersion version;
        EntityIndex index;

        if (!m_freeIndices.empty()) {
            index = m_freeIndices.back();
            m_freeIndices.pop_back();
            version = m_versions[index];
            m_alive[index] = 1;
        } else {
            index = m_nextEntityIndex++;
            m_versions.push_back(0);
            m_alive.push_back(1);
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
        m_alive[index] = 0;
        m_freeIndices.push_back(index);
    }

    // 판정 순서: 범위 → 생존 → version.
    // version 비교만으로는 "회수됐지만 아직 재사용되지 않은 index"를 걸러내지 못한다.
    // 그 index의 현재 version으로 만든 id(= ResolveEntityId의 결과)가 통과해버리기 때문이다.
    // 자세한 근거와 버린 대안은 m_alive 주석에 있다.
    inline bool EntityManager::IsAlive(EntityId entity) const {
        const EntityIndex index = GetEntityIndex(entity);
        if (index >= m_versions.size()) {
            return false;
        }

        if (m_alive[index] == 0) {
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
        m_alive.clear();
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

        // 검증과 생존 플래그 재구성을 한 번에 한다.
        // free index는 범위 안이어야 하고 중복이 없어야 한다 — 중복이 있으면 같은 index가
        // 두 번 살아나서 서로 다른 엔티티가 겹친다. "이미 free로 표시됨"이 곧 중복이므로
        // 검증용 벡터를 따로 두지 않고 생존 플래그 자체를 검사에 쓴다.
        std::vector<uint8_t> alive(capacity, 1);
        for (EntityIndex index : snapshot.freeIndices) {
            if (static_cast<std::size_t>(index) >= capacity || alive[index] == 0) {
                return false;
            }
            alive[index] = 0;
        }

        m_nextEntityIndex = snapshot.nextEntityIndex;
        m_versions = snapshot.versions;
        m_freeIndices = snapshot.freeIndices;
        m_alive = std::move(alive);
        return true;
    }
}
