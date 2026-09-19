#pragma once

#include <span>
#include <tuple>
#include <type_traits>

#include "ComponentPool.hpp"
#include "Core.hpp"

namespace EF {
    class Registry;

    template<typename WithList, typename WithoutList>
    class ViewIterator;

    template<typename WithList, typename WithoutList>
    class View;

    /**
     * ViewIterator는 View가 고른 엔티티를 반복자 형태로 순회할 수 있게 해준다.
     *
     * 내부 구현은 ComponentPool의 dense key인 EntityIndex를 기준으로 움직이지만,
     * 외부 API에서는 엔티티를 식별하는 정식 값인 EntityId를 반환한다.
     */
    template<typename... IncludedTypes, typename... ExcludedTypes>
    class ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>> {
    private:
        using ViewType = View<With<IncludedTypes...>, Without<ExcludedTypes...>>;

        const ViewType* m_view;
        std::span<const EntityIndex> m_denseEntities;
        std::size_t m_index;

        void SkipInvalid();

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = std::tuple<EntityId, IncludedTypes&...>;
        using difference_type = std::ptrdiff_t;

        ViewIterator(const ViewType* view, std::span<const EntityIndex> entities, std::size_t index);
        ViewIterator& operator++();
        auto operator*() const;
        bool operator==(const ViewIterator& other) const;
        bool operator!=(const ViewIterator& other) const;
    };

    /**
     * View는 Registry에 등록된 엔티티 중
     * 특정 컴포넌트 조건을 만족하는 엔티티만 골라 순회할 수 있게 해주는 ECS 쿼리 객체다.
     *
     * 입력:
     * - 반드시 포함되어야 하는 컴포넌트 목록
     * - 반드시 제외되어야 하는 컴포넌트 목록
     *
     * 출력:
     * - `ForEach`를 통한 직접 순회
     * - `ViewIterator`를 통한 반복자 기반 순회
     *
     * 구현 전략:
     * - 포함 컴포넌트 중 가장 작은 풀을 기준으로 순회를 시작한다.
     * - 내부 풀 접근과 필터링은 EntityIndex 기준으로 수행한다.
     *
     * ⚠️ **두 순회가 넘겨주는 엔티티 값의 타입이 다르다.**
     *    - `ViewIterator`(range-for)는 `ResolveEntityId`를 거쳐 **`EntityId`**(version 포함)를 준다.
     *    - `ForEach`는 **`EntityIndex`**(version 없음)를 그대로 준다.
     *
     *    `EntityIndex`(uint32)는 `EntityId`(uint64)로 암묵 변환되므로, 콜백을
     *    `[](EntityId e, ...)`로 적어도 **컴파일이 통과한다.** 그리고 version 비트가 0인
     *    가짜 id가 만들어진다. 갓 만든 엔티티는 version이 0이라 우연히 맞아떨어지고,
     *    파괴 후 재사용된 index에서만 틀어진다 — `Registry::AddComponent`의
     *    `IsAlive` 단언이 그때 터진다.
     *
     *    그래서 `ForEach` 콜백의 첫 인자는 **반드시 `EntityIndex`로 받는다.**
     *    `EntityId`가 필요하면 `view.ResolveEntityId(index)`로 복원할 것.
     */
    template<typename... IncludedTypes, typename... ExcludedTypes>
    requires(sizeof...(IncludedTypes) > 0)
    class View<With<IncludedTypes...>, Without<ExcludedTypes...>> {
        friend class ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>;

    private:
        const Registry* m_registry = nullptr;

        // 반드시 포함되어야 하는 컴포넌트 풀들
        std::tuple<ComponentPool<std::remove_const_t<IncludedTypes>>*...> m_pools;

        // 반드시 제외되어야 하는 컴포넌트 풀들
        std::tuple<ComponentPool<std::remove_const_t<ExcludedTypes>>*...> m_excludePools;

        IPool* m_smallestPool = nullptr;
        bool m_isValid = false;

    public:
        using iterator = ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>;

        View(
            const Registry* registry,
            std::tuple<ComponentPool<std::remove_const_t<IncludedTypes>>*...> pools,
            std::tuple<ComponentPool<std::remove_const_t<ExcludedTypes>>*...> excludePools)
            : m_registry(registry), m_pools(pools), m_excludePools(excludePools) {

            // 포함 컴포넌트 중 가장 작은 풀을 찾는다.
            // 이후 View 순회는 이 풀의 dense entity 목록을 기준으로 진행한다.
            m_isValid = true;
            (([&] {
                if (!m_isValid) {
                    return;
                }

                auto pool = std::get<ComponentPool<std::remove_const_t<IncludedTypes>>*>(m_pools);
                if (pool) {
                    if (!m_smallestPool || pool->Size() < m_smallestPool->Size()) {
                        m_smallestPool = pool;
                    }
                } else {
                    // 포함 대상 컴포넌트 풀이 하나라도 없으면 View는 성립하지 않는다.
                    m_isValid = false;
                }
            }()), ...);
        }

