#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
#include <span>

#include "Base/Assert.hpp"
#include "Core.hpp"

namespace EF {
    namespace Internal {
        template<typename T, bool IsEnum>
        struct SparseSetKeyIndexType;

        template<typename T>
        struct SparseSetKeyIndexType<T, false> {
            using type = T;
        };

        template<typename T>
        struct SparseSetKeyIndexType<T, true> {
            using type = std::underlying_type_t<T>;
        };
    }

    template<typename T>
    concept IsValidSparseSetKey = std::is_integral_v<T> || std::is_enum_v<T>;

    template<IsValidSparseSetKey TKey, typename TValue>
    class SparseSet {

        // 이넘인경우 underlying type(실제 정수형)으로 변환해서 사용한다. 음수는 허용하지 않는다.
        using KeyIndex = typename Internal::SparseSetKeyIndexType<TKey, std::is_enum_v<TKey>>::type;
    public:
        using DenseIndex = std::size_t;

        using DenseContainer = std::vector<TValue>;
        using EntityContainer = std::vector<TKey>;



    private:
        static constexpr DenseIndex kInvalidIndex = std::numeric_limits<DenseIndex>::max();

        //** entity id -> dense index */
        std::vector<DenseIndex> m_sparseIndices;
        //** dense index -> entity id */
        EntityContainer m_denseKeys;
        //** dense index -> value */
        DenseContainer m_denseValues;

    private:

    
        static DenseIndex ToSparseIndex(TKey key) {
            KeyIndex keyIndex = static_cast<KeyIndex>(key);

            if constexpr (std::is_signed_v<KeyIndex>) {
                EF_ASSERT(keyIndex >= 0, "SparseSet key must be non-negative.");
            }

            return static_cast<DenseIndex>(keyIndex);
        }

        bool TryGetDenseIndex(TKey key, DenseIndex& outDenseIndex) const {
            DenseIndex sparseIndex = ToSparseIndex(key);
            if (sparseIndex >= m_sparseIndices.size()) {
                return false;
            }

            DenseIndex denseIndex = m_sparseIndices[sparseIndex];
            if (denseIndex == kInvalidIndex) {
                return false;
            }

            if (denseIndex >= m_denseKeys.size()) {
                return false;
            }

            if (m_denseKeys[denseIndex] != key) {
                return false;
            }

            outDenseIndex = denseIndex;
            return true;
        }

        void EnsureSparseCapacity(TKey key) {
            DenseIndex sparseIndex = ToSparseIndex(key);
            if (sparseIndex < m_sparseIndices.size()) {
                return;
            }

            m_sparseIndices.resize(sparseIndex + 1, kInvalidIndex);
        }

    public:
        template<typename... Args>
        TValue& Emplace(TKey key, Args&&... args) {
            DenseIndex existingDenseIndex;
            EF_ASSERT(!TryGetDenseIndex(key, existingDenseIndex), "SparseSet element already exists for key.");

            EnsureSparseCapacity(key);

            DenseIndex sparseIndex = ToSparseIndex(key);
            DenseIndex newDenseIndex = m_denseValues.size();

            m_sparseIndices[sparseIndex] = newDenseIndex;
            m_denseKeys.emplace_back(key);
            m_denseValues.emplace_back(std::forward<Args>(args)...);

            return m_denseValues.back();
        }

        [[nodiscard]] bool Contains(TKey key) const {
            DenseIndex denseIndex;
            return TryGetDenseIndex(key, denseIndex);
        }

        TValue* Get(TKey key) {
            DenseIndex denseIndex;
            if (!TryGetDenseIndex(key, denseIndex)) {
                return nullptr;
            }

            return &m_denseValues[denseIndex];
        }

        const TValue* Get(TKey key) const {
            DenseIndex denseIndex;
            if (!TryGetDenseIndex(key, denseIndex)) {
                return nullptr;
            }

            return &m_denseValues[denseIndex];
        }

        /**
         * 지정된 키에 해당하는 요소를 제거합니다.
         * @param key 제거할 요소의 키
         * @todo None(Entt에서는 Tombstone이라고 불리는) 상태를 지원해서 포인터 안정성 보장할수도 있음
         */
        void Remove(TKey key) {
            DenseIndex removeDenseIndex;
            if (!TryGetDenseIndex(key, removeDenseIndex)) {
                return;
            }

            // 가장 마지막 인덱스를 제거할 인덱스에 덮어쓰고, 마지막 요소를 제거하는 방식으로 빠르게 삭제
            DenseIndex lastDenseIndex = m_denseValues.size() - 1;

            // 이미 마지막 요소를 제거하려는 경우는 그냥 제거하도록 if문으로 분기
            if (removeDenseIndex != lastDenseIndex) {
                TKey movedKey = m_denseKeys[lastDenseIndex];

                // std::move를 사용하여 이동 시도
                m_denseValues[removeDenseIndex] = std::move(m_denseValues[lastDenseIndex]);
                m_denseKeys[removeDenseIndex] = movedKey;
                m_sparseIndices[ToSparseIndex(movedKey)] = removeDenseIndex;
            }

            m_denseValues.pop_back();
            m_denseKeys.pop_back();
            m_sparseIndices[ToSparseIndex(key)] = kInvalidIndex;
        }

        [[nodiscard]] std::size_t Size() const noexcept {
            return m_denseValues.size();
        }

        [[nodiscard]] bool Empty() const noexcept {
            return m_denseValues.empty();
        }

        void Clear() {
            m_sparseIndices.clear();
            m_denseKeys.clear();
            m_denseValues.clear();
        }

        [[nodiscard]] std::span<const TKey> Keys() const noexcept {
            return m_denseKeys;
        }

        [[nodiscard]] std::span<TValue> Values() noexcept {
            return m_denseValues;
        }

        [[nodiscard]] std::span<const TValue> Values() const noexcept {
            return m_denseValues;
        }
    };
}
