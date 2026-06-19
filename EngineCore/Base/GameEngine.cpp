#include "GameEngine.hpp"
#include "IGameApp.hpp"
#include "InputManager.hpp"

#include <algorithm>

namespace EF {

    namespace {
        constexpr double kMicrosecondsToSeconds = 1.0 / 1000000.0;
    }

    GameEngine::GameEngine(GameEngineConfig config)
        : m_config(config),
          m_fixedTimeStepUs(config.fixedTimeStepUs) {
    }

    GameEngine::~GameEngine() {
        DestroyAllWorlds();
    }

    void GameEngine::Initialize() {
        Input::Initialize();
        for (World* world : m_worlds) {
            if (world) {
                world->Initialize();
            }
        }
    }

    void GameEngine::Run() {
        m_isRunning.store(true);
        m_lastFrameTimeUs = GetCurrentTimeUs();

        while (m_isRunning.load()) {
            const uint64_t currentTimeUs = GetCurrentTimeUs();
            const uint64_t deltaTimeUs = currentTimeUs - m_lastFrameTimeUs;
            m_lastFrameTimeUs = currentTimeUs;
            
            Input::Update();
            ManualStepFrame(deltaTimeUs, FrameStepMode::SimulateRealTime);
        }
    }

    void GameEngine::RequestStop() {
        m_isRunning.store(false);
    }

    void GameEngine::Shutdown() {
        RequestStop();

        for (World* world : m_worlds) {
            if (world) {
                world->Shutdown();
            }
        }

        DestroyAllWorlds();
        m_currentTick = 0;
        ResetTimeAccumulator();
        m_lastFrameTimeUs = 0;
    }

    World* GameEngine::CreateWorld(WorldRole role, bool isUpdating) {
        std::unique_ptr<World> ownedWorld = std::make_unique<World>();
        World* newWorld = ownedWorld.get();
        m_ownedWorlds.push_back(std::move(ownedWorld));
        m_worlds.push_back(newWorld);
        m_worldRoles.push_back(role);
        if (isUpdating) {
            m_updatingWorlds.push_back(newWorld);
        }
        return newWorld;
    }

    bool GameEngine::DestroyWorld(World* world) {
        if (!world) {
            return false;
        }

        auto updatingIt = std::find(m_updatingWorlds.begin(), m_updatingWorlds.end(), world);
        if (updatingIt != m_updatingWorlds.end()) {
            m_updatingWorlds.erase(updatingIt);
        }

        auto worldIt = std::find(m_worlds.begin(), m_worlds.end(), world);
        if (worldIt == m_worlds.end()) {
            return false;
        }

        const auto worldIndex = static_cast<size_t>(std::distance(m_worlds.begin(), worldIt));
        m_worlds.erase(worldIt);
        if (worldIndex < m_worldRoles.size()) {
            m_worldRoles.erase(m_worldRoles.begin() + static_cast<std::ptrdiff_t>(worldIndex));
        }

        auto ownedIt = std::find_if(
            m_ownedWorlds.begin(),
            m_ownedWorlds.end(),
            [world](const std::unique_ptr<World>& ownedWorld) {
                return ownedWorld.get() == world;
            }
        );
        if (ownedIt != m_ownedWorlds.end()) {
            m_ownedWorlds.erase(ownedIt);
        }

        return true;
    }

    WorldRole GameEngine::GetWorldRole(const World* world) const noexcept {
        auto worldIt = std::find(m_worlds.begin(), m_worlds.end(), world);
        if (worldIt == m_worlds.end()) {
            return WorldRole::ClientPresentation;
        }

        const auto worldIndex = static_cast<size_t>(std::distance(m_worlds.begin(), worldIt));
        if (worldIndex >= m_worldRoles.size()) {
            return WorldRole::ClientPresentation;
        }

        return m_worldRoles[worldIndex];
    }

    bool GameEngine::SetWorldUpdating(World* world, bool isUpdating) {
        if (!world) {
            return false;
        }

        const bool isOwnedWorld = std::find(m_worlds.begin(), m_worlds.end(), world) != m_worlds.end();
        if (!isOwnedWorld) {
            return false;
        }

        auto updatingIt = std::find(m_updatingWorlds.begin(), m_updatingWorlds.end(), world);
        if (isUpdating) {
            if (updatingIt == m_updatingWorlds.end()) {
                m_updatingWorlds.push_back(world);
                return true;
            }
            return false;
        }

        if (updatingIt != m_updatingWorlds.end()) {
            m_updatingWorlds.erase(updatingIt);
            return true;
        }

        return false;
    }

    void GameEngine::SetFixedTimeStep(uint64_t microseconds) {
        if (microseconds == 0) {
            return;
        }

        m_fixedTimeStepUs = microseconds;
    }

    void GameEngine::ResetTimeAccumulator() noexcept {
        m_accumulatedTimeUs = 0;
    }

    void GameEngine::ManualStepFrame(uint64_t deltaTimeUs, FrameStepMode mode) {
        if (mode == FrameStepMode::SimulateRealTime) {
            m_accumulatedTimeUs += deltaTimeUs;
            CallFixedUpdates();
        }

        const float deltaTimeSeconds = static_cast<float>(static_cast<double>(deltaTimeUs) * kMicrosecondsToSeconds);
        FrameUpdateUpdatingWorlds(deltaTimeSeconds);
    }

    void GameEngine::ManualStepFrame(float deltaTimeSeconds, FrameStepMode mode) {
        if (deltaTimeSeconds <= 0.0f) {
            FrameUpdateUpdatingWorlds(deltaTimeSeconds);
            return;
        }

        const auto deltaTimeUs = static_cast<uint64_t>(static_cast<double>(deltaTimeSeconds) * 1000000.0);
        ManualStepFrame(deltaTimeUs, mode);
    }

    void GameEngine::ManualStepFixed(Fixed64 deltaTime) {
        for (World* world : m_updatingWorlds) {
            if (world) {
                world->FixedUpdate(deltaTime, m_currentTick);
            }
        }

        ++m_currentTick;
    }

    void GameEngine::CallFixedUpdates() {
        if (m_fixedTimeStepUs == 0) {
            return;
        }

        const Fixed64 fixedDeltaTime = Fixed64::FromDouble(
            static_cast<double>(m_fixedTimeStepUs) * kMicrosecondsToSeconds
        );

        while (m_accumulatedTimeUs >= m_fixedTimeStepUs) {
            ManualStepFixed(fixedDeltaTime);
            m_accumulatedTimeUs -= m_fixedTimeStepUs;
        }
    }

    void GameEngine::FrameUpdateUpdatingWorlds(float deltaTimeSeconds) {
        for (World* world : m_updatingWorlds) {
            if (world) {
                world->FrameUpdate(deltaTimeSeconds);
            }
        }
    }

    void GameEngine::DestroyAllWorlds() noexcept {
        m_ownedWorlds.clear();
        m_worlds.clear();
        m_updatingWorlds.clear();
        m_worldRoles.clear();
    }

}