        /**
         * View 조건을 만족하는 엔티티를 순회하며,
         * 각 엔티티의 포함 컴포넌트를 함수 인자로 전달한다.
         *
         * 이 경로는 Entity 자체가 아니라 컴포넌트 참조만 필요할 때 사용한다.
         */
        template<typename Func>
        void ForEach(Func&& func) const {
            if (!m_isValid || !m_smallestPool) {
                return;
            }

            for (EntityIndex entityIndex : m_smallestPool->GetDenseEntities()) {
                
                // 포함 컴포넌트를 미리 가져오기
                auto componentPtrs = std::make_tuple(GetComponent<IncludedTypes>(entityIndex)...);

                // 긁어온 포인터들 중 하나라도 nullptr이면 탈락. HasInclude함수를 사용하면 O(2)기 때문에 내부에서 검증
                bool hasAllIncluded = (... && (std::get<std::remove_const_t<IncludedTypes>*>(componentPtrs) != nullptr));

                // 포함 컴포넌트는 모두 있고, 제외 컴포넌트는 없는 경우에만 func을 호출한다.
                if (hasAllIncluded && !HasExcludedComponents(entityIndex)) { // HasExcludedComponents는 0개인경우 false반환이므로 컴파일러가 제외함
                    
                    // 사용자가 던진 func의 인자 형태를 컴파일러가 보고 알아서 매칭해줍니다.
                    if constexpr (std::is_invocable_v<Func, EntityIndex, IncludedTypes&...>) {
                        // 사용자가 EntityIndex를 원할 때: func(entity, compA, compB)
                        func(entityIndex, *std::get<std::remove_const_t<IncludedTypes>*>(componentPtrs)...);
                    } 
                    else if constexpr (std::is_invocable_v<Func, IncludedTypes&...>) {
                        // 사용자가 컴포넌트만 원할 때: func(compA, compB)
                        func(*std::get<std::remove_const_t<IncludedTypes>*>(componentPtrs)...);
                    } 
                    else {
                        // 사용자가 인자를 이상하게 넣으면 컴파일 에러로 멱살을 잡습니다.
                        static_assert(!std::is_same_v<Func, Func>, "[EF Engine Error] ForEach callback signature is invalid! "
                                            "Expected: (EntityIndex, Components&...) OR (Components&...)");
                    }
                }
            }
        }

        iterator begin() const {
            if (!m_isValid || !m_smallestPool) {
                return end();
            }

            return iterator(this, m_smallestPool->GetDenseEntities(), 0);
        }

        iterator end() const {
            std::span<const EntityIndex> entities;
            if (m_isValid && m_smallestPool) {
                entities = m_smallestPool->GetDenseEntities();
            }

            return iterator(this, entities, entities.size());
        }

    private:
        bool IsValidEntity(EntityIndex entityIndex) const {
            return HasIncludedComponents(entityIndex) && !HasExcludedComponents(entityIndex);
        }

        EntityId ResolveEntityId(EntityIndex entityIndex) const;

        bool HasIncludedComponents(EntityIndex entityIndex) const {
            return (... && (std::get<ComponentPool<std::remove_const_t<IncludedTypes>>*>(m_pools)->Get(entityIndex) != nullptr));
        }

        bool HasExcludedComponents(EntityIndex entityIndex) const {
            if constexpr (sizeof...(ExcludedTypes) == 0) {
                return false;
            } else {
                return (... || HasExcludedComponent<ExcludedTypes>(entityIndex));
            }
        }

        template<typename T>
        bool HasExcludedComponent(EntityIndex entityIndex) const {
            auto pool = std::get<ComponentPool<std::remove_const_t<T>>*>(m_excludePools);
            return pool && pool->Get(entityIndex) != nullptr;
        }

        template<typename T>
        std::remove_const_t<T>* GetComponent(EntityIndex entityIndex) const {
            auto pool = GetComponentPool<std::remove_const_t<T>>();
            if (!pool) {
                return nullptr;
            }
            return pool->Get(entityIndex);
        }

        template<typename T>
        auto GetComponentPool() const {
            return std::get<ComponentPool<std::remove_const_t<T>>*>(m_pools);
        }
    };

    /*
     * ViewIterator 구현부
     * 템플릿 클래스이므로 헤더에 구현을 둔다.
     */

    template<typename... IncludedTypes, typename... ExcludedTypes>
    void ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::SkipInvalid() {
        while (m_index < m_denseEntities.size()) {
            const EntityIndex entityIndex = m_denseEntities[m_index];
            if (m_view->IsValidEntity(entityIndex)) {
                break;
            }
            ++m_index;
        }
    }

    template<typename... IncludedTypes, typename... ExcludedTypes>
    ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::ViewIterator(
        const ViewType* view,
        std::span<const EntityIndex> entities,
        std::size_t index)
        : m_view(view), m_denseEntities(entities), m_index(index) {
        SkipInvalid();
    }

    template<typename... IncludedTypes, typename... ExcludedTypes>
    ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>&
    ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::operator++() {
        ++m_index;
        SkipInvalid();
        return *this;
    }

    template<typename... IncludedTypes, typename... ExcludedTypes>
    auto ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::operator*() const {
        const EntityIndex entityIndex = m_denseEntities[m_index];
        return std::tuple<EntityId, IncludedTypes&...>(
            m_view->ResolveEntityId(entityIndex),
            *(m_view->template GetComponent<IncludedTypes>(entityIndex))...);
    }

    template<typename... IncludedTypes, typename... ExcludedTypes>
    bool ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::operator==(const ViewIterator& other) const {
        return m_index == other.m_index;
    }

    template<typename... IncludedTypes, typename... ExcludedTypes>
    bool ViewIterator<With<IncludedTypes...>, Without<ExcludedTypes...>>::operator!=(const ViewIterator& other) const {
        return !(*this == other);
    }
}
