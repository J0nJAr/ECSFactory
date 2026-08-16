#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "ECS/World.hpp"
#include "Math/Fixed64.hpp"
#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"
#include "Network/InputCommand.hpp"
#include "Network/Packet.hpp"
#include "Network/ReliabilityChannel.hpp"

namespace EF {

    struct NetworkClientState {
        uint32_t clientId = 0;
        EntityId controlledEntity = kNullEntity;
        bool hasControlledEntity = false;
        Network::Endpoint endpoint;      // 이 클라이언트의 전송 주소(라우팅용)
        uint64_t lastReceivedTick = 0;
        bool hasTypeTable = false;
        bool hasSnapshot = false;
        std::vector<ComponentTypeDescriptor> remoteTypes;
        std::map<uint64_t, InputCommand> inputByTick;

        // 서버가 확정한 입력 상태. inputByTick에는 아직 확정되지 않은 슬롯만 남는다.
        // 결측 tick을 직전 입력으로 대체(hold)할 때 이 값을 쓴다.
        InputCommand lastConfirmed;
        bool hasConfirmed = false;
        uint64_t lastConfirmedTick = 0;
    };

    // 수신된 패킷 하나 + 보낸 사람 endpoint.
    struct ReceivedPacket {
        Network::Endpoint from;
        Network::NetworkPacket packet;
    };

    // 프로토콜 계층. 월드 리소스로 붙는다(AddResource<NetworkManager>).
    //
    // 책임:
    //  - 패킷 인코딩/디코딩과 전달 등급 배정(DeliveryClassFor)
    //  - endpoint ↔ clientId 라우팅 테이블. 인가는 페이로드의 clientId가 아니라
    //    endpoint로 한다(ClientIdForEndpoint) — 스푸핑 방지
    //  - endpoint별 ReliabilityChannel 소유
    //  - 서버 쪽 클라이언트별 입력 슬롯 관리와 tick 확정(ConfirmInputs)
    //
    // 보장:
    //  - ConfirmInputs는 clientId로 정렬된 확정셋을 돌려준다. 결측 입력은 그 클라이언트의
    //    직전 확정 입력으로 채운다(hold). 서버가 대체값을 만들어 전파하므로 어떤 peer도
    //    임의로 판단하지 않는다.
    //  - TryReceive는 순서·재조립이 끝난 패킷만 돌려준다.
    //
    // 이 타입이 하지 않는 일:
    //  - 소켓을 직접 만지지 않는다(INetworkConnection 뒤에 있다).
    //  - 시뮬레이션을 돌리지 않는다. 언제 무엇을 보낼지는 NetworkSyncSystem이 정한다.
    //  - 재전송을 스스로 굴리지 않는다.
    //
    // ⚠️ 매 fixed tick마다 Update(tick)을 불러야 한다. SendX는 즉시 flush하지만
    //    재전송과 누적 ack를 굴리는 건 Update다. 이 펌프가 멈추면 신뢰성 트래픽이
    //    손실에서 영영 복구되지 않는다.
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

        // 미래 tick 입력을 그 클라이언트의 빈 슬롯에 넣는다.
        // 이미 채워졌거나 확정이 끝난 tick이면 false — 이중화 재전송이 자연히 걸러진다.
        bool EnqueueInput(const InputCommand& command);
        std::vector<InputCommand> ConfirmInputs(uint64_t tick);

        // 송신. target을 비워두면 연결의 기본 상대(DefaultTarget)로 보낸다.
        bool SendHello(const Network::Endpoint& target = {});
        bool SendWelcome(uint32_t assignedClientId, const Network::Endpoint& target = {});
        bool SendTypeTable(const std::vector<ComponentTypeDescriptor>& types, const Network::Endpoint& target = {});
        bool SendWorldSnapshotRequest(uint64_t lastKnownTick, const Network::Endpoint& target = {});
        bool SendWorldSnapshot(const WorldSnapshot& snapshot, const Network::Endpoint& target = {});
        bool SendInputCommand(const InputCommand& command, const Network::Endpoint& target = {});
        bool SendInputFrame(const InputFrame& frame, const Network::Endpoint& target = {});
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
        static std::vector<uint8_t> EncodeInputFrame(const InputFrame& frame);
        static bool DecodeInputFrame(const std::vector<uint8_t>& payload, InputFrame& outFrame);

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
