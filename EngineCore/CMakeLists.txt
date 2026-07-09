add_library(EngineCore STATIC)

target_sources(EngineCore
    PRIVATE
        Base/GameEngine.cpp
        ECS/EntityCommandBuffer.cpp
        Math/Math.cpp
        Network/MemoryConnection.cpp
        Network/NetworkManager.cpp
        Network/NetworkSyncSystem.cpp
        Network/UDPConnection.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Base/Assert.hpp
            Base/Defines.hpp
            Base/GameEngine.hpp
            Base/IGameApp.hpp
            Base/Logger.hpp
            Base/Types.hpp
            Components/Transform.hpp
            Components/Velocity.hpp
            ECS/ComponentPool.hpp
            ECS/Core.hpp
            ECS/EntitiyManager.hpp
            ECS/EntityCommandBuffer.hpp
            ECS/FowardDeclare.hpp
            ECS/Registry.hpp
            ECS/SparseSet.hpp
            ECS/SystemRegistry.hpp
            ECS/TypeInfo.hpp
            ECS/View.hpp
            Math/Fixed64.hpp
            Math/Internal.hpp
            Math/Math.hpp
            Math/Quaternion.hpp
            Math/Vector.hpp
            Network/INetworkConnection.hpp
            Network/MemoryConnection.hpp
            Network/NetUtils.hpp
            Network/NetworkManager.hpp
            Network/NetworkSyncSystem.hpp
            Network/Packet.hpp
            Network/UDPConnection.hpp
            ECS/ISystem.hpp
)

target_include_directories(EngineCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(EngineCore PUBLIC cxx_std_20)

if(WIN32)
    target_link_libraries(EngineCore PUBLIC ws2_32)
endif()
