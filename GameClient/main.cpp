#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>

#include "Base/IGameApp.hpp"
#include "Base/InputManager.hpp"
#include "Base/Logger.hpp"
#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "Gameplay/InputMapping.hpp"
#include "Network/INetworkConnection.hpp"
#include "Network/LossyConnection.hpp"
#include "Network/MemoryConnection.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/UDPConnection.hpp"
#include "Platform/Win32Window.hpp"
#include "Render/Camera.hpp"
#include "Render/Dx11/Dx11Renderer.hpp"
#include "Render/RenderContext.hpp"
#include "Render/RenderSystem.hpp"
#include "Render/Renderable.hpp"
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

        // ⚠️ Renderable은 스냅샷 대상이 아니면서 **해시에서도 빠져야 한다.**
        //    서버에는 이 풀이 아예 없으므로, 클라 해시에 섞이면 매 tick 불일치가 나고
        //    TickHash 비교가 그걸 디싱크로 읽어 무한 resync에 빠진다.
        //    (World::RegisterPresentationComponent 주석에 전체 시나리오가 있다.)
        world.RegisterPresentationComponent<Renderable>();
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
        //
        // ⚠️ ForEach가 아니라 range-for를 쓴다. **둘이 넘겨주는 엔티티 값의 타입이 다르다.**
        //    ForEach는 version이 없는 EntityIndex를 주고, 그걸 EntityId로 받아도
        //    암묵 변환 때문에 컴파일이 통과한다. 그러면 version 0인 가짜 id가 만들어져
        //    재사용된 index에서 AddComponent의 IsAlive 단언이 터진다.
        //    반복자 경로는 ResolveEntityId를 거쳐 제대로 된 EntityId를 준다.
        //    (ECS/View.hpp 클래스 주석 참조)
        std::vector<std::pair<EntityId, uint32_t>> pending;
        for (auto [entity, transform, control] :
             world.CreateView<Transform, PlayerControl>(Without<Renderable>{})) {
            static_cast<void>(transform);
            pending.emplace_back(entity, control.clientId);
        }

        // 순회 중에 컴포넌트를 추가하면 풀이 재할당되어 순회가 무효가 된다.
        // 그래서 모아 두고 밖에서 붙인다.
        for (const auto& [entity, clientId] : pending) {
            world.AddComponent<Renderable>(entity, Renderable{
                .color = PlayerColor(clientId),
                .size = Float2{ kPlayerSize, kPlayerSize }
            });
        }
    }

    // EnsurePlayerVisuals를 **시스템으로** 돌린다.
    //
    // ⚠️ 왜 앱 루프에서 부르면 안 되는가 (한 프레임 깜빡임의 원인이었다):
    //    한 번의 ManualStepFrame 안에서 순서가 이렇다.
    //        FixedUpdate들 → (여기서 ApplySnapshot이 레지스트리를 교체 = Renderable 전멸)
    //        FrameUpdate들 → (여기서 RenderSystem이 그린다)
    //    앱 루프에서 ManualStepFrame **전에** 붙이면, 스냅샷이 온 프레임에는
    //    "붙임 → 전멸 → 그리기" 순서가 되어 그 프레임이 통째로 빈 화면이 된다.
    //    실제로 캡처가 그 프레임에 걸려서 플레이어가 사라진 적이 있다.
    //
    //    시스템으로 두고 RenderSystem보다 **먼저 등록**하면 같은 FrameUpdate 안에서
    //    "전멸 → 붙임 → 그리기"가 되어 깜빡임이 없어진다.
    //    (SystemRegistry::SortSystems가 아직 TODO라 등록 순서가 곧 실행 순서다.)
    // LossyConnection의 지연 큐를 시뮬레이션 tick에 맞춰 돌린다.
    //
    // ⚠️ 앱 루프(프레임)가 아니라 **FixedUpdate**에서 돌려야 한다. "3틱 지연"은 고정
    //    timestep 기준의 약속이라, 프레임률이 다른 기계에서 같은 지연을 재현하려면
    //    시뮬레이션 박자를 따라야 한다. 프레임에서 돌리면 144Hz 기계에서는 지연이
    //    2.5배 짧아진다 — 재현성을 위해 만든 물건이 재현성을 깨게 된다.
    class LossyLinkPumpSystem final : public ISystem {
    public:
        explicit LossyLinkPumpSystem(std::shared_ptr<Network::LossyConnection> link)
            : m_link(std::move(link)) {}

        void Initialize(World&) override {}
        void FixedUpdate(World&, Fixed64, uint64_t tick) override {
            if (m_link) {
                m_link->Advance(tick);
            }
        }
        void FrameUpdate(World&, float) override {}
        void Shutdown(World&) override {}

    private:
        std::shared_ptr<Network::LossyConnection> m_link;
    };

    class PlayerVisualSystem final : public ISystem {
    public:
        void Initialize(World&) override {}
        void FixedUpdate(World&, Fixed64, uint64_t) override {}
        void FrameUpdate(World& world, float) override { EnsurePlayerVisuals(world); }
        void Shutdown(World&) override {}
    };
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
        AttachClientNetwork(WrapWithLossyLink(udpConnection));

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

    // 이 클라이언트의 링크를 인위적으로 나쁘게 만든다. 재현 가능한 결정론 검증용이다.
    //
    // 왜 필요한가: 손실·지연 복구 경로(재전송, TickHash 불일치 → resync)는 **정상 세션에서
    // 절대 안 밟힌다.** 그래서 조용히 썩기 딱 좋다. 실제 네트워크가 나빠지기를 기다리는 대신
    // 시드를 고정해 같은 손실 패턴을 매번 재현한다.
    void SetLossyLink(const Network::LossyLinkSettings& settings) {
        m_linkSettings = settings;
        m_hasLossyLink = true;
    }

    // 스크립트 입력을 설정한다. 형식: "tick:키들,tick:키들,..." (예: "20:W,80:WD,140:")
    //
    // 왜 필요한가: 실제 키보드로 하는 검증은 **창 포커스에 묶여 있다.** 창이 둘이면
    // OS가 어느 쪽에 키를 줄지 사람이 정해야 하고, 자동화 스크립트에서
    // SetForegroundWindow 는 신뢰할 수 없다. 그래서 사람 없이 돌리는 검증이 불가능해진다.
    //
    // 이건 "가짜 키보드"다. 지정한 tick에 지정한 버튼 마스크를 그대로 큐에 넣는다.
    // tick 기준이라 프레임률과 무관하게 **매번 같은 결과**가 나온다 — 그게 이 기능의 요점이다.
    // 실제 키보드 경로(CollectLocalInput)와 같은 QueueLocalInput 을 쓰므로,
    // 검증하는 경로가 실제 경로와 다르지 않다.
    void SetInputScript(const std::string& script) {
        m_inputScript.clear();
        std::size_t pos = 0;
        while (pos < script.size()) {
            const std::size_t comma = script.find(',', pos);
            const std::string entry = script.substr(pos, comma - pos);
            pos = (comma == std::string::npos) ? script.size() : comma + 1;

            const std::size_t colon = entry.find(':');
            if (colon == std::string::npos) {
                continue;
            }

            const uint64_t tick = std::strtoull(entry.substr(0, colon).c_str(), nullptr, 10);
            uint64_t mask = 0;
            for (char key : entry.substr(colon + 1)) {
                switch (key) {
                case 'W': case 'w': mask |= kInputMoveForward; break;
                case 'S': case 's': mask |= kInputMoveBack;    break;
                case 'A': case 'a': mask |= kInputMoveLeft;    break;
                case 'D': case 'd': mask |= kInputMoveRight;   break;
                default: break;
                }
            }
            m_inputScript[tick] = mask;
        }
        m_hasInputScript = !m_inputScript.empty();
        LOG_INFO("입력 스크립트 {}개 항목을 읽었다.", m_inputScript.size());
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

        // 키 상태 배열을 0으로 깔아 둔다. 안 하면 첫 프레임의 "방금 눌렀는가" 판정이
        // 초기화되지 않은 이전 상태와 비교된다.
        Input::Initialize();

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
            // ⚠️ 순서가 중요하다. 표현 컴포넌트를 붙이는 시스템이 렌더보다 **먼저** 와야
            //    스냅샷을 받은 프레임에도 화면이 비지 않는다(PlayerVisualSystem 주석 참조).
            m_clientWorld->RegisterSystem<PlayerVisualSystem>();
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

            Input::Update();
            CollectLocalInput(engine, window);
            FollowLocalPlayer();

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

            UpdateDebugTitle(window);

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

private:
    // 카메라를 내 플레이어에 붙인다.
    //
    // 왜 필요한가: 이동이 tick당 1유닛이고 tick이 50Hz라 **초당 50유닛**이다.
    // 기본 시야가 세로 60유닛이므로 카메라가 고정이면 0.6초 만에 화면 밖으로 나간다.
    // 실제로 W를 1.5초 누르고 캡처했더니 아무도 안 보였다 — 사라진 게 아니라 75유닛
    // 위로 날아간 것이었다.
    //
    // ⚠️ 이건 순수하게 표현이다. 카메라 위치는 시뮬레이션에 영향을 주지 않고 해시에도
    //    안 들어간다. 그래서 클라마다 카메라가 달라도(각자 자기를 따라가도) 결정론은
    //    그대로다 — 오히려 그게 정상이다.
    //
    // 지금은 즉시 따라붙는다(스무딩 없음). 50Hz 시뮬을 그대로 그리는 단계라 부드럽게
    // 만들 여지가 어차피 없고, 보간(R7)이 들어오면 그때 카메라도 같이 다듬는 게 맞다.
    void FollowLocalPlayer() {
        if (m_clientWorld == nullptr) {
            return;
        }

        const uint32_t localId = m_clientWorld->GetResource<NetworkManager>().LocalClientId();
        if (localId == 0) {
            return;   // 아직 Welcome을 못 받았다
        }

        for (auto [entity, transform, control] : m_clientWorld->CreateView<Transform, PlayerControl>()) {
            static_cast<void>(entity);
            if (control.clientId != localId) {
                continue;
            }
            // 화면 평면은 월드 XZ다. Camera::position.y가 월드 Z인 것에 주의.
            m_camera.position = Float2{
                transform.position.x.ToFloat(),
                transform.position.z.ToFloat()
            };
            return;
        }
    }

    // 실제 키보드를 lockstep 경로에 넣는다. tick당 최대 하나.
    //
    // ⚠️ **프레임이 아니라 tick 단위로 넣는다.** 이 루프는 모니터 주사율(보통 60~144Hz)로
    //    도는데 시뮬레이션은 50Hz다. 프레임마다 넣으면 같은 applyTick을 겨냥한 입력이
    //    여러 개 쌓이고, 서버는 그 tick의 빈 슬롯 하나만 채우므로 나머지는 버려진다.
    //    대역폭만 쓰고 얻는 게 없다.
    //
    // ⚠️ 포커스가 없으면 넣지 않는다. Input은 GetAsyncKeyState 기반이라 시스템 전역
    //    키 상태를 읽는다 — 걸러내지 않으면 창 두 개가 한 번의 키 입력에 같이 반응한다.
    //
    // 입력은 여기서 "제출"될 뿐 여기서 적용되지 않는다. 서버가 tick을 확정해 되뿌린
    // 뒤에야 모든 peer가 같은 tick에 같은 입력을 적용한다. 그게 lockstep이고,
    // 그래서 클라는 자기 입력조차 왕복 시간만큼 늦게 본다(예측·롤백은 아직 없다).
    void CollectLocalInput(GameEngine& engine, const IWindow& window) {
        if (m_clientSync == nullptr) {
            return;
        }

        const uint64_t tick = engine.GetCurrentTick();
        if (tick == m_lastInputTick) {
            return;
        }
        m_lastInputTick = tick;

        // 스크립트가 있으면 실제 키보드 대신 그걸 쓴다.
        // 포커스도 보지 않는다 — 사람 없이 돌리는 것이 목적이기 때문이다.
        if (m_hasInputScript) {
            // 지정된 tick의 항목이 있으면 그 마스크로 갱신한다. 없으면 직전 마스크를 유지한다
            // ("W를 20틱에 누르고 80틱에 뗀다"를 두 항목으로 표현할 수 있게).
            if (auto it = m_inputScript.find(tick); it != m_inputScript.end()) {
                m_scriptedMask = it->second;
            }
            if (m_scriptedMask != 0) {
                InputCommand scripted{};
                scripted.buttonMask = m_scriptedMask;
                m_clientSync->QueueLocalInput(scripted);
            }
            return;
        }

        if (window.HasFocus() && !m_loggedFocus) {
            m_loggedFocus = true;
            LOG_INFO("창이 포커스를 얻었다. 이제부터 키 입력을 서버로 보낸다.");
        }

        if (!window.HasFocus()) {
            // 포커스를 잃은 동안은 입력을 보내지 않는다. 서버는 결측 입력을
            // "직전 입력 hold"로 대체하므로, 누르고 있던 방향으로 계속 가는 게 아니라
            // 마지막으로 보낸 상태가 유지된다. 창을 떠나기 전에 키를 뗐다면 멈춘다.
            return;
        }

        const uint64_t buttonMask = BuildButtonMask(&Input::GetKey);
        if (buttonMask != 0 && !m_loggedFirstInput) {
            m_loggedFirstInput = true;
            LOG_INFO("첫 이동 입력 감지 (buttonMask=0x{:X}). 서버 확정을 거쳐 돌아온다.", buttonMask);
        }

        InputCommand command{};
        command.buttonMask = buttonMask;
        // clientId / tick / sequence 는 NetworkSyncSystem이 채운다
        // (서버 tick 추정 + kInputDelayTicks). 여기서 정하면 추정 로직이 두 곳으로 갈린다.
        m_clientSync->QueueLocalInput(command);
    }

    // 창 제목에 결정론 상태를 찍는다.
    //
    // 왜 창 제목인가: 클라이언트 둘을 나란히 띄워 놓고 **같은 tick에서 해시가 같은지**를
    // 보는 것이 이 엔진의 핵심 검증인데, 화면 안에 글자를 그리려면 드로우 목록(U1)과
    // 폰트 아틀라스(U2)가 먼저 있어야 한다. 제목 표시줄은 OS가 이미 그려 주므로 지금
    // 공짜로 쓸 수 있다. 화면 안 디버그 오버레이(U5)가 들어오면 이 용도는 사라진다.
    //
    // ⚠️ 내용이 바뀔 때만 SetTitle을 부른다. SetWindowText는 WM_SETTEXT 동기 전송이라
    //    같은 문자열로 매 프레임 부르면 메시지 왕복과 논클라이언트 영역 재그리기를 그냥 낸다.
    void UpdateDebugTitle(IWindow& window) {
        if (m_clientWorld == nullptr || m_clientSync == nullptr) {
            return;
        }

        // 여기서 재는 해시는 "clientSimTick 까지 적용한 뒤의 로컬 월드"다.
        // 두 클라가 같은 tick에서 같은 값을 보이면 시뮬레이션이 일치한다는 뜻이다.
        //
        // 매 프레임 전체 해시를 다시 계산하는 것은 월드가 커지면 부담이 된다.
        // 지금은 엔티티가 몇 개뿐이라 무시할 만하고, 비싸지면 tick 경계에서만 재거나
        // NetworkSyncSystem이 이미 기록해 둔 tick별 해시를 읽어오면 된다.
        // 접속 진행 상황을 한 번씩만 로그로 남긴다.
        //
        // 이게 없으면 dedicated server 접속이 실패했을 때 화면도 로그도 조용해서
        // "서버가 안 떴나 / 패킷이 안 가나 / 스냅샷이 안 오나"를 구별할 수 없다.
        // 상태가 바뀌는 순간에만 찍으므로 매 프레임 스팸이 되지 않는다.
        if (m_clientSync->HasWelcome() && !m_loggedWelcome) {
            m_loggedWelcome = true;
            LOG_INFO("서버가 clientId {}를 배정했다.",
                     m_clientWorld->GetResource<NetworkManager>().LocalClientId());
        }
        if (m_clientSync->HasSnapshot() && !m_loggedSnapshot) {
            m_loggedSnapshot = true;
            LOG_INFO("첫 월드 스냅샷 수신 (tick {}). 이제부터 확정 입력으로 시뮬레이션한다.",
                     m_clientSync->LastSnapshotTick());
        }

        const uint64_t hash = m_clientWorld->ComputeHash();
        const uint64_t simTick = m_clientSync->ClientSimTick();

        std::string title = "ECSFactory";
        title += (m_mode == Mode::Join) ? " [join]" : " [single]";
        title += " tick=" + std::to_string(simTick);

        char hashText[24] = {};
        std::snprintf(hashText, sizeof(hashText), " hash=%016llX",
                      static_cast<unsigned long long>(hash));
        title += hashText;

        if (!m_clientSync->HasSnapshot()) {
            title += " (스냅샷 대기)";
        }
        if (m_clientSync->HasHashMismatch()) {
            // 디싱크를 감지하면 resync가 돌지만, 그런 일이 있었다는 사실 자체가 정보다.
            title += " [디싱크!]";
        }

        if (title != m_lastTitle) {
            window.SetTitle(title.c_str());
            m_lastTitle = std::move(title);
        }
    }

protected:
    void OnShutdown(GameEngine& engine) override {
        static_cast<void>(engine);
        StopLocalServer();
        m_clientWorld = nullptr;
        LOG_INFO("Client shutting down.");
    }

private:
    // 링크 열화가 켜져 있으면 연결을 한 겹 감싼다. 꺼져 있으면 그대로 돌려준다.
    //
    // ⚠️ 감싼 경우 매 fixed tick Advance()를 돌려야 지연 큐가 풀린다. 그 펌프는
    //    LossyLinkPumpSystem이 담당한다 — 앱 루프에서 부르면 프레임률에 묶여서
    //    "3틱 지연"이 프레임률마다 다른 시간이 되어버린다.
    std::shared_ptr<Network::INetworkConnection> WrapWithLossyLink(
        std::shared_ptr<Network::INetworkConnection> connection) {
        if (!m_hasLossyLink) {
            return connection;
        }

        auto lossy = std::make_shared<Network::LossyConnection>(std::move(connection), m_linkSettings);
        m_lossyLink = lossy;
        LOG_INFO("링크 열화 주입: 손실 {:.0f}%, 중복 {:.0f}%, 지연 {}~{}틱, 시드 {}",
                 m_linkSettings.lossRate * 100.0f, m_linkSettings.duplicateRate * 100.0f,
                 m_linkSettings.minDelayTicks, m_linkSettings.maxDelayTicks, m_linkSettings.seed);
        return lossy;
    }

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

        // 펌프를 sync 시스템보다 **먼저** 등록한다. 등록 순서가 곧 실행 순서라
        // (SortSystems는 아직 TODO), 이번 tick에 도착 예정인 데이터그램을 먼저 풀어
        // 놓아야 같은 tick의 sync가 그걸 읽는다. 뒤에 두면 항상 한 tick씩 더 늦는다.
        if (m_lossyLink) {
            m_clientWorld->RegisterSystem<LossyLinkPumpSystem>(m_lossyLink);
        }

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
    std::string m_lastTitle;
    bool m_loggedWelcome = false;
    bool m_loggedSnapshot = false;
    uint64_t m_lastInputTick = UINT64_MAX;
    bool m_loggedFocus = false;
    bool m_loggedFirstInput = false;

    // tick → 버튼 마스크. 스크립트 입력(가짜 키보드)용.
    std::map<uint64_t, uint64_t> m_inputScript;
    bool m_hasInputScript = false;
    Network::LossyLinkSettings m_linkSettings{};
    bool m_hasLossyLink = false;
    std::shared_ptr<Network::LossyConnection> m_lossyLink;
    uint64_t m_scriptedMask = 0;
    uint64_t m_frameLimit = 0;
    std::string m_capturePath;
};

