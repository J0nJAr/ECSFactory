#pragma once

#include <memory>
#include <type_traits>
#include <vector>


#include "Core.hpp"
#include "TypeInfo.hpp"

namespace EF {

    class ISystem;
    
    template<typename T>
    struct Before {
        using Type = T;
    };

    template<typename T>
    struct After {
        using Type = T;
    };

    /**
    시스템을 등록하고 정렬해주는 클래스
    
    필요한것
    - 시스템 등록
    - 시스템 정렬 (의존성 기반)
      - 방안 1 : int 기반 우선순위
      - 방안 2 : Before<>, After<> 기반 의존성 명시 << 가장 깔끔한듯
      - 방안 3 : 알아서 의존성 분석해서 위상 정렬 
    */
    class SystemRegistry {
    private:
        std::vector<std::unique_ptr<ISystem>> m_systems;

    public:
        SystemRegistry() = default;

        template<typename T, typename... Args>
        void RegisterSystem(Args&&... args) {
            static_assert(std::is_base_of_v<ISystem, T>, "T must derive from ISystem");
            m_systems.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        }

        [[nodiscard]] const std::vector<std::unique_ptr<ISystem>>& GetSystems() const noexcept {
            return m_systems;
        }

        void SortSystems() {
            // TODO: 시스템 정렬 로직 구현 (의존성 기반)
        }
    };
    
}