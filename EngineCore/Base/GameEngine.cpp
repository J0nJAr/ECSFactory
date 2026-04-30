#include "GameEngine.hpp"

#include <algorithm>

namespace EF {

    namespace {
        constexpr double kMicrosecondsToSeconds = 1.0 / 1000000.0;
    }

    GameEngine::GameEngine() = default;

    GameEngine::~GameEngine() {
        DestroyAllWorlds();
    }

    void GameEngine::Initialize() {
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

    World* GameEngine::CreateWorld(bool isUpdating) {
        World* newWorld = new World();
        m_worlds.push_back(newWorld);
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

        delete *worldIt;
        m_worlds.erase(worldIt);
        return true;
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
        for (World* world : m_worlds) {
            delete world;
        }

        m_worlds.clear();
        m_updatingWorlds.clear();
    }

}
