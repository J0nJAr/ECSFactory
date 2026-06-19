#pragma once

#include "Base/GameEngine.hpp"

namespace EF {

    // App-level owner for a GameEngine instance.
    // Derived apps configure worlds and systems before the engine initializes them.
    class IGameApp {
    public:
        explicit IGameApp(GameEngineConfig config = {})
            : m_engine(config) {
        }

        virtual ~IGameApp() = default;

        /**
         * Runs the game application. This will call the virtual methods in the following order:
         * 1. OnConfigure - for configuring the engine before initialization.
         * 2. OnInitialize - for creating worlds and systems before the main loop starts.
         * 3. call GameEngine::Initialize.
         * 4. OnRun - the main entry point for running the app. By default
         *    calls GameEngine::Run, but can be overridden for custom main loops or additional logic.
         * 5. OnShutdown - for cleanup after the main loop exits, before engine shutdown.
         * 6. call GameEngine::Shutdown.
         */
        int Run() {
            OnConfigure(m_engine);
            OnInitialize(m_engine);
            m_engine.Initialize();
            const int exitCode = OnRun(m_engine);
            OnShutdown(m_engine);
            m_engine.Shutdown();
            return exitCode;
        }

    protected:
        [[nodiscard]] GameEngine& GetEngine() noexcept {
            return m_engine;
        }

        [[nodiscard]] const GameEngine& GetEngine() const noexcept {
            return m_engine;
        }

        virtual void OnConfigure(GameEngine& engine) { static_cast<void>(engine); }
        virtual void OnInitialize(GameEngine& engine) { static_cast<void>(engine); }
        virtual int OnRun(GameEngine& engine) {
            engine.Run();
            return 0;
        }
        virtual void OnShutdown(GameEngine& engine) { static_cast<void>(engine); }

    private:
        GameEngine m_engine;
    };

    using GameAppBase = IGameApp;
}
