#pragma once

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "Base/Assert.hpp"

namespace EF {
    template<typename TKey, typename TValue>
    class SparseSet {
        using DenseIndex = std::size_t;
        using KeyIndex = std::conditional_t<std::is_enum_v<TKey>, std::underlying_type_t<TKey>, TKey>;

        static_assert(std::is_integral_v<KeyIndex>, "SparseSet key must be an integral or enum type.");

    private:
        static constexpr DenseIndex kInvalidIndex = std::numeric_limits<DenseIndex>::max();

        //** entity id -> dense index */
        std::vector<DenseIndex> m_sparseIndices;
        //** dense index -> entity id */
        std::vector<TKey> m_denseKeys;
        //** dense index -> value */
        std::vector<TValue> m_denseValues;

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

        void Remove(TKey key) {
            DenseIndex removeDenseIndex;
            if (!TryGetDenseIndex(key, removeDenseIndex)) {
                return;
            }

            DenseIndex lastDenseIndex = m_denseValues.size() - 1;

            if (removeDenseIndex != lastDenseIndex) {
                TKey movedKey = m_denseKeys[lastDenseIndex];

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

        const std::vector<TKey>& Keys() const noexcept {
            return m_denseKeys;
        }

        std::vector<TValue>& Values() noexcept {
            return m_denseValues;
        }

        const std::vector<TValue>& Values() const noexcept {
            return m_denseValues;
        }
    };
}
