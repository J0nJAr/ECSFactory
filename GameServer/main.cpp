#include <iostream>
#include "Base/Logger.hpp"
#include "Base/Assert.hpp"
#include "Math/Fixed64.hpp"
#include "ECS/World.hpp"
#include "Base/GameEngine.hpp"
#include "Base/IGameApp.hpp"

using namespace EF;

class GameServerApplication : public IGameApp{
public:
    void OnEngineConfigure(GameEngine& engine) override {
        Logger::Log("Configuring Game Server Application...");
        // Configure the game engine if needed
    }

    void OnEngineInitialize(GameEngine& engine) override {
        Logger::Log("Initializing Game Server Application...");
        // Initialize game server resources, worlds, etc.
    }

    void OnEngineShutdown(GameEngine& engine) override {
        Logger::Log("Shutting down Game Server Application...");
        // Clean up resources, save state, etc.
    }
private:
    
}


int main() {
    GameServerApplication app;
    
    Logger::Init("game_server.log");
    Logger::Log("Starting Game Server..."); 




    return 0;
}