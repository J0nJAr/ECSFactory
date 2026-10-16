#include "NetworkSyncSystem.hpp"

#include <algorithm>

#include "Base/Logger.hpp"

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Gameplay/PlayerMovement.hpp"

namespace EF {
    namespace {
        constexpr uint64_t kInputDelayTicks = 2;
        constexpr uint32_t kInputRedundancySends = 3;

        // tick별 로컬 해시를 몇 개까지 들고 있을지.
        // 50Hz 기준 128 tick ≈ 2.5초로, 입력 지연(2 tick) + 왕복 지연 + 재전송(4 tick 간격)을
        // 다 합쳐도 한참 남는 창이다. 이보다 오래 밀린 TickHash는 비교해봐야 의미가 없다.
        constexpr std::size_t kLocalHashHistoryTicks = 128;

        // 접속 순서대로 플레이어를 x축에 늘어놓을 때의 간격(월드 유닛).
        //
        // 6을 고른 근거: ApplyConfirmedInputs의 이동 스텝이 tick당 1유닛이므로, 6유닛은
        // "6 tick(50Hz에서 약 0.12초) 동안 한 방향으로 밀어야 옆 사람 자리에 닿는" 거리다.
        // 스폰 직후 서로 겹쳐 보이지 않을 만큼은 떨어져 있고, 화면 밖으로 나갈 만큼

    }

    NetworkSyncSystem::NetworkSyncSystem(NetworkSyncMode mode)
        : m_mode(mode) {
    }

    void NetworkSyncSystem::Initialize(World& world) {
        static_cast<void>(world);
    }

