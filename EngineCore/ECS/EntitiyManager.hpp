#pragma once

#include <vector>

#include "Core.hpp"

namespace EF {
    class EntityManager {
    public:
        EntityId CreateEntity();
        void DestroyEntity(EntityId entity);
        [[nodiscard]] bool IsAlive(EntityId entity) const;
        [[nodiscard]] EntityId ResolveEntityId(EntityIndex index) const;
        [[nodiscard]] EntityIndex EntityCapacity() const noexcept;
        void Clear();

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
}
