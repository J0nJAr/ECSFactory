#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>

#include "Base/IGameApp.hpp"
#include "Base/Logger.hpp"
#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "Network/INetworkConnection.hpp"
#include "Network/MemoryConnection.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/UDPConnection.hpp"
#include "Platform/Win32Window.hpp"
#include "Render/Camera.hpp"
#include "Render/RenderSystem.hpp"
#include "Render/Renderable.hpp"
#include "Render/Dx11/Dx11Renderer.hpp"
#include "Render/RenderContext.hpp"
#include "Render/RenderTypes.hpp"

using namespace EF;

namespace {
    // 스냅샷으로 서버 → 클라 전파되어야 하는 컴포넌트들.
    //
    // PlayerControl이 여기 있어야 하는 이유: 클라가 "이 엔티티는 누가 조종하는가"를 알아야
    // ApplyConfirmedInputs가 서버와 **같은 엔티티에 같은 입력**을 넣는다. 이게 빠지면
    // 클라 쪽에서는 아무도 움직이지 않고, 서버와 해시가 갈린다.
    //
    // ⚠️ Renderable은 여기 넣지 않는다. 색과 크기는 시뮬레이션 상태가 아니다.
    //    (그래서 스냅샷을 받으면 날아간다 — EnsurePlayerVisuals가 매 프레임 다시 붙인다.)
    void RegisterNetworkSnapshotComponents(World& world) {
        world.RegisterSnapshotComponent<Transform>();
        world.RegisterSnapshotComponent<Velocity>();
        world.RegisterSnapshotComponent<PlayerControl>();
    }

    // clientId에서 눈에 띄게 다른 색을 만든다.
    //
    // 창을 두 개 띄워 놓고 "누가 나인가"를 구별하려면 색이 달라야 한다. 해시를 쓰면
    // 인접한 id가 비슷한 색으로 나올 수 있어서, 색상환을 고정 간격으로 도는 표를 쓴다.
    // 결정론과 무관하다 — 표현 전용이라 클라마다 달라도 상관없다(다만 같은 편이 읽기 쉽다).
    Color PlayerColor(uint32_t clientId) {
        static constexpr Color kPalette[] = {
            { 0.35f, 0.75f, 1.00f, 1.0f },   // 하늘
            { 0.95f, 0.55f, 0.25f, 1.0f },   // 주황
            { 0.45f, 0.85f, 0.45f, 1.0f },   // 초록
            { 0.90f, 0.45f, 0.80f, 1.0f },   // 분홍
            { 0.95f, 0.85f, 0.35f, 1.0f },   // 노랑
            { 0.65f, 0.55f, 0.95f, 1.0f },   // 보라
        };
        constexpr uint32_t kCount = static_cast<uint32_t>(std::size(kPalette));
        return kPalette[clientId % kCount];
    }

