#include "Base/IGameApp.hpp"
#include "Base/Logger.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/UDPConnection.hpp"

#include <memory>

using namespace EF;

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

        auto udpConnection = std::make_shared<Network::UDPConnection>();
        if (udpConnection->Bind(6780)) {
            m_serverWorld->AddResource<NetworkManager>(udpConnection);
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
    }

private:
    World* m_serverWorld = nullptr;
};

int main() {
    Logger::Init("game_server.log");

    GameServerApplication app;
    return app.Run();
}
