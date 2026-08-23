#include "Base/IGameApp.hpp"
#include "Base/Logger.hpp"
#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/UDPConnection.hpp"

#include <memory>

using namespace EF;

namespace {
    // 스냅샷으로 클라이언트에 실어 보낼 컴포넌트 목록.
    //
    // PlayerControl이 여기 있어야 하는 이유: 서버는 Hello를 받을 때 그 클라이언트의
    // 플레이어 엔티티에 PlayerControl{clientId}를 붙인다(NetworkSyncSystem). 이 컴포넌트가
    // 스냅샷을 타고 클라이언트까지 가야, 클라이언트의 ApplyConfirmedInputs도 "이 clientId의
    // 입력은 이 엔티티에"라는 같은 매칭을 할 수 있다. 빠지면 클라이언트 쪽
    // View<Transform, PlayerControl>이 비어서 확정 입력이 아무 데도 적용되지 않고,
    // 서버만 움직이는 상태 — 즉 즉각적인 디싱크가 된다.
    void RegisterNetworkSnapshotComponents(World& world) {
        world.RegisterSnapshotComponent<Transform>();
        world.RegisterSnapshotComponent<Velocity>();
        world.RegisterSnapshotComponent<PlayerControl>();
    }
}

class GameServerApplication : public GameAppBase {
public:
    GameServerApplication()
        : GameAppBase(GameEngineConfig{ EngineRole::Server, 20000 }) {
    }

protected:
    void OnConfigure(GameEngine& engine) override {
        LOG_INFO("Configuring dedicated server.");
        engine.SetFixedTimeStep(20000);
    }

    void OnInitialize(GameEngine& engine) override {
        LOG_INFO("Creating authoritative server simulation world.");
        m_serverWorld = engine.CreateWorld(WorldRole::ServerSimulation, true);
        RegisterNetworkSnapshotComponents(*m_serverWorld);

        auto udpConnection = std::make_shared<Network::UDPConnection>();
        if (udpConnection->Bind(6780)) {
            m_serverWorld->AddResource<NetworkManager>(udpConnection);
            m_syncSystem = &m_serverWorld->RegisterSystem<NetworkSyncSystem>(NetworkSyncMode::Server);
            LOG_INFO("Dedicated server listening on UDP port 6780.");
        } else {
            LOG_INFO("Dedicated server failed to bind UDP port 6780.");
        }
    }

    int OnRun(GameEngine& engine) override {
        LOG_INFO("Dedicated server running.");
        engine.Run();
        return 0;
    }

    void OnShutdown(GameEngine& engine) override {
        static_cast<void>(engine);
        LOG_INFO("Dedicated server shutting down.");
        m_serverWorld = nullptr;
        m_syncSystem = nullptr;
    }

private:
    World* m_serverWorld = nullptr;
    NetworkSyncSystem* m_syncSystem = nullptr;
};

int main() {
    Logger::Init("game_server.log");

    GameServerApplication app;
    return app.Run();
}
