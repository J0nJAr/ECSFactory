#pragma once

#include <cstddef>
#include <utility>

#include "Core.hpp"
#include "SparseSet.hpp"

namespace EF {
    class IPool {
    public:
        virtual ~IPool() = default;
        virtual void RemoveEntityData(EntityIndex entityIndex) = 0;
        [[nodiscard]] virtual std::size_t Size() const = 0;
        virtual void Clear() = 0;
        [[nodiscard]] virtual std::span<const EntityIndex> GetDenseEntities() const = 0;
    };


    /**
     * 특정 Component 타입 T에 대한 ComponentPool 클래스입니다. EntityIndex와 Component 데이터를 매핑하여 저장합니다.
     * 
     * TODO : 풀을 여러개의 블록으로 나누어서 관리하기
     */
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

        /*
         * IPool
         */
        [[nodiscard]] std::size_t Size() const override {
            return m_components.Size();
        }

        void Clear() override {
            m_components.Clear();
        }

        [[nodiscard]] std::span<const EntityIndex> GetDenseEntities() const override {
            return m_components.Keys();
        }
    };
}
