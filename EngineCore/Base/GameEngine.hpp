#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "ECS/World.hpp"
#include "Math/Fixed64.hpp"

namespace EF {

    enum class EngineRole {
        Client,
        Server
    };

    enum class WorldRole {
        ServerSimulation,
        ClientPresentation
    };

    struct GameEngineConfig {
        EngineRole role = EngineRole::Client;
        uint64_t fixedTimeStepUs = 20000; // 20 ms = 50 Hz
    };

    enum class FrameStepMode {
        SimulateRealTime,
        FrameOnly
    };

    class GameEngine {
    public:
        explicit GameEngine(GameEngineConfig config = {});
        ~GameEngine();

        void Initialize();
        void Run();
        void RequestStop();
        void Shutdown();

        World* CreateWorld(WorldRole role, bool isUpdating);
        bool DestroyWorld(World* world);
        bool SetWorldUpdating(World* world, bool isUpdating);

        [[nodiscard]] EngineRole GetRole() const noexcept {
            return m_config.role;
        }

        [[nodiscard]] WorldRole GetWorldRole(const World* world) const noexcept;

        [[nodiscard]] const std::vector<World*>& GetUpdatingWorlds() const noexcept {
            return m_updatingWorlds;
        }

        [[nodiscard]] uint64_t GetCurrentTick() const noexcept {
            return m_currentTick;
        }

        void SetFixedTimeStep(uint64_t microseconds);
        void ResetTimeAccumulator() noexcept;

        void ManualStepFrame(uint64_t deltaTimeUs, FrameStepMode mode = FrameStepMode::SimulateRealTime);
        void ManualStepFrame(float deltaTimeSeconds, FrameStepMode mode = FrameStepMode::SimulateRealTime);
        void ManualStepFixed(Fixed64 deltaTime);

    private:
        [[nodiscard]] uint64_t GetCurrentTimeUs() const noexcept {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
        }

        void CallFixedUpdates();
        void FrameUpdateUpdatingWorlds(float deltaTimeSeconds);
        void DestroyAllWorlds() noexcept;

    private:
        GameEngineConfig m_config;
        std::atomic<bool> m_isRunning{ false };
        uint64_t m_currentTick = 0;
        uint64_t m_accumulatedTimeUs = 0;
        uint64_t m_lastFrameTimeUs = 0;
        uint64_t m_fixedTimeStepUs = 20000; // 20 ms = 50 Hz

        std::vector<std::unique_ptr<World>> m_ownedWorlds;
        std::vector<World*> m_worlds;
        std::vector<World*> m_updatingWorlds;
        std::vector<WorldRole> m_worldRoles;
    };

}
