#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Base/Assert.hpp"
#include "ECS/ISystem.hpp"
#include "ECS/Registry.hpp"
#include "ECS/SystemRegistry.hpp"
#include "ECS/TypeInfo.hpp"
#include "Math/Fixed64.hpp"

namespace EF {

    struct ComponentTypeDescriptor {
        TypeHash hash = 0;
        std::size_t size = 0;
        std::size_t alignment = 0;
        bool isTrivial = false;
    };

    struct ComponentSnapshot {
        ComponentTypeDescriptor type;
        std::vector<EntityIndex> entities;
        std::vector<std::byte> bytes;
    };

    struct WorldSnapshot {
        uint64_t tick = 0;
        std::vector<ComponentTypeDescriptor> typeTable;
        std::vector<ComponentSnapshot> components;
    };

    class World {
    private:
        Registry m_registry;
        SystemRegistry m_systems;
        std::unordered_map<TypeHash, std::unique_ptr<void, void(*)(void*)>> m_resources;
        std::unordered_map<TypeHash, std::function<bool(Registry&, const ComponentSnapshot&)>> m_snapshotAppliers;

    public:
        EntityId CreateEntity() {
            return m_registry.CreateEntity();
        }

        void DestroyEntity(EntityId entity) {
            m_registry.DestroyEntity(entity);
        }

        template<typename T, typename... Args>
        T& AddComponent(EntityId entity, Args&&... args) {
            return m_registry.AddComponent<T>(entity, std::forward<Args>(args)...);
        }

        template<typename... T>
        auto CreateView() {
            return m_registry.CreateView<T...>();
        }

        template<typename TSystem, typename... Args>
        TSystem& RegisterSystem(Args&&... args) {
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

        void FrameUpdate(float dt) {
            for (const auto& system : m_systems.GetSystems()) {
                system->FrameUpdate(*this, dt);
            }
        }

        void Shutdown() {
            for (const auto& system : m_systems.GetSystems()) {
                system->Shutdown(*this);
            }
        }

        template<typename T, typename... Args>
        T& AddResource(Args&&... args) {
            const TypeHash hash = GetTypeInfo<T>().hash;
            auto* ptr = new T(std::forward<Args>(args)...);
            auto resource = std::unique_ptr<void, void(*)(void*)>(
                ptr,
                [](void* value) { delete static_cast<T*>(value); }
            );
            m_resources.insert_or_assign(hash, std::move(resource));
            return *ptr;
        }

        template<typename T>
        T& GetResource() {
            const TypeHash hash = GetTypeInfo<T>().hash;
            auto it = m_resources.find(hash);
            EF_ASSERT(it != m_resources.end(), "Resource does not exist.");
            return *static_cast<T*>(it->second.get());
        }

        template<typename T>
        void RegisterSnapshotComponent() {
            static_assert(std::is_trivially_copyable_v<T>, "Snapshot components must be trivially copyable.");

            const TypeInfo& type = GetTypeInfo<T>();
            m_snapshotAppliers[type.hash] = [](Registry& registry, const ComponentSnapshot& snapshot) {
                const TypeInfo& localType = GetTypeInfo<T>();
                if (snapshot.type.hash != localType.hash ||
                    snapshot.type.size != localType.size ||
                    snapshot.type.alignment != localType.alignment ||
                    !snapshot.type.isTrivial) {
                    return false;
                }

                const std::size_t expectedBytes = snapshot.entities.size() * sizeof(T);
                if (snapshot.bytes.size() != expectedBytes) {
                    return false;
                }

                const std::byte* cursor = snapshot.bytes.data();
                for (EntityIndex entityIndex : snapshot.entities) {
                    registry.AddComponentRaw<T>(entityIndex, cursor);
                    cursor += sizeof(T);
                }
                return true;
            };
        }

        [[nodiscard]] std::vector<ComponentTypeDescriptor> BuildTypeTable() const {
            std::vector<ComponentTypeDescriptor> types;
            for (const auto& pool : m_registry.GetPools()) {
                if (!pool) {
                    continue;
                }

                const TypeInfo& type = pool->GetType();
                types.push_back(ComponentTypeDescriptor{
                    .hash = type.hash,
                    .size = type.size,
                    .alignment = type.alignment,
                    .isTrivial = type.isTrivial
                });
            }
            return types;
        }

        [[nodiscard]] WorldSnapshot CreateSnapshot(uint64_t tick) const {
            WorldSnapshot snapshot;
            snapshot.tick = tick;
            snapshot.typeTable = BuildTypeTable();

            for (const auto& pool : m_registry.GetPools()) {
                if (!pool) {
                    continue;
                }

                const TypeInfo& type = pool->GetType();
                if (!type.isTrivial) {
                    continue;
                }

                ComponentSnapshot componentSnapshot;
                componentSnapshot.type = ComponentTypeDescriptor{
                    .hash = type.hash,
                    .size = type.size,
                    .alignment = type.alignment,
                    .isTrivial = type.isTrivial
                };

                const auto entities = pool->GetDenseEntities();
                componentSnapshot.entities.assign(entities.begin(), entities.end());

                const auto bytes = pool->GetRawBytes();
                componentSnapshot.bytes.assign(bytes.begin(), bytes.end());

                snapshot.components.push_back(std::move(componentSnapshot));
            }

            return snapshot;
        }

        [[nodiscard]] bool ApplySnapshot(const WorldSnapshot& snapshot) {
            Registry newRegistry;

            for (const ComponentSnapshot& componentSnapshot : snapshot.components) {
                auto applierIt = m_snapshotAppliers.find(componentSnapshot.type.hash);
                if (applierIt == m_snapshotAppliers.end()) {
                    continue;
                }
                if (!applierIt->second(newRegistry, componentSnapshot)) {
                    return false;
                }
            }

            m_registry = std::move(newRegistry);
            return true;
        }

        [[nodiscard]] uint64_t ComputeHash() const {
            uint64_t hash = 14695981039346656037ull;
            auto mixByte = [&hash](std::byte value) {
                hash ^= static_cast<uint64_t>(std::to_integer<uint8_t>(value));
                hash *= 1099511628211ull;
            };
            auto mixUInt64 = [&mixByte](uint64_t value) {
                for (int shift = 56; shift >= 0; shift -= 8) {
                    mixByte(static_cast<std::byte>((value >> shift) & 0xFF));
                }
            };

            for (const auto& pool : m_registry.GetPools()) {
                if (!pool || !pool->GetType().isTrivial) {
                    continue;
                }

                const TypeInfo& type = pool->GetType();
                mixUInt64(type.hash);
                mixUInt64(static_cast<uint64_t>(type.size));

                for (EntityIndex entity : pool->GetDenseEntities()) {
                    mixUInt64(entity);
                }

                for (std::byte value : pool->GetRawBytes()) {
                    mixByte(value);
                }
            }

            return hash;
        }
    };

}
