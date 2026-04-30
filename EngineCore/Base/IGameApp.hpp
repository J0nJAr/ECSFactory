#pragma once

namespace EF {
    class GameEngine;


    /**
     * 모든 게임 애플리케이션은 IGameApp 인터페이스를 구현해야 합니다. 
     * 게임 엔진은 스스로 IGameApp의 라이프사이클 메서드(OnEngineConfigure, OnEngineInitialize, OnEngineShutdown)를 호출하여 애플리케이션이 엔진과 상호작용할 수 있도록 합니다.
     * 
     * OnEngineConfigure: 게임 엔진이 초기화되기 전에 호출됩니다. 이 메서드에서 애플리케이션은 게임 엔진의 설정을 구성할 수 있습니다. 예를 들어, 고정 시간 단계의 길이를 설정하거나, 초기 월드를 생성하고 업데이트 대상으로 설정할 수 있습니다.
     * OnEngineInitialize: 게임 엔진이 초기화된 후에 호출됩니다. 이 메서드에서 애플리케이션은 게임 로직을 초기화할 수 있습니다. 예를 들어, 시스템을 등록하거나, 초기 엔티티와 컴포넌트를 생성할 수 있습니다.
     * OnEngineShutdown: 게임 엔진이 종료되기 전에 호출됩니다. 이 메서드에서 애플리케이션은 게임 로직을 정리할 수 있습니다. 
     * 
     * 일반적으로 서버에서는 OnEngineConfigure에서 고정 시간 단계의 길이를 설정하고, OnEngineInitialize에서 게임 월드를 생성하고 시스템을 등록하는 방식으로 구현됩니다.
     * 클라이언트에서는 우선 서버에 연결한 후, 서버에서 초기 게임 상태를 받아와서 OnEngineInitialize에서 게임 월드를 생성하고 시스템을 등록하는 방식으로 구현됩니다.
     */

    class IGameApp {
    public:
        virtual ~IGameApp() = default;

        virtual void OnEngineConfigure(GameEngine& engine) { static_cast<void>(engine); }
        virtual void OnEngineInitialize(GameEngine& engine) { static_cast<void>(engine); }
        virtual void OnEngineShutdown(GameEngine& engine) { static_cast<void>(engine); }
    };
}
