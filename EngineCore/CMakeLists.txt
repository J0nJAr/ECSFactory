add_library(EngineCore STATIC)

target_sources(EngineCore
    PRIVATE
        Base/GameEngine.cpp
        ECS/EntityCommandBuffer.cpp
        Gameplay/PlayerMovement.cpp
        Math/Math.cpp
        Network/MemoryConnection.cpp
        Network/NetworkManager.cpp
        Network/NetworkSyncSystem.cpp
        Network/ReliabilityChannel.cpp
        Network/SimulatedNetwork.cpp
        Network/UDPConnection.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Base/Assert.hpp
            Base/Defines.hpp
            Base/GameEngine.hpp
            Base/IGameApp.hpp
            Base/InputManager.hpp
            Base/Logger.hpp
            Base/RingBuffer.hpp
            Base/Types.hpp
            Components/PlayerControl.hpp
            Components/Transform.hpp
            Components/Velocity.hpp
            Gameplay/PlayerMovement.hpp
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
            ECS/World.hpp
            Math/Fixed64.hpp
            Math/Internal.hpp
            Math/Math.hpp
            Math/Quaternion.hpp
            Math/Vector.hpp
            Network/Endpoint.hpp
            Network/INetworkConnection.hpp
            Network/InputCommand.hpp
            Network/MemoryConnection.hpp
            Network/NetUtils.hpp
            Network/NetworkManager.hpp
            Network/NetworkSyncSystem.hpp
            Network/Packet.hpp
            Network/ReliabilityChannel.hpp
            Network/SimulatedNetwork.hpp
            Network/UDPConnection.hpp
            ECS/ISystem.hpp
)

target_include_directories(EngineCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(EngineCore PUBLIC cxx_std_20)

# 엔진 버전을 코드에서 볼 수 있게 한다. 디버그 오버레이(U5)가 화면에 찍을 대상이고,
# 나중에 클라/서버 버전 불일치를 handshake에서 거를 때도 쓴다.
target_compile_definitions(EngineCore PUBLIC
    EF_VERSION_STRING="${PROJECT_VERSION}"
    EF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    EF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    EF_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

if(WIN32)
    target_link_libraries(EngineCore PUBLIC ws2_32)
endif()

# ---------------------------------------------------------------------------
# EngineRender — 창과 렌더 백엔드, 그리고 UI.
#
# UI가 여기 있는 이유: UI는 ClientPresentation 전용이고 서버 빌드는 링크조차 하면 안 된다
# (docs/design/ui-layer.md §0-3). 앵커 레이아웃은 순수 함수라 GPU가 없어도 돌지만,
# 타깃을 EngineCore로 내리면 UI의 나머지(드로우 목록·폰트·히트테스트)가 따라 내려올 구실이
# 생긴다. 처음부터 렌더 쪽에 둔다.
#
# EngineCore와 별도 타깃으로 두는 이유: GameServer가 DX11을 링크조차 하지 않게 하기 위함.
# dedicated server는 창도 GPU도 없다. 나중에 분리하려면 의존이 얽힌 뒤라 비싸지므로
# 처음부터 나눠 둔다. (docs/design/rendering-dx11.md §9, docs/design/roadmap.md E2)
#
# 방향은 한쪽뿐이다: EngineRender → EngineCore. 코어는 렌더를 모른다.
# ---------------------------------------------------------------------------
add_library(EngineRender STATIC)

target_sources(EngineRender
    PRIVATE
        Platform/Win32Window.cpp
        Render/Dx11/Dx11Renderer.cpp
        UI/AnchorLayout.cpp
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
        FILES
            Platform/IWindow.hpp
            Platform/Win32Window.hpp
            Render/Dx11/ComPtr.hpp
            Render/Dx11/Dx11Renderer.hpp
            Render/IRenderer.hpp
            Render/RenderContext.hpp
            Render/RenderTypes.hpp
            UI/AnchorLayout.hpp
)

target_link_libraries(EngineRender PUBLIC EngineCore)
target_compile_features(EngineRender PUBLIC cxx_std_20)

if(WIN32)
    # d3dcompiler는 R3(런타임 셰이더 컴파일)부터 실제로 쓰인다.
    target_link_libraries(EngineRender PRIVATE d3d11 dxgi d3dcompiler)
endif()
