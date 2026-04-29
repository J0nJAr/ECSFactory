#pragma once
#include <cstdint>
#include <limits>
#include <algorithm>

// 64비트 엔티티 체계(32:32)를 사용하기
#define EF_ENTITY_ID_64 
// 32비트 엔티티 체계(20:12)를 사용하기
//#define EF_ENTITY_ID_32

namespace EF {

#ifdef EF_ENTITY_ID_64
    // 64비트 모드 (Index 32bit, Version 32bit)
    using EntityId      = uint64_t;
    using EntityIndex   = uint32_t;
    using EntityVersion = uint32_t;

    inline constexpr uint32_t kEntityIndexBits   = 32;
    inline constexpr uint32_t kEntityVersionBits = 32;
#elif defined(EF_ENTITY_ID_32)
    // 32비트 모드(Index 20bit, Version 12bit)
    using EntityId      = uint32_t;
    using EntityIndex   = uint32_t;
    using EntityVersion = uint32_t; 

    inline constexpr uint32_t kEntityIndexBits   = 20;
    inline constexpr uint32_t kEntityVersionBits = 12;
#else
    using EntityId      = uint32_t;
    using EntityIndex   = uint32_t;
    using EntityVersion = uint32_t;

    inline constexpr uint32_t kEntityIndexBits   = 20;
    inline constexpr uint32_t kEntityVersionBits = 12;
#endif

    using TypeHash = uint64_t;
    using TypeIndex = uint32_t;


    template<typename... Types>
    struct With {};

    template<typename... Types>
    struct Without {};

    inline constexpr uint32_t kEntityVersionShift = kEntityIndexBits;

    inline constexpr EntityId kEntityIndexMask = (EntityId(1) << kEntityIndexBits) - 1;
    inline constexpr EntityId kEntityVersionMask = (EntityId(1) << kEntityVersionBits) - 1;

    inline constexpr EntityId kNullEntity  = std::numeric_limits<EntityId>::max();

    [[nodiscard]] constexpr EntityIndex GetEntityIndex(EntityId entityId) noexcept {
        return static_cast<EntityIndex>(entityId & kEntityIndexMask);
    }

    [[nodiscard]] constexpr EntityVersion GetEntityVersion(EntityId entityId) noexcept {
        return static_cast<EntityVersion>((entityId >> kEntityVersionShift) & kEntityVersionMask);
    }

    [[nodiscard]] constexpr EntityId CreateEntityId(EntityIndex index, EntityVersion version) noexcept {
        return (static_cast<EntityId>(version & kEntityVersionMask) << kEntityVersionShift) |
               (static_cast<EntityId>(index & kEntityIndexMask));
    }

} // namespace EF