#pragma once

#include <utility>

#include "Core.hpp"
#include "SparseSet.hpp"

namespace EF {
    class IPool {
    public:
        virtual ~IPool() = default;
        virtual void RemoveEntityData(EntityIndex entityIndex) = 0;
    };

    template<typename T>
    class ComponentPool : public IPool {
    private:
        SparseSet<EntityIndex, T> m_components;

    public:
        template<typename... Args>
        T& Add(EntityIndex entityIndex, Args&&... args) {
            return m_components.Emplace(entityIndex, std::forward<Args>(args)...);
        }

        T* Get(EntityIndex entityIndex) {
            return m_components.Get(entityIndex);
        }

        const T* Get(EntityIndex entityIndex) const {
            return m_components.Get(entityIndex);
        }

        void Remove(EntityIndex entityIndex) {
            m_components.Remove(entityIndex);
        }

        void RemoveEntityData(EntityIndex entityIndex) override {
            Remove(entityIndex);
        }
    };
}
