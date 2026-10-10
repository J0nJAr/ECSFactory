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

    /**
     * 클라가 디싱크를 보고 서버에 보내는 복구 요청.
     *
     * `localBreakdown` 은 클라가 **불일치를 본 그 tick(`tick`)** 의 해시 분해표다. 서버는 자기가
     * 기록해 둔 같은 tick의 분해표와 풀 단위로 비교해, 어긋난 풀만 보낼지(부분 resync) 전체
     * 스냅샷을 보낼지 정한다(`SelectPoolsForPartialResync`).
     *
     * `localHash` 는 `localBreakdown.worldHash` 와 같다. 와이어에 따로 싣지 않는다 — 두 값이
     * 어긋난 요청을 검증해야 하는 부담만 생기기 때문이다.
     */
    struct ResyncRequest {
        uint64_t tick = 0;
        uint64_t remoteHash = 0;               // 클라가 받은 서버 TickHash 값(진단용)
        WorldHashBreakdown localBreakdown;     // tick 시점 클라의 분해표
    };

    struct NetworkClientState {
        uint32_t clientId = 0;
        EntityId controlledEntity = kNullEntity;
        bool hasControlledEntity = false;
        Network::Endpoint endpoint;      // 이 클라이언트의 전송 주소(라우팅용)
        // ⚠️ 이름과 달리 "언제 받았나"가 아니라 **마지막으로 받은 입력의 적용 예정 tick**이다
        //    (EnqueueInput이 command.tick을 넣는다). 타임아웃 판정에 쓰면 안 된다 —
        //    입력 지연 때문에 항상 현재보다 미래를 가리킨다.
        uint64_t lastReceivedTick = 0;

        // 이 클라이언트에게서 **마지막으로 패킷을 받은 서버 tick.** 연결 살아있음 판정용이다.
        // 패킷 종류를 가리지 않는다 — 입력이 없어도(가만히 서 있어도) ack는 오기 때문이다.
        uint64_t lastHeardFromTick = 0;
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

        /// @brief 클라이언트 등록을 해제한다. 조종하던 엔티티 id를 돌려준다(없으면 kNullEntity).
        ///
        /// 세 가지를 함께 지운다: 클라이언트 상태, endpoint→clientId 매핑, **그리고 신뢰성 채널.**
        ///
        /// ⚠️ 채널을 지우는 게 핵심이다. 서버는 매 tick 등록된 전원에게 InputFrame을
        ///    **reliable**로 보내는데, 떠난 클라의 채널이 남아 있으면 ack가 영영 안 와서
        ///    미확인 레코드가 tick당 하나씩 무한히 쌓이고 재전송도 계속 나간다.
        ///    메모리와 대역폭이 함께 새는 자리다.
        ///
        /// 월드에서 엔티티를 지우는 것은 **호출자의 몫**이다. NetworkManager는 월드를 모른다.
        EntityId UnregisterClient(uint32_t clientId);

        /// @brief 세션 이벤트를 특정 tick에 확정되도록 예약한다(서버 전용).
        ///
        /// 접속·해제는 **입력이 아니다.** 도착 시각이 peer마다 다르고 클라이언트는
        /// 다른 클라의 접속을 스스로 알 수 없다. 그래서 서버가 그 비결정론적 사건을
        /// tick 하나에 못 박고, 그 뒤로는 확정 입력과 똑같이 흐르게 한다.
        void QueueSessionEvent(uint64_t tick, const SessionEvent& event);

        /// @brief 확정된 Join으로 만들어진 엔티티를 이 clientId의 장부에 연결한다(서버 전용).
        ///
        /// 월드 상태가 아니라 서버만 쓰는 조회용이라 확정 경로 밖에서 갱신해도 결정론에
        /// 영향이 없다. 엔티티 자체는 ApplySessionEvents가 모든 peer에서 똑같이 만든다.
        void BindControlledEntity(uint32_t clientId, EntityId entity);

        /// @brief 그 tick에 확정된 세션 이벤트를 꺼낸다. **clientId 순으로 정렬되어 나온다.**
        ///
        /// ⚠️ 정렬이 여기 있는 이유: 이 순서가 곧 엔티티 생성 순서이고, 그게 EntityId를
        ///    결정한다. 순서가 peer마다 다르면 같은 사건에서 다른 id가 나와 즉시 갈라진다.
        ///    적용하는 쪽(ApplySessionEvents)에서 다시 정렬하지 않는다 — 두 곳에서 정렬하면
        ///    한쪽만 바뀌었을 때 조용히 어긋난다.
        [[nodiscard]] std::vector<SessionEvent> ConfirmSessionEvents(uint64_t tick);

        /// @brief 이 endpoint의 클라이언트가 살아 있다고 표시한다. 등록되지 않은 endpoint면 무시.
        ///
        /// 패킷 종류를 가리지 않고 부른다. 가만히 서 있는 클라이언트는 InputCommand를
        /// 보내지 않지만 ack는 계속 오므로, 입력만 보고 판정하면 멀쩡한 클라를 쫓아낸다.
        void MarkClientHeardFrom(const Network::Endpoint& endpoint, uint64_t tick);
        NetworkClientState& GetOrCreateClient(uint32_t clientId);
        [[nodiscard]] const NetworkClientState* FindClient(uint32_t clientId) const;
        /// @brief 클라이언트를 등록한다. `heardAtTick` 은 **반드시 현재 tick을 준다.**
        ///
        /// ⚠️ 기본값 0으로 두면 안 된다. 등록 직후 DropTimedOutClients 가 같은 tick에
        ///    돌면서 `tick >= 0 + 타임아웃` 을 만족해버려, **오래 떠 있던 서버에는
        ///    아무도 접속하지 못한다.** 실제로 이 버그를 냈다 — 서버를 매번 새로 띄워
        ///    테스트해서 5초 미만에서만 확인했고, 교차검증에서 잡혔다.
        NetworkClientState& RegisterClient(uint32_t clientId,
                                           EntityId controlledEntity = kNullEntity,
                                           const Network::Endpoint& endpoint = {},
                                           uint64_t heardAtTick = 0);
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
        bool SendResyncRequest(const ResyncRequest& request, const Network::Endpoint& target = {});
        bool SendPartialSnapshot(const PartialSnapshot& snapshot, const Network::Endpoint& target = {});

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
        static std::vector<uint8_t> EncodeResyncRequest(const ResyncRequest& request);
        static bool DecodeResyncRequest(const std::vector<uint8_t>& payload, ResyncRequest& outRequest);

        static std::vector<uint8_t> EncodePartialSnapshot(const PartialSnapshot& snapshot);
        static bool DecodePartialSnapshot(const std::vector<uint8_t>& payload, PartialSnapshot& outSnapshot);

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

        // tick → 그 tick에 확정될 세션 이벤트들. 서버에서만 채워진다.
        std::map<uint64_t, std::vector<SessionEvent>> m_pendingSessionEvents;

        // endpoint별 신뢰성 상태 머신.
        std::unordered_map<Network::Endpoint, Network::ReliabilityChannel> m_channels;

        // 전달 준비된(순서/재조립까지 끝난) 패킷 큐.
        std::deque<ReceivedPacket> m_delivered;
    };

}
