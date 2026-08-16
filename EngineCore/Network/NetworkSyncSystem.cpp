#include "NetworkSyncSystem.hpp"

#include "Gameplay/PlayerMovement.hpp"

namespace EF {
    namespace {
        constexpr uint64_t kInputDelayTicks = 2;
        constexpr uint32_t kInputRedundancySends = 3;

        // tick별 로컬 해시를 몇 개까지 들고 있을지.
        // 50Hz 기준 128 tick ≈ 2.5초로, 입력 지연(2 tick) + 왕복 지연 + 재전송(4 tick 간격)을
        // 다 합쳐도 한참 남는 창이다. 이보다 오래 밀린 TickHash는 비교해봐야 의미가 없다.
        constexpr std::size_t kLocalHashHistoryTicks = 128;
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

    void NetworkSyncSystem::UpdateServer(World& world, NetworkManager& network, uint64_t tick) {
        while (auto received = network.TryReceive()) {
            HandlePacketOnServer(world, network, received->from, received->packet, tick);
        }

        // 등록된 모든 클라이언트에게 현재 월드 hash를 보낸다.
        const std::vector<InputCommand> confirmed = network.ConfirmInputs(tick);
        ApplyConfirmedInputs(world, confirmed);

        const uint64_t worldHash = world.ComputeHash();
        for (const auto& [clientId, state] : network.Clients()) {
            static_cast<void>(clientId);
            if (state.endpoint.IsValid()) {
                network.SendInputFrame(InputFrame{
                    .tick = tick,
                    .inputs = confirmed
                }, state.endpoint);
                network.SendTickHash(TickHash{
                    .tick = tick,
                    .worldHash = worldHash
                }, state.endpoint);
            }
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

            ApplyConfirmedInputs(world, frameIt->second);
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
                network.SendResyncRequest(tick, localHash, remoteHash);
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
                m_frameBuffer.try_emplace(frame.tick, std::move(frame.inputs));
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
                    const EntityId controlledEntity = world.CreateEntity();
                    NetworkClientState& client = network.RegisterClient(clientId, controlledEntity, from);
                    client.hasTypeTable = true;
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
                network.SendWorldSnapshot(world.CreateSnapshot(tick), from);
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
                network.SendWorldSnapshot(world.CreateSnapshot(tick), from);
            }
            break;
        }

        default:
            break;
        }
    }

}
