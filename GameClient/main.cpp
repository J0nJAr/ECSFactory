#include <memory>
#include <string>

#include "Base/IGameApp.hpp"
#include "Base/Logger.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "Network/INetworkConnection.hpp"
#include "Network/MemoryConnection.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/UDPConnection.hpp"

using namespace EF;

namespace {
    void RegisterNetworkSnapshotComponents(World& world) {
        world.RegisterSnapshotComponent<Transform>();
        world.RegisterSnapshotComponent<Velocity>();
    }
}

class GameClientApplication : public GameAppBase {
public:
    GameClientApplication()
        : GameAppBase(GameEngineConfig{ EngineRole::Client, 20000 }) {
    }

    void StartSinglePlayer() {
        auto [clientPipe, serverPipe] = Network::MemoryConnection::CreatePair();

        CreateLocalServerWorld(serverPipe);
        CreateClientWorld();
        AttachClientNetwork(clientPipe);

        LOG_INFO("Singleplayer started. Client and local server connected via memory transport.");
    }

    void StartHost() {
        auto [clientPipe, serverPipe] = Network::MemoryConnection::CreatePair();

        CreateLocalServerWorld(serverPipe);
        CreateClientWorld();
        AttachClientNetwork(clientPipe);

        LOG_INFO("Host mode started through local server world.");
    }

    void JoinServer(const std::string& address) {
        StopLocalServer();
        CreateClientWorld();

        auto udpConnection = std::make_shared<Network::UDPConnection>();
        udpConnection->Connect(address, 6780);
        AttachClientNetwork(udpConnection);

        LOG_INFO("Joining remote server at {}.", address);
    }

protected:
    void OnConfigure(GameEngine& engine) override {
        LOG_INFO("Configuring client runtime.");
        engine.SetFixedTimeStep(20000);
    }

    void OnInitialize(GameEngine& engine) override {
        static_cast<void>(engine);
        LOG_INFO("Client initialized in menu state.");
    }

    int OnRun(GameEngine& engine) override {
        static_cast<void>(engine);
        LOG_INFO("Client menu is ready. Select singleplayer, host, or join from the UI layer.");
        return 0;
    }

    void OnShutdown(GameEngine& engine) override {
        static_cast<void>(engine);
        StopLocalServer();
        m_clientWorld = nullptr;
        LOG_INFO("Client shutting down.");
    }

private:
    void CreateLocalServerWorld(std::shared_ptr<Network::INetworkConnection> serverConnection) {
        if (m_localServerWorld) {
            return;
        }

        m_localServerWorld = GetEngine().CreateWorld(WorldRole::ServerSimulation, true);
        RegisterNetworkSnapshotComponents(*m_localServerWorld);
        m_localServerWorld->AddResource<NetworkManager>(std::move(serverConnection));
        m_localServerSync = &m_localServerWorld->RegisterSystem<NetworkSyncSystem>(NetworkSyncMode::Server);
    }

    void StopLocalServer() {
        if (!m_localServerWorld) {
            return;
        }

        GetEngine().DestroyWorld(m_localServerWorld);
        m_localServerWorld = nullptr;
        m_localServerSync = nullptr;
    }

    void CreateClientWorld() {
        if (m_clientWorld) {
            return;
        }

        m_clientWorld = GetEngine().CreateWorld(WorldRole::ClientPresentation, true);
        RegisterNetworkSnapshotComponents(*m_clientWorld);
    }

    void AttachClientNetwork(std::shared_ptr<Network::INetworkConnection> connection) {
        if (!m_clientWorld) {
            return;
        }

        m_clientWorld->AddResource<NetworkManager>(std::move(connection));
        if (!m_clientSync) {
            m_clientSync = &m_clientWorld->RegisterSystem<NetworkSyncSystem>(NetworkSyncMode::Client);
        }
    }

private:
    World* m_clientWorld = nullptr;
    World* m_localServerWorld = nullptr;
    NetworkSyncSystem* m_clientSync = nullptr;
    NetworkSyncSystem* m_localServerSync = nullptr;
};

int main() {
    Logger::Init("game_client.log");

    GameClientApplication app;
    return app.Run();
}
