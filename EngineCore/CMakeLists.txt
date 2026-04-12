add_library(EngineCore STATIC)

target_sources(EngineCore PRIVATE
    Math/Fixed64.cpp
    Math/Fixed64.hpp
    Math/Internal.hpp
    Math/Math.hpp
    Base/Assert.hpp
    Base/Logger.hpp
    Base/Types.hpp
    ECS/ComponentPool.hpp
    ECS/Core.hpp
    ECS/EntitiyManager.hpp
    ECS/Registry.hpp
    ECS/SparseSet.hpp
    ECS/TypeInfo.hpp
)

target_include_directories(EngineCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(EngineCore PUBLIC cxx_std_20)
