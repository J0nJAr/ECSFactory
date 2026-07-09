#include "NetworkSyncSystem.hpp"

namespace EF {

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
    }

    void NetworkSyncSystem::UpdateServer(World& world, NetworkManager& network, uint64_t tick) {
        while (auto received = network.TryReceive()) {
            HandlePacketOnServer(world, network, received->from, received->packet, tick);
        }

        // 등록된 모든 클라이언트에게 현재 월드 hash를 보낸다.
        const uint64_t worldHash = world.ComputeHash();
        for (const auto& [clientId, state] : network.Clients()) {
            static_cast<void>(clientId);
            if (state.endpoint.IsValid()) {
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
                command.tick = tick;
            }
            if (command.sequence == 0) {
                command.sequence = m_nextInputSequence;
            }

            if (!network.SendInputCommand(command)) {
                break;
            }

            if (m_localInputs.front().sequence == 0) {
                ++m_nextInputSequence;
            }
            m_localInputs.pop_front();
        }
    }

    /// @brief 클라이언트 모드에서 수신한 패킷을 처리한다.
    /// @param world
    /// @param network
    /// @param packet
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
            }
            break;
        }

        case Network::PacketType::TickHash: {
            TickHash tickHash;
            if (NetworkManager::DecodeTickHash(packet.payload, tickHash)) {
                m_lastRemoteTickHash = tickHash;
                const uint64_t localHash = world.ComputeHash();
                if (m_hasSnapshot && localHash != tickHash.worldHash) {
                    m_hasHashMismatch = true;
                    network.SendResyncRequest(tickHash.tick, localHash, tickHash.worldHash);
                }
            }
            break;
        }

        default:
            break;
        }
    }

    /// @brief 서버 모드에서 수신한 패킷을 처리한다.
    /// @param world
    /// @param network
    /// @param from 보낸 사람 endpoint. 응답 라우팅과 clientId 인가에 쓴다.
    /// @param packet
    /// @param tick
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