    void NetworkSyncSystem::FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) {
        static_cast<void>(fixedDeltaTime);

        NetworkManager& network = world.GetResource<NetworkManager>();
        if (m_mode == NetworkSyncMode::Client) {
            if (m_hasServerTickEstimate) {
                ++m_serverTickEstimate;
            }
            UpdateClient(world, network);
            SendQueuedLocalInputs(network, tick);
        } else {
            UpdateServer(world, network, tick);
        }

        // 재전송/누적 ack를 흘려보낸다.
        network.Update(tick);
    }

    void NetworkSyncSystem::FrameUpdate(World& world, float frameDeltaSeconds) {
        static_cast<void>(world);
        static_cast<void>(frameDeltaSeconds);
    }

    void NetworkSyncSystem::Shutdown(World& world) {
        static_cast<void>(world);
    }

    void NetworkSyncSystem::QueueLocalInput(const InputCommand& command) {
        m_localInputs.push_back(command);
    }

    void NetworkSyncSystem::UpdateClient(World& world, NetworkManager& network) {
        if (!m_sentHello) {
            m_sentHello = network.SendHello();
        }

        while (auto received = network.TryReceive()) {
            HandlePacketOnClient(world, network, received->packet);
        }

        ApplyBufferedInputFrames(world, network);
        VerifyPendingTickHashes(network);
    }

    namespace {
        // 지금 서버 월드가 "어느 tick까지 시뮬레이션한 상태"인지.
        //
        // ⚠️ 이 한 줄이 resync 정확성의 핵심이다. UpdateServer의 순서가
        //        수신 처리(여기서 스냅샷을 만든다) → ConfirmInputs → ApplyConfirmedInputs → TickHash 방송
        //    이라, 패킷을 처리하는 시점의 월드는 **tick의 입력을 아직 적용하지 않았다.**
        //    즉 tick-1 까지 시뮬레이션한 상태다.
        //
        //    예전에는 이걸 그냥 `tick` 으로 라벨링했고, 그래서 이런 일이 벌어졌다.
        //    클라는 스냅샷을 받고 `clientSimTick = snapshot.tick` 으로 놓는데,
        //    같은 tick의 InputFrame은 `frame.tick > m_clientSimTick` 을 통과하지 못해
        //    **버려진다.** 결과적으로 resync 할 때마다 확정 입력을 정확히 한 프레임 삼키고,
        //    월드가 계속 바뀌는 중이면 영원히 한 스텝 뒤처진 채 재동기화를 반복했다
        //    (실측: 200 tick / 스냅샷 200개를 받고도 미복구).
        //    월드가 정지해 있을 때만 삼킨 프레임이 no-op이라 우연히 복구됐다.
        //
        //    tick 0에서는 뺄 것이 없다. 그때는 아직 아무 입력도 적용되지 않은 초기 상태이고,
        //    라벨 0이 곧 "아무것도 시뮬레이션하지 않았다"라 의미가 맞는다.
        [[nodiscard]] uint64_t SimulatedThroughTick(uint64_t tick) noexcept {
            return tick == 0 ? 0 : tick - 1;
        }
    }

    void NetworkSyncSystem::UpdateServer(World& world, NetworkManager& network, uint64_t tick) {
        while (auto received = network.TryReceive()) {
            HandlePacketOnServer(world, network, received->from, received->packet, tick);
        }

        DropTimedOutClients(world, network, tick);

        // 이 tick에 확정된 것: 세션 이벤트 먼저, 그다음 입력.
        //
        // ⚠️ 순서가 계약이다. 같은 tick에 접속과 입력이 함께 있으면 엔티티가 먼저 있어야
        //    입력이 갈 곳이 생긴다. 클라이언트도 같은 순서로 적용한다(ApplyBufferedInputFrames).
        const std::vector<SessionEvent> events = network.ConfirmSessionEvents(tick);
        ApplySessionEvents(world, events);

        // Join으로 만들어진 엔티티를 서버 장부에 연결한다.
        // 이건 월드 상태가 아니라 서버만 쓰는 조회용이라 여기서 해도 결정론에 영향이 없다.
        for (const SessionEvent& event : events) {
            if (event.kind == SessionEvent::Kind::Join) {
                network.BindControlledEntity(event.clientId, FindPlayerEntityFor(world, event.clientId));
            }
        }

        const std::vector<InputCommand> confirmed = network.ConfirmInputs(tick);
        ApplyConfirmedInputs(world, confirmed);

        const uint64_t worldHash = world.ComputeHash();
        for (const auto& [clientId, state] : network.Clients()) {
            static_cast<void>(clientId);
            if (state.endpoint.IsValid()) {
                network.SendInputFrame(InputFrame{
                    .tick = tick,
                    .events = events,
                    .inputs = confirmed
                }, state.endpoint);
                network.SendTickHash(TickHash{
                    .tick = tick,
                    .worldHash = worldHash
                }, state.endpoint);
            }
        }
    }

    // 오랫동안 아무 패킷도 오지 않은 클라이언트를 정리한다.
    //
    // 안 하면 두 가지가 샌다.
    //  1. **신뢰성 채널.** 서버는 매 tick 등록된 전원에게 InputFrame을 reliable로 보낸다.
    //     떠난 클라의 채널은 ack를 영영 못 받아 미확인 레코드가 tick당 하나씩 무한히 쌓이고,
    //     재전송도 계속 나간다. 메모리와 대역폭이 함께 샌다.
    //  2. **월드.** 조종자가 없는 플레이어 엔티티가 영원히 남는다. UDP로 재접속하면
    //     endpoint가 달라져 새 clientId + 새 스폰이 되므로 유령이 쌓인다.
    //
    // 📌 엔티티 파괴는 여기서 하지 않는다. Leave 이벤트를 다음 tick에 예약해서
    //    서버와 모든 클라가 **각자 같은 tick에** 같은 엔티티를 파괴한다(N2).
    //    여기서 정리하는 것은 월드 상태가 아닌 것들뿐이다 — 장부와 신뢰성 채널.
    //
    //    아직 남은 것: 설계 문서 §8의 Dormant(파괴 대신 얼리기). 지금은 나갔다 들어오면
    //    새 clientId + 새 캐릭터가 된다. 영속 플레이어 정체성이 생길 때 함께 처리한다.
    void NetworkSyncSystem::DropTimedOutClients(World& world, NetworkManager& network, uint64_t tick) {
        // 엔티티 파괴는 확정 이벤트가 하므로 여기서 월드를 직접 만지지 않는다.
        // 인자는 시그니처 일관성(다른 Update* 계열과 같은 모양)을 위해 남긴다.
        static_cast<void>(world);

        // 5초(50Hz 기준 250 tick). 짧으면 잠깐 끊긴 클라를 쫓아내고, 길면 유령이 오래 남는다.
        // 신뢰성 계층의 재전송이 이 안에서 몇 번이고 시도할 수 있는 길이로 잡았다.
        constexpr uint64_t kClientTimeoutTicks = 250;

        // 삭제 대상을 먼저 모은다. Clients()를 순회하면서 지우면 반복자가 무효가 된다.
        //
        // ⚠️ clientId로 정렬한다. Clients()는 unordered_map이라 순회 순서가 프로세스마다
        //    다를 수 있고, 여기서 world.DestroyEntity를 부르므로 **월드 상태를 만들어내는
        //    순회**다. 결정론 규칙 2번(상태를 만드는 순회는 결정론적 순서로).
        std::vector<uint32_t> timedOut;
        for (const auto& [clientId, state] : network.Clients()) {
            if (tick < state.lastHeardFromTick + kClientTimeoutTicks) {
                continue;
            }
            timedOut.push_back(clientId);
        }
        std::sort(timedOut.begin(), timedOut.end());

        for (uint32_t clientId : timedOut) {
            // ⚠️ 여기서 엔티티를 직접 파괴하지 않는다. 스폰과 같은 이유다 —
            //    확정 경로 밖에서 월드를 바꾸면 EntityManager가 갈라진다.
            //    다음 tick에 확정될 Leave 이벤트를 예약하면 서버와 모든 클라가
            //    각자 같은 tick에 같은 엔티티를 파괴한다.
            //
            //    ⚠️ 예약을 **먼저** 하고 등록을 해제한다. 순서를 뒤집으면 이 클라가
            //       Clients()에서 사라진 뒤에 이벤트가 확정되는데, 그 자체는 문제없지만
            //       예약 실패 시 유령이 남는다. 예약이 확실히 큐에 들어간 뒤에 지운다.
            network.QueueSessionEvent(tick + 1, SessionEvent{
                .kind = SessionEvent::Kind::Leave,
                .clientId = clientId
            });

            // 장부와 신뢰성 채널은 지금 정리한다. 이건 월드 상태가 아니고,
            // 채널을 남겨두면 ack 없는 reliable 레코드가 계속 쌓인다.
            network.UnregisterClient(clientId);

            LOG_INFO("클라이언트 {} 타임아웃({}틱 무응답). 등록을 정리하고 "
                     "다음 tick에 Leave를 확정한다.", clientId, kClientTimeoutTicks);
        }
    }

    void NetworkSyncSystem::SendQueuedLocalInputs(NetworkManager& network, uint64_t tick) {
        if (network.LocalClientId() == 0) {
            return;
        }

        while (!m_localInputs.empty()) {
            InputCommand command = m_localInputs.front();
            if (command.clientId == 0) {
                command.clientId = network.LocalClientId();
            }
            if (command.tick == 0) {
                command.tick = (m_hasServerTickEstimate ? m_serverTickEstimate : tick) + kInputDelayTicks;
            }
            if (command.sequence == 0) {
                command.sequence = m_nextInputSequence;
            }

            if (m_localInputs.front().sequence == 0) {
                ++m_nextInputSequence;
            }
            m_redundantInputs.push_back(RedundantInput{
                .command = command,
                .remainingSends = kInputRedundancySends
            });
            m_localInputs.pop_front();
        }

        for (auto it = m_redundantInputs.begin(); it != m_redundantInputs.end();) {
            if (it->remainingSends == 0) {
                it = m_redundantInputs.erase(it);
                continue;
            }

            if (!network.SendInputCommand(it->command)) {
                break;
            }

            --it->remainingSends;
            if (it->remainingSends == 0) {
                it = m_redundantInputs.erase(it);
            } else {
                ++it;
            }
        }
    }

    void NetworkSyncSystem::ApplyBufferedInputFrames(World& world, NetworkManager& network) {
        static_cast<void>(network);
        if (!m_hasSnapshot) {
            return;
        }

        for (;;) {
            const uint64_t nextTick = m_clientSimTick + 1;
            auto frameIt = m_frameBuffer.find(nextTick);
            if (frameIt == m_frameBuffer.end()) {
                break;
            }

            if (m_debugSkipNextConfirmedFrame) {
                // ⚠️ 디버그 주입 경로다(DebugSkipNextConfirmedInputFrameOnce).
                // 이 프레임을 "받았지만 적용하지 않는다". tick은 아래에서 정상 진행시킨다 —
                // 여기서 멈추면 디싱크가 아니라 지연된 클라이언트가 되어 버린다.
                // 1회성이라 여기서 스스로 꺼진다.
                m_debugSkipNextConfirmedFrame = false;
            } else {
                // 서버와 **같은 순서**로 적용한다: 세션 이벤트 → 입력.
                ApplySessionEvents(world, frameIt->second.events);
                ApplyConfirmedInputs(world, frameIt->second.inputs);
            }
            m_clientSimTick = nextTick;
            m_frameBuffer.erase(frameIt);

            // 이 tick을 시뮬레이션한 "그 순간"의 해시를 남긴다.
            // 루프가 여러 프레임을 몰아 적용할 수 있으므로, 나중에 현재 해시를 다시
            // 계산하면 어느 tick의 상태인지 알 수 없게 된다.
            RecordLocalHash(world, m_clientSimTick);
        }
    }

    void NetworkSyncSystem::RecordLocalHash(World& world, uint64_t tick) {
        m_localHashByTick[tick] = world.ComputeHash();

        // 무한히 쌓이지 않게 자른다. TickHash는 서버가 매 tick 보내고 클라는 입력 지연 +
        // RTT만큼 뒤처지므로, 이 창보다 오래 밀린 해시는 어차피 비교 가치가 없다.
        while (m_localHashByTick.size() > kLocalHashHistoryTicks) {
            m_localHashByTick.erase(m_localHashByTick.begin());
        }
    }

    void NetworkSyncSystem::VerifyPendingTickHashes(NetworkManager& network) {
        if (!m_hasSnapshot) {
            return;
        }

        for (auto it = m_pendingTickHashes.begin(); it != m_pendingTickHashes.end();) {
            const uint64_t tick = it->first;
            if (tick > m_clientSimTick) {
                // 아직 그 tick을 시뮬레이션하지 않았다. 따라잡을 때까지 보류한다.
                break;
            }

            const uint64_t remoteHash = it->second;

            // ⚠️ 반드시 "그 tick의" 로컬 해시와 비교한다. world.ComputeHash()를 여기서
            //    부르면 현재(= clientSimTick) 상태가 나오는데, 프레임이 몰아서 적용되면
            //    tick != clientSimTick 이라 정상 상태를 불일치로 판정한다.
            auto localIt = m_localHashByTick.find(tick);
            if (localIt == m_localHashByTick.end()) {
                // 기록이 잘려 나갔거나 애초에 시뮬레이션한 적 없는 tick이다.
                // 판정할 근거가 없으므로 조용히 버린다 — 추측으로 디싱크를 선언하지 않는다.
                it = m_pendingTickHashes.erase(it);
                continue;
            }

            const uint64_t localHash = localIt->second;
            if (localHash != remoteHash) {
                m_hasHashMismatch = true;

                // ⚠️ 요청은 **한 번에 하나만** 띄운다.
                //
                //    디싱크가 나면 그 뒤의 tick들도 줄줄이 어긋난다. 불일치하는 pending tick
                //    마다 요청을 보내면, 서버는 요청 하나당 **월드 전체 스냅샷**을 reliable로
                //    답한다. 공장 게임처럼 엔티티가 수만 개인 월드에서는 그게 곧 대역폭 붕괴다.
                //    실측으로도 한 번의 디싱크에 스냅샷이 3개 왔다(하나면 충분한 상황에서).
                //
                //    스냅샷이 도착하면 ApplySnapshot 경로가 이 플래그를 내린다. 그때까지는
                //    불일치를 계속 "감지"하되 요청은 보내지 않는다 — 감지를 멈추면
                //    HasHashMismatch()가 깜빡여서 디버그 표시가 쓸모없어진다.
                if (!m_resyncRequestInFlight) {
                    // ⚠️ 반환값을 플래그에 대입하면 안 된다. NetworkManager::SendPacket 은
                    //    레코드를 신뢰성 채널에 **먼저 큐에 넣고** flush 결과를 돌려준다.
                    //    즉 false는 "안 보냈다"가 아니라 "지금 소켓 전송이 실패했다"이고,
                    //    그 요청은 이미 큐에 있어서 재전송으로 결국 나간다.
                    //    false를 보고 다시 보내면 요청이 중복 적재되고, 링크가 회복되는 순간
                    //    그게 한꺼번에 나가 서버가 스냅샷을 그 수만큼 답한다 — 막으려던
                    //    폭주가 오히려 링크가 나쁠 때 터진다.
                    network.SendResyncRequest(tick, localHash, remoteHash);
                    m_resyncRequestInFlight = true;
                }
            }
            it = m_pendingTickHashes.erase(it);
        }
    }

    // 클라이언트 모드에서 수신한 패킷 하나를 처리한다.
    // 여기서는 상태만 갱신하고 시뮬레이션은 돌리지 않는다 —
    // 프레임 적용은 ApplyBufferedInputFrames가 순서를 통제해서 한다.
    void NetworkSyncSystem::HandlePacketOnClient(World& world, NetworkManager& network, const Network::NetworkPacket& packet) {
        switch (packet.type) {
        case Network::PacketType::Welcome: {
            uint32_t assignedClientId = 0;
            if (NetworkManager::DecodeWelcome(packet.payload, assignedClientId) && assignedClientId != 0) {
                network.SetLocalClientId(assignedClientId);
                m_hasWelcome = true;
                if (m_hasTypeTable && !m_requestedSnapshot) {
                    m_requestedSnapshot = network.SendWorldSnapshotRequest(m_lastSnapshotTick);
                }
            }
            break;
        }

        case Network::PacketType::TypeTable:
            if (NetworkManager::DecodeTypeTable(packet.payload, m_remoteTypes)) {
                m_hasTypeTable = true;
                if (m_hasWelcome && !m_requestedSnapshot) {
                    m_requestedSnapshot = network.SendWorldSnapshotRequest(m_lastSnapshotTick);
                }
            }
            break;

        case Network::PacketType::WorldSnapshot: {
            WorldSnapshot snapshot;
            if (NetworkManager::DecodeWorldSnapshot(packet.payload, snapshot) && world.ApplySnapshot(snapshot)) {
                m_hasSnapshot = true;
                m_hasHashMismatch = false;

                // 요청한 스냅샷이 도착했다. 다음 불일치에는 다시 요청할 수 있다.
                m_resyncRequestInFlight = false;
                ++m_appliedSnapshotCount;
                m_lastSnapshotTick = snapshot.tick;
                m_clientSimTick = snapshot.tick;
                m_serverTickEstimate = snapshot.tick;
                m_hasServerTickEstimate = true;
                m_frameBuffer.erase(m_frameBuffer.begin(), m_frameBuffer.upper_bound(m_clientSimTick));

                // 스냅샷은 월드를 통째로 갈아끼운다. 이전 해시 기록은 지금 월드와 무관하므로
                // 버리고, 스냅샷 tick의 해시를 새 기준선으로 남긴다.
                m_localHashByTick.clear();
                RecordLocalHash(world, m_clientSimTick);

                // 스냅샷보다 오래된 TickHash는 판정 근거가 사라졌다.
                m_pendingTickHashes.erase(m_pendingTickHashes.begin(),
                                          m_pendingTickHashes.lower_bound(m_clientSimTick));
            }
            break;
        }

        case Network::PacketType::InputFrame: {
            InputFrame frame;
            if (NetworkManager::DecodeInputFrame(packet.payload, frame) && m_hasSnapshot && frame.tick > m_clientSimTick) {
                m_frameBuffer.try_emplace(frame.tick, std::move(frame));
            }
            break;
        }

        case Network::PacketType::TickHash: {
            TickHash tickHash;
            if (NetworkManager::DecodeTickHash(packet.payload, tickHash)) {
                m_lastRemoteTickHash = tickHash;
                m_serverTickEstimate = tickHash.tick;
                m_hasServerTickEstimate = true;
                if (m_hasSnapshot) {
                    m_pendingTickHashes[tickHash.tick] = tickHash.worldHash;

                    // 상한을 건다. InputFrame에 구멍이 나서 클라 시뮬레이션이 멈추면
                    // 미래 tick의 TickHash가 계속 쌓이기만 한다.
                    //
                    // 넘칠 때 "가장 새 것"을 버리는 이유: 검증은 오래된 tick부터 순서대로
                    // 소비된다. 구멍이 재전송으로 메워지면 클라는 버퍼된 프레임을 따라
                    // 앞에서부터 따라잡으므로, 가까운 미래의 해시가 먼 미래의 것보다 쓸모 있다.
                    while (m_pendingTickHashes.size() > kLocalHashHistoryTicks) {
                        m_pendingTickHashes.erase(std::prev(m_pendingTickHashes.end()));
                    }
                }
            }
            break;
        }

        default:
            break;
        }
    }

    // 서버 모드에서 수신한 패킷을 처리한다.
    void NetworkSyncSystem::HandlePacketOnServer(World& world, NetworkManager& network, const Network::Endpoint& from, const Network::NetworkPacket& packet, uint64_t tick) {
        switch (packet.type) {
        case Network::PacketType::Hello: {
            if (NetworkManager::DecodeHello(packet.payload)) {
                // 같은 endpoint의 Hello 재도착이면 기존 클라이언트를 재사용한다.
                uint32_t clientId = network.ClientIdForEndpoint(from);
                if (clientId == 0) {
                    clientId = network.AllocateClientId();

                    // ⚠️ 여기서 엔티티를 만들지 않는다. 이게 N2의 요점이다.
                    //
                    // clientId 발급과 endpoint 등록은 **서버의 장부**일 뿐 월드 상태가 아니라
                    // 여기서 해도 된다. 하지만 플레이어 엔티티를 만드는 것은 월드를 바꾸는
                    // 일이고, 그걸 확정 입력 밖에서 하면 그 순간 서버의 EntityManager가
                    // 클라들과 갈라진다 — 이후 CreateEntity가 서로 다른 index를 뽑는다.
                    //
                    // 대신 다음 tick에 확정될 Join 이벤트를 예약한다. 그러면 서버와 모든
                    // 클라이언트가 **각자** 같은 tick에 같은 엔티티를 만든다.
                    network.RegisterClient(clientId, kNullEntity, from, tick);
                    network.QueueSessionEvent(tick + 1, SessionEvent{
                        .kind = SessionEvent::Kind::Join,
                        .clientId = clientId
                    });
                }
                network.SendWelcome(clientId, from);
                network.SendTypeTable(world.BuildTypeTable(), from);
            }
            break;
        }

        case Network::PacketType::WorldSnapshotRequest: {
            uint64_t lastKnownTick = 0;
            if (NetworkManager::DecodeWorldSnapshotRequest(packet.payload, lastKnownTick)) {
                static_cast<void>(lastKnownTick);
                network.SendWorldSnapshot(world.CreateSnapshot(SimulatedThroughTick(tick)), from);
            }
            break;
        }

        case Network::PacketType::InputCommand: {
            InputCommand command;
            if (NetworkManager::DecodeInputCommand(packet.payload, command)) {
                // 페이로드의 clientId를 믿지 않고 endpoint로 인가한다(스푸핑 방지).
                const uint32_t authoritativeId = network.ClientIdForEndpoint(from);
                if (authoritativeId != 0) {
                    command.clientId = authoritativeId;
                    network.EnqueueInput(command);
                }
            }
            break;
        }

        case Network::PacketType::ResyncRequest: {
            uint64_t resyncTick = 0;
            uint64_t localHash = 0;
            uint64_t remoteHash = 0;
            if (NetworkManager::DecodeResyncRequest(packet.payload, resyncTick, localHash, remoteHash)) {
                static_cast<void>(resyncTick);
                static_cast<void>(localHash);
                static_cast<void>(remoteHash);
                network.SendWorldSnapshot(world.CreateSnapshot(SimulatedThroughTick(tick)), from);
            }
            break;
        }

        default:
            break;
        }
    }

}
