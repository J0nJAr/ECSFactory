#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ECS/World.hpp"
#include "Math/Fixed64.hpp"
#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"
#include "Network/Packet.hpp"
#include "Network/ReliabilityChannel.hpp"

namespace EF {

    struct InputCommand {
        uint32_t clientId = 0;
        uint64_t tick = 0;
        uint32_t sequence = 0;
        uint64_t buttonMask = 0;
        // 마우스 룩 등은 화면 픽셀(float)이 아니라 결정론적 월드 의도(Fixed64)로 싣는다.
        Fixed64 aimYaw = Fixed64::Zero();
        Fixed64 aimPitch = Fixed64::Zero();
    };

    struct TickHash {
        uint64_t tick = 0;
        uint64_t worldHash = 0;
    };

    struct NetworkClientState {
        uint32_t clientId = 0;
        EntityId controlledEntity = kNullEntity;
        bool hasControlledEntity = false;
        Network::Endpoint endpoint;      // 이 클라이언트의 전송 주소(라우팅용)
        uint64_t lastReceivedTick = 0;
        bool hasTypeTable = false;
        bool hasSnapshot = false;
        std::vector<ComponentTypeDescriptor> remoteTypes;
        std::deque<InputCommand> pendingInputs;

        // 입력 시퀀스 추적: 중복/구식 InputCommand를 걸러내는 데 쓴다.
        uint32_t lastInputSequence = 0;
        bool hasInput = false;
    };

    // 수신된 패킷 하나 + 보낸 사람 endpoint.
    struct ReceivedPacket {
        Network::Endpoint from;
        Network::NetworkPacket packet;
    };

    class NetworkManager {
    public:
        explicit NetworkManager(std::shared_ptr<Network::INetworkConnection> connection);

        [[nodiscard]] Network::INetworkConnection& Connection() noexcept {
            return *m_connection;
        }

        [[nodiscard]] const Network::INetworkConnection& Connection() const noexcept {
            return *m_connection;
        }

        [[nodiscard]] uint32_t LocalClientId() const noexcept {
            return m_localClientId;
        }

        void SetLocalClientId(uint32_t clientId) noexcept {
            m_localClientId = clientId;
        }

        [[nodiscard]] Network::Endpoint DefaultTarget() const noexcept {
            return m_defaultTarget;
        }

        uint32_t AllocateClientId();
        NetworkClientState& GetOrCreateClient(uint32_t clientId);
        [[nodiscard]] const NetworkClientState* FindClient(uint32_t clientId) const;
        NetworkClientState& RegisterClient(uint32_t clientId,
                                           EntityId controlledEntity = kNullEntity,
                                           const Network::Endpoint& endpoint = {});
        // endpoint로 인가된 clientId를 찾는다(스푸핑 방지). 없으면 0.
        [[nodiscard]] uint32_t ClientIdForEndpoint(const Network::Endpoint& endpoint) const;
        [[nodiscard]] const std::unordered_map<uint32_t, NetworkClientState>& Clients() const noexcept {
            return m_clients;
        }

        // 시퀀스 기준 중복/구식 입력은 폐기한다. 수락되면 true.
        bool EnqueueInput(const InputCommand& command);
        [[nodiscard]] const std::deque<InputCommand>& PendingInputs(uint32_t clientId) const;
        std::vector<InputCommand> ConsumePendingInputs(uint32_t clientId, uint64_t upToTick);

        // 송신. target을 비워두면 연결의 기본 상대(DefaultTarget)로 보낸다.
        bool SendHello(const Network::Endpoint& target = {});
        bool SendWelcome(uint32_t assignedClientId, const Network::Endpoint& target = {});
        bool SendTypeTable(const std::vector<ComponentTypeDescriptor>& types, const Network::Endpoint& target = {});
        bool SendWorldSnapshotRequest(uint64_t lastKnownTick, const Network::Endpoint& target = {});
        bool SendWorldSnapshot(const WorldSnapshot& snapshot, const Network::Endpoint& target = {});
        bool SendInputCommand(const InputCommand& command, const Network::Endpoint& target = {});
        bool SendTickHash(const TickHash& tickHash, const Network::Endpoint& target = {});
        bool SendResyncRequest(uint64_t tick, uint64_t localHash, uint64_t remoteHash,
                               const Network::Endpoint& target = {});

        // 매 fixed tick 호출: 재전송/누적 ack를 흘려보낸다.
        void Update(uint64_t tick);

        // 다음 전달 가능한 패킷 하나(보낸 사람 endpoint 포함). 없으면 nullopt.
        [[nodiscard]] std::optional<ReceivedPacket> TryReceive();
        // endpoint를 무시하는 편의 래퍼(기존 호출부 호환).
        [[nodiscard]] std::optional<Network::NetworkPacket> TryReceivePacket();

        static std::vector<uint8_t> EncodeHello();
        static bool DecodeHello(const std::vector<uint8_t>& payload);

        static std::vector<uint8_t> EncodeWelcome(uint32_t assignedClientId);
        static bool DecodeWelcome(const std::vector<uint8_t>& payload, uint32_t& outAssignedClientId);

        static std::vector<uint8_t> EncodeWorldSnapshotRequest(uint64_t lastKnownTick);
        static bool DecodeWorldSnapshotRequest(const std::vector<uint8_t>& payload, uint64_t& outLastKnownTick);

        static std::vector<uint8_t> EncodeTypeTable(const std::vector<ComponentTypeDescriptor>& types);
        static bool DecodeTypeTable(const std::vector<uint8_t>& payload, std::vector<ComponentTypeDescriptor>& outTypes);

        static std::vector<uint8_t> EncodeWorldSnapshot(const WorldSnapshot& snapshot);
        static bool DecodeWorldSnapshot(const std::vector<uint8_t>& payload, WorldSnapshot& outSnapshot);

        static std::vector<uint8_t> EncodeInputCommand(const InputCommand& command);
        static bool DecodeInputCommand(const std::vector<uint8_t>& payload, InputCommand& outCommand);

        static std::vector<uint8_t> EncodeTickHash(const TickHash& tickHash);
        static bool DecodeTickHash(const std::vector<uint8_t>& payload, TickHash& outTickHash);
        static bool DecodeResyncRequest(
            const std::vector<uint8_t>& payload,
            uint64_t& outTick,
            uint64_t& outLocalHash,
            uint64_t& outRemoteHash);

    private:
        static Network::ReliabilityChannel::Delivery DeliveryClassFor(Network::PacketType type);

        bool SendPacket(Network::PacketType type, const std::vector<uint8_t>& payload, const Network::Endpoint& target);
        Network::ReliabilityChannel& ChannelFor(const Network::Endpoint& endpoint);
        bool FlushEndpoint(const Network::Endpoint& endpoint);
        void PumpConnection();

        std::shared_ptr<Network::INetworkConnection> m_connection;
        Network::Endpoint m_defaultTarget;
        uint32_t m_localClientId = 0;
        uint32_t m_nextClientId = 1;
        uint64_t m_currentTick = 0;
        std::unordered_map<uint32_t, NetworkClientState> m_clients;
        std::unordered_map<Network::Endpoint, uint32_t> m_endpointToClient;

        // endpoint별 신뢰성 상태 머신.
        std::unordered_map<Network::Endpoint, Network::ReliabilityChannel> m_channels;

        // 전달 준비된(순서/재조립까지 끝난) 패킷 큐.
        std::deque<ReceivedPacket> m_delivered;
    };

}
