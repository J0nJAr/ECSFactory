#pragma once

#include <queue>

#include "Core.hpp"
#include "Registry.hpp"


namespace EF {
    class EntityCommandBuffer {
        
        class Command {
            enum class Type {
                AddComponent,
                RemoveComponent,
                DestroyEntity
            };

            Type type;
            EntityId entity;
            
        };

        std::queue<Command> commands;
    
    public:
        void AddComponent(EntityId entity, TypeHash componentType, void* componentData);
        void RemoveComponent(EntityId entity, TypeHash componentType);
        void DestroyEntity(EntityId entity);

        void Execute(Registry& registry);
    
    };
    
    

                
}