int main(int argc, char** argv) {
    Logger::Init("game_client.log");

    GameClientApplication app;

    // --frames N : N프레임만 돌고 끝낸다. 사람이 창을 닫아줄 수 없는 환경
    // (CI, 자동 확인)에서 렌더 경로가 실제로 도는지 보기 위한 것이다.
    EF::Network::LossyLinkSettings link{};
    bool hasLink = false;

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
        } else if (arg == "--input-script" && hasNext) {
            // 가짜 키보드. "20:W,80:" = tick 20부터 앞으로, tick 80에 정지.
            // 포커스에 묶이지 않아 사람 없이 결정론 검증을 돌릴 수 있다.
            app.SetInputScript(argv[++i]);
        } else if (arg == "--net-loss" && hasNext) {
            link.lossRate = static_cast<float>(std::atof(argv[++i])) / 100.0f;
            hasLink = true;
        } else if (arg == "--net-delay" && hasNext) {
            // "3" 또는 "1-4"(지터). 틱 단위이고 고정 timestep 20ms 기준이라 3틱 = 60ms.
            const std::string spec = argv[++i];
            const std::size_t dash = spec.find('-');
            link.minDelayTicks = static_cast<uint32_t>(std::strtoul(spec.substr(0, dash).c_str(), nullptr, 10));
            link.maxDelayTicks = (dash == std::string::npos)
                ? link.minDelayTicks
                : static_cast<uint32_t>(std::strtoul(spec.substr(dash + 1).c_str(), nullptr, 10));
            hasLink = true;
        } else if (arg == "--net-dup" && hasNext) {
            link.duplicateRate = static_cast<float>(std::atof(argv[++i])) / 100.0f;
            hasLink = true;
        } else if (arg == "--net-seed" && hasNext) {
            // 같은 시드 + 같은 트래픽 = 같은 손실 패턴. 재현의 핵심이라 반드시 로그에 남긴다.
            link.seed = std::strtoull(argv[++i], nullptr, 10);
            hasLink = true;
        } else if (arg == "--single") {
            // 같은 프로세스 안에 서버 월드를 두고 인메모리로 연결한다.
            app.SetMode(GameClientApplication::Mode::SinglePlayer);
        }
    }

    if (hasLink) {
        app.SetLossyLink(link);
    }

    return app.Run();
}
