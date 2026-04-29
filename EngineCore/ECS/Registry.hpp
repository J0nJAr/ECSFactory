#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "Base/Assert.hpp"
#include "Base/Logger.hpp"
#include "ComponentPool.hpp"
#include "Core.hpp"
#include "EntitiyManager.hpp"
#include "TypeInfo.hpp"
#include "View.hpp"

namespace EF {
    /**
     * Registry는 Entity와 ComponentPool을 관리하는 클래스다.
     * Entity의 생성과 소멸, Component의 추가와 제거를 담당한다.
     *
     * TODO : Group 기능(특정 Component 조합을 가진 엔티티를 묶어놓기)
     */
    class Registry {
    private:
        EntityManager m_entityManager;

        /**
         * ComponentPool을 타입 인덱스에 매핑하여 저장하는 벡터다.
         * 각 풀은 실제 타입별로 하나씩 생성되며, 존재하지 않는 타입 슬롯은 nullptr일 수 있다.
         */
        std::vector<std::unique_ptr<IPool>> m_pools;

    private:
        template<typename T>
        ComponentPool<T>* GetPool() {
            const uint32_t index = GetTypeInfo<T>().index;
            if (index >= m_pools.size() || !m_pools[index]) {
                return nullptr;
            }
            return static_cast<ComponentPool<T>*>(m_pools[index].get());
        }

        template<typename T>
        const ComponentPool<T>* GetPool() const {
            const uint32_t index = GetTypeInfo<T>().index;
            if (index >= m_pools.size() || !m_pools[index]) {
                return nullptr;
            }

            return static_cast<const ComponentPool<T>*>(m_pools[index].get());
        }

        template<typename T>
        ComponentPool<T>* GetOrCreatePool() {
            const uint32_t index = GetTypeInfo<T>().index;
            if (index >= m_pools.size() || !m_pools[index]) {
                m_pools.resize(std::max(m_pools.size(), static_cast<size_t>(index + 1)));
                m_pools[index] = std::make_unique<ComponentPool<T>>();
            }
            return static_cast<ComponentPool<T>*>(m_pools[index].get());
        }

    public:
        EntityId CreateEntity() {
            return m_entityManager.CreateEntity();
        }

        [[nodiscard]] EntityId ResolveEntityId(EntityIndex index) const {
            return m_entityManager.ResolveEntityId(index);
        }

        void DestroyEntity(EntityId entity) {
            if (!m_entityManager.IsAlive(entity)) {
                return;
            }

            const EntityIndex index = GetEntityIndex(entity);

            // 풀을 타입 인덱스 기준 벡터로 관리하므로
            // 파괴 시에도 전체 풀을 순회하는 방식이 단순하고 캐시 친화적이다.
            for (auto& pool : m_pools) {
                if (pool) {
                    pool->RemoveEntityData(index);
                }
            }

            m_entityManager.DestroyEntity(entity);
        }

        template<typename T, typename... Args>
        T& AddComponent(EntityId entity, Args&&... args) {
            EF_ASSERT(m_entityManager.IsAlive(entity), "Invalid entity.");
            return GetOrCreatePool<T>()->Add(GetEntityIndex(entity), std::forward<Args>(args)...);
        }

        
        bool IsAlive(EntityId entity) const {
            return m_entityManager.IsAlive(entity);
        }


        /**
         * 포함할 컴포넌트 목록과 제외할 컴포넌트 목록을 기반으로 View를 생성한다.
         *
         * 예:
         * - `CreateView<Position, Velocity>()`
         * - `CreateView<Position, Velocity>(Without<Sleeping>{})`
         */
        template<typename... ComponentTypes, typename... ExcludedComponentTypes>
        View<With<ComponentTypes...>, Without<ExcludedComponentTypes...>> CreateView(Without<ExcludedComponentTypes...> = {}) {
            return View<With<ComponentTypes...>, Without<ExcludedComponentTypes...>>(
                this,
                std::make_tuple(GetPool<std::remove_const_t<ComponentTypes>>()...),
                std::make_tuple(GetPool<std::remove_const_t<ExcludedComponentTypes>>()...)
            );
        }
    };
}

namespace EF {
    template<typename... IncludedTypes, typename... ExcludedTypes>
    requires(sizeof...(IncludedTypes) > 0)
    EntityId View<With<IncludedTypes...>, Without<ExcludedTypes...>>::ResolveEntityId(EntityIndex entityIndex) const {
        return m_registry ? m_registry->ResolveEntityId(entityIndex) : kNullEntity;
    }
}
