#pragma once

#include <unordered_map>

// #include "Base/GameEngine.hpp"
#include "ECS/Registry.hpp"
#include "ECS/SystemRegistry.hpp"
#include "ECS/ISystem.hpp"

namespace EF {



    class World {
    private:
    
        Registry m_registry;
        SystemRegistry m_systems;

        // 타입당 하나씩 리소스를 저장하는 맵. 시스템들이 공유하는 리소스들을 저장할 때 사용. ex) InputManager
        std::unordered_map<TypeHash, std::unique_ptr<void, void(*)(void*)>> m_resources;

    public:
        EntityId CreateEntity();

        void DestroyEntity(EntityId entity){
            m_registry.DestroyEntity(entity);
        }
        template<typename T, typename... Args>
        T& AddComponent(EntityId entity, Args&&... args){
            return m_registry.AddComponent<T>(entity, std::forward<Args>(args)...);
        }

        template<typename... T>
        auto CreateView(){
            return m_registry.CreateView<T...>();
        }

        template<typename TSystem, typename... Args>
        TSystem& RegisterSystem(Args&&... args){
            m_systems.RegisterSystem<TSystem>(std::forward<Args>(args)...);
            return static_cast<TSystem&>(*m_systems.GetSystems().back());
        }
        void Initialize() {
            for (const auto& system : m_systems.GetSystems()) {
                system->Initialize(*this);
            }
        }

        void FixedUpdate(Fixed64 dt, uint64_t tick) {
            for (const auto& system : m_systems.GetSystems()) {
                system->FixedUpdate(*this, dt, tick);
            }
        }
        void FrameUpdate(float dt){
            for (const auto& system : m_systems.GetSystems()) {
                system->FrameUpdate(*this, dt);
            }
        }
        void Shutdown(){
            for (const auto& system : m_systems.GetSystems()) {
                system->Shutdown(*this);
            }
        }

        // 리소스 추가 ex : world.AddResource<InputState>(...q
        template<typename T, typename... Args>
        T& AddResource(Args&&... args) {
            TypeHash hash = GetTypeInfo<T>().hash;
            auto ptr = new T(std::forward<Args>(args)...);
            
            // 삭제자(Deleter)를 함께 넘겨서 void* 래핑 해제 시 메모리 누수를 막습니다.
            m_resources.emplace(hash, std::unique_ptr<void, void(*)(void*)>(ptr, [](void* p) { delete static_cast<T*>(p); }));
            
            return *ptr;
        }

        // 리소스 가져오기 ex : InputState& input = world.GetResource<InputState>();
        template<typename T>
        T& GetResource() {
            TypeHash hash = GetTypeInfo<T>().hash;
            EF_ASSERT(m_resources.contains(hash), "Resource does not exist!");
            return *static_cast<T*>(m_resources[hash].get());
        }


    };


    }