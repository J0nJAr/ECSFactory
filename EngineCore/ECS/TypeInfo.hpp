#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "ECS/Core.hpp"

namespace EF {
    template<typename T>
    concept HasCustomTypeName = requires {
        { T::TypeName } -> std::convertible_to<std::string_view>;
    };

    struct TypeInfo final {
        TypeHash hash;
        TypeIndex index;
        std::string_view name;
        std::size_t size;
        std::size_t alignment;
        bool isTrivial;

        auto operator<=>(const TypeInfo&) const = default;
    };

    template<typename T>
    [[nodiscard]] const TypeInfo& GetTypeInfo() noexcept;

    namespace Internal {
        template<typename T>
        [[nodiscard]] constexpr std::string_view GetPureTypeName() noexcept {
#if defined(_MSC_VER)
            std::string_view sig = __FUNCSIG__;
            auto first = sig.find('<') + 1;
            auto last = sig.find_last_of('>');
            std::string_view name = sig.substr(first, last - first);
            if (name.starts_with("struct ")) {
                name.remove_prefix(7);
            }
            if (name.starts_with("class ")) {
                name.remove_prefix(6);
            }
            return name;
#elif defined(__clang__) || defined(__GNUC__)
            std::string_view sig = __PRETTY_FUNCTION__;
            auto first = sig.find("T = ");
            if (first == std::string_view::npos) {
                return sig;
            }
            first += 4;
            auto last = sig.find(';', first);
            if (last == std::string_view::npos) {
                last = sig.find(']', first);
            }
            return sig.substr(first, last - first);
#else
#error "Unsupported compiler!"
#endif
        }

        [[nodiscard]] constexpr TypeHash CompileTimeHash(std::string_view str) noexcept {
            uint64_t hash = 14695981039346656037ull;
            for (char c : str) {
                hash ^= static_cast<uint64_t>(c);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template<typename T>
        [[nodiscard]] constexpr TypeHash GetTypeHash() noexcept {
            if constexpr (HasCustomTypeName<T>) {
                return CompileTimeHash(T::TypeName);
            } else {
                return CompileTimeHash(GetPureTypeName<T>());
            }
        }

        class TypeIndexGenerator {
            static inline TypeIndex s_counter = 0;

        public:
            template<typename T>
            [[nodiscard]] static TypeIndex Get() noexcept {
                static const TypeIndex id = s_counter++;
                return id;
            }
        };

        template<typename T>
        [[nodiscard]] TypeInfo CreateTypeInfoImpl() noexcept {
            return TypeInfo{
                .hash = GetTypeHash<T>(),
                .index = TypeIndexGenerator::Get<T>(),
                .name = GetPureTypeName<T>(),
                .size = sizeof(T),
                .alignment = alignof(T),
                .isTrivial = std::is_trivially_copyable_v<T>
            };
        }
    }

    template<typename T>
    [[nodiscard]] const TypeInfo& GetTypeInfo() noexcept {
        using PureType = std::remove_cvref_t<T>;
        static const TypeInfo instance = Internal::CreateTypeInfoImpl<PureType>();
        return instance;
    }
}
