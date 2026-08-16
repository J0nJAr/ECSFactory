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

    /**
     * 월드 전체를 한 장에 담은 스냅샷.
     *
     * 두 층으로 되어 있다.
     * - `entities` — **엔티티가 존재한다는 사실 자체**(EntityManager 상태). 컴포넌트가
     *   하나도 없는 엔티티도 여기에 담기고, 다음 `CreateEntity()`가 어떤 index/version을
     *   낼지도 여기서 결정된다. 자세한 근거는 `EntitySnapshot` 주석 참고.
     * - `components` — 각 trivially-copyable 풀의 dense 바이트.
     *
     * 순서가 중요하다: 적용은 반드시 `entities` → `components` 순이다(`ApplySnapshot`).
     */
    struct WorldSnapshot {
        uint64_t tick = 0;
        EntitySnapshot entities;
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

        [[nodiscard]] bool IsAlive(EntityId entity) const {
            return m_registry.IsAlive(entity);
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

        /// @brief 해당 타입의 스냅샷 컴포넌트를 등록한다. 
        /// 스냅샷 컴포넌트는 네트워크 동기화 시에만 사용되며,
        /// 반드시 비트 복사가능(trivially copyable)이어야 한다.
        /// @tparam T 
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
            snapshot.entities = m_registry.CreateEntitySnapshot();
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

        /**
         * 스냅샷으로 월드를 통째로 갈아끼운다. 실패하면 기존 월드는 그대로 둔다.
         *
         * **엔티티 → 컴포넌트 순서가 이 함수의 핵심**이고, 이유가 둘이다.
         * 1. 컴포넌트가 하나도 없는 엔티티는 `components`에 흔적이 없다. 엔티티 상태를
         *    먼저 깔지 않으면 그런 엔티티는 스냅샷을 넘어가면서 조용히 사라진다.
         * 2. 먼저 깔지 않으면 `AddComponentRaw` → `EnsureEntityExists`가 `CreateEntity()`를
         *    돌려 free list를 소모한다. 그러면 서버와 클라의 다음 index 할당이 갈라지고,
         *    `ComputeHash()`는 dense entity index를 섞으므로 즉시 해시 불일치 →
         *    resync → 또 불일치의 무한 루프가 된다.
         */
        [[nodiscard]] bool ApplySnapshot(const WorldSnapshot& snapshot) {
            Registry newRegistry;

            if (!newRegistry.RestoreEntities(snapshot.entities)) {
                return false;
            }

            // 살아 있는 index 판정용 비트맵. free list를 매번 선형 탐색하지 않으려고
            // 한 번만 만들어 둔다. RestoreEntities가 통과했으므로 free index는
            // 범위 안이고 중복이 없다.
            std::vector<bool> isAlive(snapshot.entities.versions.size(), true);
            for (EntityIndex freeIndex : snapshot.entities.freeIndices) {
                isAlive[freeIndex] = false;
            }

            for (const ComponentSnapshot& componentSnapshot : snapshot.components) {
                // 죽었거나 존재하지 않는 index에 컴포넌트가 붙은 스냅샷은 자기모순이다.
                //
                // ⚠️ 이 검사는 "우리가 아는 타입인가"보다 **먼저** 와야 한다.
                //    미등록 타입을 먼저 건너뛰면 그 타입에 실린 잘못된 index가 검사되지 않고,
                //    자기모순인 스냅샷을 정상으로 받아들이게 된다. 적용되지 않는 타입이라
                //    당장 월드가 깨지지는 않지만, 거부해야 할 입력을 통과시키는 것 자체가 결함이다.
                //    (등록 여부는 클라마다 다르므로, 같은 스냅샷의 수용/거부 판정이
                //     peer마다 갈리는 것도 곤란하다.)
                for (EntityIndex entityIndex : componentSnapshot.entities) {
                    if (static_cast<std::size_t>(entityIndex) >= isAlive.size() || !isAlive[entityIndex]) {
                        return false;
                    }
                }

                auto applierIt = m_snapshotAppliers.find(componentSnapshot.type.hash);
                if (applierIt == m_snapshotAppliers.end()) {
                    // 이 클라가 등록하지 않은 타입은 적용하지 않는다(거부가 아니라 무시).
                    continue;
                }

                if (!applierIt->second(newRegistry, componentSnapshot)) {
                    return false;
                }
            }

            m_registry = std::move(newRegistry);
            return true;
        }

        /**
         * 결정론 검증용 체크섬. trivially-copyable 풀의 (타입, dense index, 바이트)를 FNV-1a로 섞는다.
         *
         * ⚠️ `EntityManager` 상태(version / free list / nextEntityIndex)는 **일부러 섞지 않는다.**
         * 섞으면 디싱크를 더 빨리 잡을 수 있지만, 지금은 서버가 확정 입력 밖에서 엔티티를
         * 스폰하는 경로가 남아 있어서(`NetworkSyncSystem`의 Hello 처리 → `world.CreateEntity()`)
         * 정상 동작하는 세션조차 불일치로 판정된다. 그 경로를 확정 입력으로 옮기는 것이
         * roadmap의 N2이고, 엔티티 상태를 해시에 넣는 건 그 뒤에 할 일이다.
         */
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
