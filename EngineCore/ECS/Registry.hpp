#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "ComponentPool.hpp"
#include "Core.hpp"
#include "EntitiyManager.hpp"
#include "TypeInfo.hpp"
#include "Base/Assert.hpp"
#include "Base/Logger.hpp"
#include "View.hpp"

namespace EF {
    class Registry {
    private:
        EntityManager m_entityManager;
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

        void DestroyEntity(EntityId entity) {
            if (!m_entityManager.IsAlive(entity)) return;

            EntityIndex index = GetEntityIndex(entity);
            // 맵을 순회하는 것보다 벡터를 순회하는 게 캐시 히트율이 훨씬 높다
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

        template<typename... ComponentTypes>
        View<ComponentTypes...> GetView() {

            return View<ComponentTypes...>();
        }
    };
}
