#include <chrono>
#include <cstdlib>
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
#include "Platform/Win32Window.hpp"
#include "Render/Dx11/Dx11Renderer.hpp"
#include "Render/RenderContext.hpp"
#include "Render/RenderTypes.hpp"

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
        LOG_INFO("Client initialized in menu state.");
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

        RenderContext frameContext;
        uint64_t frames = 0;
        auto previous = std::chrono::steady_clock::now();

        while (window.PumpMessages()) {
            if (window.ConsumeResized()) {
                renderer.Resize(window.Width(), window.Height());
            }

            const auto now = std::chrono::steady_clock::now();
            const auto deltaUs = std::chrono::duration_cast<std::chrono::microseconds>(now - previous).count();
            previous = now;
            engine.ManualStepFrame(static_cast<uint64_t>(deltaUs));

            BuildFrame(frameContext, window);

            renderer.BeginFrame(Color{ 0.09f, 0.10f, 0.13f, 1.0f });
            renderer.SetCamera(Mat4::Identity());
            renderer.Submit(frameContext);

            // Present 전에 찍어야 한다. SWAP_EFFECT_DISCARD라 Present 후 내용은 보장되지 않는다.
            const bool lastFrame = (m_frameLimit != 0 && frames + 1 >= m_frameLimit);
            if (lastFrame && !m_capturePath.empty()) {
                renderer.CaptureBackBuffer(m_capturePath.c_str());
            }

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

private:
    // R3 확인용 임시 장면. R5에서 RenderSystem이 ECS를 순회해 채우게 되면 사라진다.
    //
    // 좌표는 아직 NDC다(카메라가 항등이라 -1..1이 화면 전체). 월드 좌표를 쓰려면
    // R4에서 Camera가 ortho 행렬을 만들어 SetCamera로 넘겨야 한다.
    void BuildFrame(RenderContext& context, const IWindow& window) {
        static_cast<void>(window);
        context.Clear();

        context.Add(DrawItem{
            .position = Float3{ 0.0f, 0.0f, 0.0f },
            .size = Float2{ 0.5f, 0.5f },
            .rotation = 0.0f,
            .color = Color{ 0.35f, 0.75f, 1.0f, 1.0f }
        });
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
    uint64_t m_frameLimit = 0;
    std::string m_capturePath;
};

int main(int argc, char** argv) {
    Logger::Init("game_client.log");

    GameClientApplication app;

    // --frames N : N프레임만 돌고 끝낸다. 사람이 창을 닫아줄 수 없는 환경
    // (CI, 자동 확인)에서 렌더 경로가 실제로 도는지 보기 위한 것이다.
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--frames") {
            app.SetFrameLimit(std::strtoull(argv[i + 1], nullptr, 10));
        } else if (std::string(argv[i]) == "--capture") {
            app.SetCapturePath(argv[i + 1]);
        }
    }

    return app.Run();
}
