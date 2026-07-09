#include "Base/IGameApp.hpp"
#include "Base/Logger.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/UDPConnection.hpp"

#include <memory>

using namespace EF;

namespace {
    void RegisterNetworkSnapshotComponents(World& world) {
        world.RegisterSnapshotComponent<Transform>();
        world.RegisterSnapshotComponent<Velocity>();
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
