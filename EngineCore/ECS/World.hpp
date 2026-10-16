#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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

    // 시뮬레이션 한 덩어리. Registry(엔티티·컴포넌트) + SystemRegistry(시스템) +
    // 타입 키 리소스 맵(월드별 싱글턴, 예: NetworkManager)의 조합이다.
    //
    // 한 GameEngine이 World를 여러 개 가질 수 있다. 호스트·싱글플레이는 같은 엔진 안에
    // ServerSimulation 월드와 ClientPresentation 월드를 함께 두고 인메모리로 연결한다.
    //
    // 보장:
    //  - CreateSnapshot / ApplySnapshot은 trivially-copyable 컴포넌트만 다룬다.
    //    ApplySnapshot은 엔티티 할당 상태를 먼저 복원한 뒤 컴포넌트를 채우고,
    //    타입 모양이나 엔티티 index가 어긋나면 아무것도 바꾸지 않고 거부한다.
    //  - ComputeHash는 trivial 풀의 내용으로 결정론 체크섬을 만든다. 풀 순회 순서는
    //    TypeInfo::hash 오름차순이라 프로세스가 달라도 같은 내용이면 같은 값이 나온다.
    //
    // 이 타입이 하지 않는 일:
    //  - 네트워크를 모른다. 스냅샷을 만들고 적용할 뿐, 보내는 건 NetworkSyncSystem이다.
    //  - 시스템 실행 순서를 정하지 않는다(SystemRegistry::SortSystems는 아직 TODO라
    //    등록 순서대로 실행된다).
    class World {
    private:
        Registry m_registry;
        SystemRegistry m_systems;
        std::unordered_map<TypeHash, std::unique_ptr<void, void(*)(void*)>> m_resources;
        std::unordered_map<TypeHash, std::function<bool(Registry&, const ComponentSnapshot&)>> m_snapshotAppliers;

        // ComputeHash에서 제외할 타입들(클라이언트 전용 표현 컴포넌트).
        // RegisterPresentationComponent 주석 참조.
        std::unordered_set<TypeHash> m_presentationTypes;

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

        /// @brief 쿼리에 맞는 엔티티를 순회하는 View를 만든다.
        ///
        /// 제외 목록도 그대로 전달한다:
        ///   world.CreateView<Transform, PlayerControl>()
        ///   world.CreateView<Transform, PlayerControl>(Without<Renderable>{})
        ///
        /// 이 ECS에는 `HasComponent` 가 없다. "이 컴포넌트가 붙어 있나"를 묻는 자리는
        /// 전부 `Without<...>` 뷰로 표현한다 — 개별 조회를 열어 두면 뷰를 안 쓰고
        /// 엔티티를 하나씩 훑는 코드가 생기기 쉽고, 그건 dense 순회의 이점을 버린다.
        template<typename... T, typename... Excluded>
        auto CreateView(Without<Excluded...> without = {}) {
            return m_registry.CreateView<T...>(without);
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

        /// @brief 이 타입을 **결정론 해시에서 제외**한다. 클라이언트 전용 표현 컴포넌트용.
        ///
        /// ⚠️ 이게 왜 필요한가 (모르면 무한 resync에 빠진다):
        ///
        /// `ComputeHash()` 는 trivially-copyable 풀을 **전부** 섞는다. 그런데 클라이언트에는
        /// 서버에 없는 컴포넌트가 붙는다 — `Renderable` 처럼 "어떻게 그릴 것인가"를 담는 것들이다.
        /// 그 풀이 해시에 들어가면 클라 해시가 서버 해시와 **매 tick 어긋나고**, `TickHash`
        /// 비교가 이걸 디싱크로 읽어 `ResyncRequest` 를 보낸다. 새 스냅샷이 와서 레지스트리가
        /// 교체되고, 표현 컴포넌트가 다시 붙고, 또 어긋난다. 화면은 멀쩡해 보이는데 네트워크가
        /// 스냅샷을 계속 실어 나르는 상태가 된다.
        ///
        /// 제외해도 안전한 이유: 이 컴포넌트들은 **어떤 peer의 시뮬레이션에도 영향을 주지 않는다.**
        /// 색과 크기가 클라마다 달라도 게임 상태는 같다. 해시가 지켜야 하는 것은
        /// "모든 peer의 시뮬레이션이 일치하는가"이지 "모든 peer가 똑같이 생겼는가"가 아니다.
        ///
        /// ⚠️ 반대로 **시뮬레이션에 영향을 주는 컴포넌트를 여기 등록하면 안 된다.**
        ///    그러면 디싱크가 나도 해시가 조용해서 영영 못 잡는다. 기본값은 "해시에 포함"이고
        ///    이 함수는 명시적인 예외 선언이다.
        template<typename T>
        void RegisterPresentationComponent() {
            m_presentationTypes.insert(GetTypeInfo<T>().hash);
        }

        /// @brief 해당 타입의 스냅샷 컴포넌트를 등록한다.
        /// 스냅샷 컴포넌트는 네트워크 동기화 시에만 사용되며,
        /// 반드시 비트 복사가능(trivially copyable)이어야 한다.
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
         * **풀 순회 순서는 `TypeInfo::hash` 오름차순으로 고정한다.** 이게 이 함수의 유일한
         * 비자명한 부분이라 이유를 남긴다.
         *
         * `Registry::GetPools()` 가 돌려주는 벡터는 `TypeInfo::index` 로 인덱싱되어 있고,
         * 그 index는 "이 프로세스에서 그 타입을 처음 쓴 순서"로 매겨지는 런타임 카운터다.
         * 즉 **프로세스마다 다르다.** FNV-1a는 순서에 민감하므로, 클라와 서버가 타입을 다른
         * 순서로 처음 쓰면 월드 내용이 완전히 같아도 해시가 갈라진다. 그러면 `TickHash`
         * 비교가 이걸 디싱크로 읽어 `ResyncRequest` → 새 스냅샷 → 또 불일치의 무한 루프가 된다.
         * (이 저장소의 규칙은 "네트워크를 넘는 비교는 index가 아니라 hash로"인데,
         *  정작 체크섬 함수 자신이 그걸 어기고 있었다.)
         *
         * 버린 대안:
         * - **순서 무관한 결합**(풀별 부분해시를 XOR 또는 덧셈으로 합치기). 정렬이 필요 없어
         *   제일 싸지만, 교환법칙이 성립하는 결합은 충돌을 만들기 쉽다 — XOR이면 내용이 같은
         *   두 풀이 서로를 지운다. 체크섬의 존재 이유가 차이를 잡는 것이므로 탐지력을 깎는
         *   교환은 하지 않는다.
         * - **`Registry` 가 풀을 TypeHash 순으로 정렬해 보관하기.** 그러면 여기선 그냥 돌면
         *   되지만 index 기반 O(1) 풀 조회를 잃는다. 조회는 컴포넌트를 만질 때마다 일어나고
         *   `ComputeHash` 는 tick당 한 번이다 — 비용은 드문 쪽이 무는 게 맞다.
         *
         * 비용: 컴포넌트 타입 수 P는 수십 규모라 O(P log P) 정렬 + 작은 vector 하나의 할당은,
         * 이 함수가 어차피 도는 "모든 풀의 전체 바이트"(수천~수만 바이트) 앞에서 무시할 수준이다.
         * 프로파일에 잡히면 그때 캐시를 넣되, 풀 추가·삭제 무효화까지 함께 설계해야 한다.
         *
         * ⚠️ 함정: 서로 다른 두 타입이 같은 `TypeHash` 를 가지면(= 타입 이름 충돌 또는
         *    `TypeName` 중복 지정) 정렬이 동률이 되고, 동률의 상대 순서는 다시 index 순서
         *    ─ 즉 프로세스 로컬 ─ 로 돌아간다. `std::stable_sort` 로 바꿔도 소용없다.
         *    정렬 전 순서 자체가 프로세스마다 다르기 때문이다. 해시 충돌은 여기서 고칠 수 있는
         *    문제가 아니라 타입 이름 쪽에서 막아야 하는 문제다.
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

            // 1단계 — 섞을 풀을 고른다. 여기서는 순서를 신경 쓰지 않는다.
            std::vector<const IPool*> hashedPools;
            hashedPools.reserve(m_registry.GetPools().size());

            for (const auto& pool : m_registry.GetPools()) {
                if (!pool || !pool->GetType().isTrivial) {
                    continue;
                }

                // 표현 전용 컴포넌트는 시뮬레이션 상태가 아니므로 건너뛴다.
                // 자세한 이유는 RegisterPresentationComponent 주석에 있다.
                if (m_presentationTypes.contains(pool->GetType().hash)) {
                    continue;
                }

                hashedPools.push_back(pool.get());
            }

            // 2단계 — 프로세스 간에 안정적인 기준으로 순서를 고정한다. 위 함수 주석 참조.
            std::sort(
                hashedPools.begin(),
                hashedPools.end(),
                [](const IPool* lhs, const IPool* rhs) {
                    return lhs->GetType().hash < rhs->GetType().hash;
                });

            // 3단계 — 고정된 순서로 섞는다.
            for (const IPool* pool : hashedPools) {
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