    // 조종 가능한 엔티티에 표현 컴포넌트를 붙인다. 없을 때만.
    //
    // ⚠️ 매 프레임 불러야 한다. World::ApplySnapshot이 레지스트리를 통째로 교체하므로
    //    스냅샷을 받을 때마다(조인 직후, resync 직후) Renderable이 전부 사라진다.
    //    "한 번 붙이고 끝"으로 만들면 resync 후 화면이 빈 채로 남는데, 그때 원인을
    //    렌더러에서 찾게 되어 시간을 크게 버린다.
    //
    // 렌더 계층이 아니라 앱이 이걸 하는 이유: 무엇이 어떤 모습이어야 하는지는 게임의 판단이다.
    // RenderSystem은 View<Transform, Renderable>을 그릴 뿐 누구에게 무엇을 붙일지 모른다.
    void EnsurePlayerVisuals(World& world) {
        // 이동 스텝이 tick당 1유닛이라, 크기 3이면 한 tick 이동이 자기 몸의 1/3이다.
        // 너무 작으면 화면에서 점처럼 보이고 너무 크면 겹쳐서 누가 움직였는지 안 보인다.
        static constexpr float kPlayerSize = 3.0f;

        // Without<Renderable>로 "아직 안 붙은 것만" 뽑는다.
        // 이 ECS에는 HasComponent가 없다 — 조회는 전부 뷰를 통한다.
        std::vector<std::pair<EntityId, uint32_t>> pending;
        auto view = world.CreateView<Transform, PlayerControl>(Without<Renderable>{});
        view.ForEach([&](EntityId entity, Transform&, PlayerControl& control) {
            pending.emplace_back(entity, control.clientId);
        });

        // 순회 중에 컴포넌트를 추가하면 풀이 재할당되어 순회가 무효가 된다.
        // 그래서 모아 두고 밖에서 붙인다.
        for (const auto& [entity, clientId] : pending) {
            world.AddComponent<Renderable>(entity, Renderable{
                .color = PlayerColor(clientId),
                .size = Float2{ kPlayerSize, kPlayerSize }
            });
        }
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

    // 실행 모드. 시작 화면(U8)이 생기기 전까지는 명령줄이 이 역할을 한다.
    enum class Mode { SinglePlayer, Join };

    void SetMode(Mode mode, std::string address = {}) {
        m_mode = mode;
        m_serverAddress = std::move(address);
    }

    // 프레임을 몇 개만 돌고 끝낼지. 0이면 창을 닫을 때까지 계속 돈다.
    // 사람 없이 렌더 경로가 도는지 확인할 때 쓴다(`GameClient.exe --frames 60`).
    void SetFrameLimit(uint64_t frames) {
        m_frameLimit = frames;
    }

    // 마지막 프레임의 백버퍼를 이 경로에 덤프한다. 비어 있으면 안 한다.
    void SetCapturePath(std::string path) {
        m_capturePath = std::move(path);
    }

protected:
    void OnConfigure(GameEngine& engine) override {
        LOG_INFO("Configuring client runtime.");
        engine.SetFixedTimeStep(20000);
    }

    void OnInitialize(GameEngine& engine) override {
        static_cast<void>(engine);

        // 월드와 네트워크는 여기서 만든다. OnRun보다 먼저 불리고, engine.Initialize()가
        // 뒤이어 각 시스템의 Initialize를 부르기 때문이다.
        switch (m_mode) {
        case Mode::Join:
            JoinServer(m_serverAddress);
            break;
        case Mode::SinglePlayer:
        default:
            StartSinglePlayer();
            break;
        }
    }

    // 플랫폼 루프는 엔진이 아니라 앱이 소유한다. 서버는 창이 없기 때문이다.
    // (docs/design/rendering-dx11.md §3)
    //
    // 한 프레임의 순서:
    //   창 메시지 펌프 → (리사이즈 반영) → 엔진 스텝 → 렌더 제출 → Present
    // 엔진 스텝이 먼저인 이유: 이번 프레임에 그릴 상태를 먼저 확정해야 한다.
    int OnRun(GameEngine& engine) override {
        Win32Window window;
        if (!window.Create("ECSFactory", 1280, 720)) {
            LOG_ERROR("창을 만들지 못했다.");
            return 1;
        }

        Dx11Renderer renderer;
        if (!renderer.Initialize(window)) {
            LOG_ERROR("렌더러 초기화 실패.");
            return 1;
        }

        // RenderSystem은 여기서 등록한다. 렌더러가 이 스택 프레임에 살기 때문이다.
        //
        // ⚠️ engine.Initialize()는 이미 지나갔으므로 이 시스템의 Initialize()는 불리지 않는다.
        //    RenderSystem::Initialize가 아무것도 하지 않도록 만들어 둔 이유가 이것이다.
        //    나중에 여기서 GPU 리소스를 잡아야 해지면 등록 시점을 다시 봐야 한다.
        if (m_clientWorld != nullptr) {
            m_clientWorld->RegisterSystem<RenderSystem>(renderer, m_camera, window);
        } else {
            LOG_ERROR("클라이언트 월드가 없다. 렌더 시스템을 붙일 곳이 없다.");
            return 1;
        }

        uint64_t frames = 0;
        auto previous = std::chrono::steady_clock::now();

        while (window.PumpMessages()) {
            if (window.ConsumeResized()) {
                renderer.Resize(window.Width(), window.Height());
            }

            // 스냅샷이 레지스트리를 갈아치우면 표현 컴포넌트가 날아간다. 매 프레임 다시 붙인다.
            EnsurePlayerVisuals(*m_clientWorld);

            const auto now = std::chrono::steady_clock::now();
            const auto deltaUs = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
            previous = now;

            // 시뮬레이션(0~n tick)과 렌더(1회)를 한 번에 돌린다.
            // RenderSystem::FrameUpdate가 이 안에서 BeginFrame → SetCamera → Submit 까지 한다.
            engine.ManualStepFrame(static_cast<uint64_t>(deltaUs));

            // Present 전에 찍어야 한다. SWAP_EFFECT_DISCARD라 Present 후 내용은 보장되지 않는다.
            const bool lastFrame = (m_frameLimit != 0 && frames + 1 >= m_frameLimit);
            if (lastFrame && !m_capturePath.empty()) {
                renderer.CaptureBackBuffer(m_capturePath.c_str());
            }

            // 프레임을 닫는 것은 앱의 몫이다. RenderSystem이 Present까지 하면
            // 나중에 UI 패스가 월드 위에 낄 자리가 없어진다.
            renderer.EndFrame();

            ++frames;
            if (m_frameLimit != 0 && frames >= m_frameLimit) {
                LOG_INFO("프레임 한도({}) 도달 — 종료한다.", m_frameLimit);
                break;
            }
        }

        LOG_INFO("루프 종료. 총 {}프레임.", frames);
        renderer.Shutdown();
        window.Destroy();
        return 0;
    }

protected:
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
    Camera m_camera;
    Mode m_mode = Mode::SinglePlayer;
    std::string m_serverAddress;
    uint64_t m_frameLimit = 0;
    std::string m_capturePath;
};

int main(int argc, char** argv) {
    Logger::Init("game_client.log");

    GameClientApplication app;

    // --frames N : N프레임만 돌고 끝낸다. 사람이 창을 닫아줄 수 없는 환경
    // (CI, 자동 확인)에서 렌더 경로가 실제로 도는지 보기 위한 것이다.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasNext = (i + 1 < argc);

        if (arg == "--frames" && hasNext) {
            app.SetFrameLimit(std::strtoull(argv[++i], nullptr, 10));
        } else if (arg == "--capture" && hasNext) {
            app.SetCapturePath(argv[++i]);
        } else if (arg == "--join" && hasNext) {
            // 원격 dedicated server에 붙는다. 결정론 확인은 이 모드로 창을 둘 띄워서 한다.
            app.SetMode(GameClientApplication::Mode::Join, argv[++i]);
        } else if (arg == "--single") {
            // 같은 프로세스 안에 서버 월드를 두고 인메모리로 연결한다.
            app.SetMode(GameClientApplication::Mode::SinglePlayer);
        }
    }

    return app.Run();
}
