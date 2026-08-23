#include "NetworkManager.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <utility>

#include "Network/NetUtils.hpp"

namespace EF {
    namespace {
        using Writer = Network::PacketWriter;
        using Reader = Network::PacketReader;

        // 와이어에서 읽은 개수로 곧장 reserve 하면 안 된다.
        //
        // 조작된(혹은 손상된) 4바이트 길이 접두 하나로 수 GB 예약을 유도할 수 있고,
        // 그 자리에서 bad_alloc으로 프로세스가 죽는다. 실제 데이터가 뒤따르는지는
        // 루프가 읽어 보기 전까지 알 수 없으므로, 예약만 남은 페이로드가 감당할 수 있는
        // 최대치로 자른다. 개수가 진짜라면 push_back이 알아서 더 늘린다.
        //
        // 어떤 항목도 4바이트(u32 하나)보다 작을 수 없으므로 Remaining()/4가 안전한 상한이다.
        // 항목 크기를 하드코딩하지 않는 이유: 와이어 포맷이 바뀌어도 이 상한은 계속 유효하다.
        [[nodiscard]] std::size_t SafeReserveCount(uint32_t count, const Reader& reader) {
            return std::min<std::size_t>(count, reader.Remaining() / 4);
        }

        void WriteType(Writer& writer, const ComponentTypeDescriptor& type) {
            writer.U64(type.hash);
            writer.U64(static_cast<uint64_t>(type.size));
            writer.U64(static_cast<uint64_t>(type.alignment));
            writer.Bool(type.isTrivial);
        }

        bool ReadType(Reader& reader, ComponentTypeDescriptor& type) {
            uint64_t size = 0;
            uint64_t alignment = 0;
            if (!reader.U64(type.hash) || !reader.U64(size) || !reader.U64(alignment) || !reader.Bool(type.isTrivial)) {
                return false;
            }
            type.size = static_cast<std::size_t>(size);
            type.alignment = static_cast<std::size_t>(alignment);
            return true;
        }

        void WriteInputCommand(Writer& writer, const InputCommand& command) {
            writer.U32(command.clientId);
            writer.U64(command.tick);
            writer.U32(command.sequence);
            writer.U64(command.buttonMask);
            writer.WriteFixed64(command.aimYaw);
            writer.WriteFixed64(command.aimPitch);
        }

        bool ReadInputCommand(Reader& reader, InputCommand& command) {
            return reader.U32(command.clientId) &&
                   reader.U64(command.tick) &&
                   reader.U32(command.sequence) &&
                   reader.U64(command.buttonMask) &&
                   reader.ReadFixed64(command.aimYaw) &&
                   reader.ReadFixed64(command.aimPitch);
        }
    }

    NetworkManager::NetworkManager(std::shared_ptr<Network::INetworkConnection> connection)
        : m_connection(std::move(connection)) {
        if (m_connection) {
            m_defaultTarget = m_connection->DefaultPeer();
        }
    }

    uint32_t NetworkManager::AllocateClientId() {
        while (m_nextClientId == 0 || m_clients.find(m_nextClientId) != m_clients.end()) {
            ++m_nextClientId;
        }

        const uint32_t clientId = m_nextClientId;
        ++m_nextClientId;
        return clientId;
    }

    NetworkClientState& NetworkManager::GetOrCreateClient(uint32_t clientId) {
        NetworkClientState& state = m_clients[clientId];
        state.clientId = clientId;
        return state;
    }

    const NetworkClientState* NetworkManager::FindClient(uint32_t clientId) const {
        auto it = m_clients.find(clientId);
        return it == m_clients.end() ? nullptr : &it->second;
    }

    NetworkClientState& NetworkManager::RegisterClient(uint32_t clientId, EntityId controlledEntity,
                                                       const Network::Endpoint& endpoint) {
        NetworkClientState& state = GetOrCreateClient(clientId);
        if (controlledEntity != kNullEntity) {
            state.controlledEntity = controlledEntity;
            state.hasControlledEntity = true;
        }
        if (endpoint.IsValid()) {
            state.endpoint = endpoint;
            m_endpointToClient[endpoint] = clientId;
        }
        return state;
    }

    uint32_t NetworkManager::ClientIdForEndpoint(const Network::Endpoint& endpoint) const {
        auto it = m_endpointToClient.find(endpoint);
        return it == m_endpointToClient.end() ? 0 : it->second;
    }

    void NetworkManager::MarkClientHeardFrom(const Network::Endpoint& endpoint, uint64_t tick) {
        const uint32_t clientId = ClientIdForEndpoint(endpoint);
        if (clientId == 0) {
            return;
        }
        auto it = m_clients.find(clientId);
        if (it != m_clients.end()) {
            it->second.lastHeardFromTick = tick;
        }
    }

    void NetworkManager::QueueSessionEvent(uint64_t tick, const SessionEvent& event) {
        m_pendingSessionEvents[tick].push_back(event);
    }

    void NetworkManager::BindControlledEntity(uint32_t clientId, EntityId entity) {
        auto it = m_clients.find(clientId);
        if (it == m_clients.end()) {
            return;
        }
        it->second.controlledEntity = entity;
        it->second.hasControlledEntity = (entity != kNullEntity);
    }

    std::vector<SessionEvent> NetworkManager::ConfirmSessionEvents(uint64_t tick) {
        auto it = m_pendingSessionEvents.find(tick);
        if (it == m_pendingSessionEvents.end()) {
            return {};
        }

        std::vector<SessionEvent> events = std::move(it->second);
        m_pendingSessionEvents.erase(it);

        // clientId 순으로 정렬한다. 이 순서가 곧 엔티티 생성 순서이고 그게 EntityId를 정한다.
        // 같은 clientId에 Join과 Leave가 같은 tick에 겹치는 일은 없어야 하지만,
        // 있더라도 순서가 확정되도록 kind를 두 번째 키로 쓴다.
        std::sort(events.begin(), events.end(), [](const SessionEvent& a, const SessionEvent& b) {
            if (a.clientId != b.clientId) {
                return a.clientId < b.clientId;
            }
            return static_cast<uint8_t>(a.kind) < static_cast<uint8_t>(b.kind);
        });
        return events;
    }

    EntityId NetworkManager::UnregisterClient(uint32_t clientId) {
        auto it = m_clients.find(clientId);
        if (it == m_clients.end()) {
            return kNullEntity;
        }

        const NetworkClientState& state = it->second;
        const EntityId controlled = state.hasControlledEntity ? state.controlledEntity : kNullEntity;

        if (state.endpoint.IsValid()) {
            m_endpointToClient.erase(state.endpoint);

            // 신뢰성 채널까지 지운다. 안 지우면 미확인 레코드가 무한히 쌓인다(헤더 주석 참조).
            m_channels.erase(state.endpoint);
        }

        m_clients.erase(it);
        return controlled;
    }

    bool NetworkManager::EnqueueInput(const InputCommand& command) {
        if (command.clientId == 0) {
            return false;
        }

        NetworkClientState& state = GetOrCreateClient(command.clientId);

        // client/tick 슬롯으로 중복을 거르고, 서버가 이미 확정한 tick은 거부한다.
        // (확정된 과거를 뒤늦게 바꾸면 서버와 클라의 시뮬레이션이 갈라진다.)
        if (command.tick <= state.lastConfirmedTick) {
            return false;
        }

        state.lastReceivedTick = command.tick;
        return state.inputByTick.try_emplace(command.tick, command).second;
    }

    std::vector<InputCommand> NetworkManager::ConfirmInputs(uint64_t tick) {
        std::vector<uint32_t> clientIds;
        clientIds.reserve(m_clients.size());
        for (const auto& [clientId, state] : m_clients) {
            static_cast<void>(state);
            clientIds.push_back(clientId);
        }
        std::sort(clientIds.begin(), clientIds.end());

        std::vector<InputCommand> confirmed;
        confirmed.reserve(clientIds.size());
        for (uint32_t clientId : clientIds) {
            NetworkClientState& state = m_clients[clientId];

            InputCommand command;
            auto inputIt = state.inputByTick.find(tick);
            if (inputIt != state.inputByTick.end()) {
                command = inputIt->second;
            } else if (state.hasConfirmed) {
                command = state.lastConfirmed;
            }
            command.clientId = clientId;
            command.tick = tick;

            state.lastConfirmed = command;
            state.hasConfirmed = true;
            state.lastConfirmedTick = tick;

            auto eraseEnd = state.inputByTick.upper_bound(tick);
            state.inputByTick.erase(state.inputByTick.begin(), eraseEnd);

            confirmed.push_back(command);
        }

        return confirmed;
    }

    Network::ReliabilityChannel::Delivery NetworkManager::DeliveryClassFor(Network::PacketType type) {
        using Network::PacketType;
        using Delivery = Network::ReliabilityChannel::Delivery;
        switch (type) {
        case PacketType::TickHash:
            return Delivery::Sequenced;    // 최신만 의미 있음
        case PacketType::InputCommand:
            return Delivery::Unreliable;   // 손실은 다음 tick이 덮음, 앱 계층 dedup
        case PacketType::InputFrame:
            return Delivery::Reliable;
        default:
            return Delivery::Reliable;     // 제어/스냅샷은 반드시 도착
        }
    }

    Network::ReliabilityChannel& NetworkManager::ChannelFor(const Network::Endpoint& endpoint) {
        return m_channels[endpoint];
    }

    bool NetworkManager::SendHello(const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::Hello, EncodeHello(), target);
    }

    bool NetworkManager::SendWelcome(uint32_t assignedClientId, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::Welcome, EncodeWelcome(assignedClientId), target);
    }

    bool NetworkManager::SendTypeTable(const std::vector<ComponentTypeDescriptor>& types, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::TypeTable, EncodeTypeTable(types), target);
    }

    bool NetworkManager::SendWorldSnapshotRequest(uint64_t lastKnownTick, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::WorldSnapshotRequest, EncodeWorldSnapshotRequest(lastKnownTick), target);
    }

    bool NetworkManager::SendWorldSnapshot(const WorldSnapshot& snapshot, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::WorldSnapshot, EncodeWorldSnapshot(snapshot), target);
    }

    bool NetworkManager::SendInputCommand(const InputCommand& command, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::InputCommand, EncodeInputCommand(command), target);
    }

    bool NetworkManager::SendInputFrame(const InputFrame& frame, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::InputFrame, EncodeInputFrame(frame), target);
    }

    bool NetworkManager::SendTickHash(const TickHash& tickHash, const Network::Endpoint& target) {
        return SendPacket(Network::PacketType::TickHash, EncodeTickHash(tickHash), target);
    }

    bool NetworkManager::SendResyncRequest(uint64_t tick, uint64_t localHash, uint64_t remoteHash,
                                           const Network::Endpoint& target) {
        Writer writer;
        writer.U64(tick);
        writer.U64(localHash);
        writer.U64(remoteHash);
        return SendPacket(Network::PacketType::ResyncRequest, writer.Take(), target);
    }

    bool NetworkManager::SendPacket(Network::PacketType type, const std::vector<uint8_t>& payload,
                                    const Network::Endpoint& target) {
        if (!m_connection || payload.size() > std::numeric_limits<uint16_t>::max()) {
            return false;
        }

        const Network::Endpoint destination = target.IsValid() ? target : m_defaultTarget;
        if (!destination.IsValid()) {
            return false;
        }

        // 앱 레코드 = [type][payload]. 신뢰성 계층은 이걸 불투명 바이트로 다룬다.
        std::vector<uint8_t> record;
        record.reserve(payload.size() + 1);
        record.push_back(static_cast<uint8_t>(type));
        record.insert(record.end(), payload.begin(), payload.end());

        ChannelFor(destination).Enqueue(DeliveryClassFor(type), record);
        return FlushEndpoint(destination);
    }

    bool NetworkManager::FlushEndpoint(const Network::Endpoint& endpoint) {
        if (!m_connection) {
            return false;
        }

        std::vector<std::vector<uint8_t>> datagrams = ChannelFor(endpoint).Flush(m_currentTick);
        bool allSent = true;
        for (const std::vector<uint8_t>& datagram : datagrams) {
            if (!m_connection->SendTo(endpoint, datagram)) {
                allSent = false;
            }
        }
        return allSent;
    }

    void NetworkManager::Update(uint64_t tick) {
        m_currentTick = tick;
        if (!m_connection) {
            return;
        }

        // 모든 endpoint 채널의 재전송/누적 ack를 흘려보낸다.
        for (auto& [endpoint, channel] : m_channels) {
            std::vector<std::vector<uint8_t>> datagrams = channel.Flush(tick);
            for (const std::vector<uint8_t>& datagram : datagrams) {
                m_connection->SendTo(endpoint, datagram);
            }
        }
    }

    void NetworkManager::PumpConnection() {
        if (!m_connection) {
            return;
        }

        while (std::optional<Network::Datagram> datagram = m_connection->ReceiveFrom()) {
            // ⚠️ 생존 판정은 **여기서** 한다. 앱 패킷이 올라오는 자리(TryReceive)에서 하면 안 된다.
            //
            //    조용한 클라이언트는 앱 패킷을 전혀 보내지 않는다 — 키를 안 누르면
            //    InputCommand가 없다. 그래도 서버가 매 tick 보내는 reliable InputFrame에
            //    **ack는 계속 보낸다.** 그런데 ack만 든 데이터그램에는 앱 레코드가 없어서
            //    TryReceive로 올라오지 않는다. 그래서 앱 계층에서 생존을 재면 가만히 서 있는
            //    플레이어가 타임아웃으로 쫓겨난다(실제로 이 회귀를 냈다).
            //
            //    m_currentTick은 Update(tick)에서 갱신되므로 여기서는 한 tick 낡을 수 있다.
            //    타임아웃이 250 tick 단위라 무해하다.
            MarkClientHeardFrom(datagram->from, m_currentTick);

            std::vector<std::vector<uint8_t>> delivered;
            ChannelFor(datagram->from).OnReceive(datagram->bytes, delivered);

            for (std::vector<uint8_t>& record : delivered) {
                if (record.empty()) {
                    continue;
                }
                Network::NetworkPacket packet;
                packet.type = static_cast<Network::PacketType>(record[0]);
                packet.payload.assign(record.begin() + 1, record.end());
                m_delivered.push_back(ReceivedPacket{
                    .from = datagram->from,
                    .packet = std::move(packet)
                });
            }
        }
    }

    std::optional<ReceivedPacket> NetworkManager::TryReceive() {
        if (m_delivered.empty()) {
            PumpConnection();
        }
        if (m_delivered.empty()) {
            return std::nullopt;
        }

        ReceivedPacket received = std::move(m_delivered.front());
        m_delivered.pop_front();
        return received;
    }

    std::optional<Network::NetworkPacket> NetworkManager::TryReceivePacket() {
        std::optional<ReceivedPacket> received = TryReceive();
        if (!received) {
            return std::nullopt;
        }
        return std::move(received->packet);
    }

    std::vector<uint8_t> NetworkManager::EncodeHello() {
        Writer writer;
        writer.U32(0);
        return writer.Take();
    }

    bool NetworkManager::DecodeHello(const std::vector<uint8_t>& payload) {
        uint32_t reserved = 0;
        Reader reader(payload);
        return reader.U32(reserved) && reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeWelcome(uint32_t assignedClientId) {
        Writer writer;
        writer.U32(assignedClientId);
        return writer.Take();
    }

    bool NetworkManager::DecodeWelcome(const std::vector<uint8_t>& payload, uint32_t& outAssignedClientId) {
        Reader reader(payload);
        return reader.U32(outAssignedClientId) && reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeWorldSnapshotRequest(uint64_t lastKnownTick) {
        Writer writer;
        writer.U64(lastKnownTick);
        return writer.Take();
    }

    bool NetworkManager::DecodeWorldSnapshotRequest(const std::vector<uint8_t>& payload, uint64_t& outLastKnownTick) {
        Reader reader(payload);
        return reader.U64(outLastKnownTick) && reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeTypeTable(const std::vector<ComponentTypeDescriptor>& types) {
        Writer writer;
        writer.U32(static_cast<uint32_t>(types.size()));
        for (const ComponentTypeDescriptor& type : types) {
            WriteType(writer, type);
        }
        return writer.Take();
    }

    bool NetworkManager::DecodeTypeTable(const std::vector<uint8_t>& payload, std::vector<ComponentTypeDescriptor>& outTypes) {
        Reader reader(payload);
        uint32_t count = 0;
        if (!reader.U32(count)) {
            return false;
        }

        outTypes.clear();
        outTypes.reserve(SafeReserveCount(count, reader));
        for (uint32_t i = 0; i < count; ++i) {
            ComponentTypeDescriptor type;
            if (!ReadType(reader, type)) {
                return false;
            }
            outTypes.push_back(type);
        }
        return reader.Finished();
    }

    /**
     * WorldSnapshot 와이어 포맷 (프로토콜 v2).
     *
     *   u64  tick
     *   u32  nextEntityIndex          ┐
     *   u32  versionCount             ├ EntitySnapshot (v2에서 추가)
     *   u32  version   × versionCount │
     *   u32  freeCount                │
     *   u32  freeIndex × freeCount    ┘
     *   u32  typeCount ;   type × typeCount
     *   u32  componentCount ; (type, u32 entityCount, u32 entity ×, u32 byteCount, bytes) ×
     *
     * `versionCount`는 `nextEntityIndex`와 항상 같지만(EntitySnapshot 불변식) 길이 접두는
     * 그대로 붙인다 — 다른 배열과 형태를 맞추고, 디코더가 불변식을 가정하지 않게 하기 위해서다.
     * 불일치 검사는 `EntityManager::RestoreFrom`이 한다.
     */
    std::vector<uint8_t> NetworkManager::EncodeWorldSnapshot(const WorldSnapshot& snapshot) {
        Writer writer;
        writer.U64(snapshot.tick);

        writer.U32(snapshot.entities.nextEntityIndex);
        writer.U32(static_cast<uint32_t>(snapshot.entities.versions.size()));
        for (EntityVersion version : snapshot.entities.versions) {
            writer.U32(version);
        }
        writer.U32(static_cast<uint32_t>(snapshot.entities.freeIndices.size()));
        for (EntityIndex freeIndex : snapshot.entities.freeIndices) {
            writer.U32(freeIndex);
        }

        writer.U32(static_cast<uint32_t>(snapshot.typeTable.size()));
        for (const ComponentTypeDescriptor& type : snapshot.typeTable) {
            WriteType(writer, type);
        }

        writer.U32(static_cast<uint32_t>(snapshot.components.size()));
        for (const ComponentSnapshot& component : snapshot.components) {
            WriteType(writer, component.type);
            writer.U32(static_cast<uint32_t>(component.entities.size()));
            for (EntityIndex entity : component.entities) {
                writer.U32(entity);
            }
            writer.U32(static_cast<uint32_t>(component.bytes.size()));
            writer.Bytes(component.bytes.data(), component.bytes.size());
        }

        return writer.Take();
    }

    bool NetworkManager::DecodeWorldSnapshot(const std::vector<uint8_t>& payload, WorldSnapshot& outSnapshot) {
        Reader reader(payload);
        uint32_t typeCount = 0;
        uint32_t componentCount = 0;
        if (!reader.U64(outSnapshot.tick) || !reader.U32(outSnapshot.entities.nextEntityIndex)) {
            return false;
        }

        // 길이 접두를 믿고 reserve 하지 않는다 — 4바이트 카운트 하나로 수 GB를 예약하게
        // 만들 수 있기 때문이다. push_back으로 늘리면 페이로드가 먼저 바닥나서 false가 난다.
        uint32_t versionCount = 0;
        if (!reader.U32(versionCount)) {
            return false;
        }
        outSnapshot.entities.versions.clear();
        for (uint32_t i = 0; i < versionCount; ++i) {
            uint32_t version = 0;
            if (!reader.U32(version)) {
                return false;
            }
            outSnapshot.entities.versions.push_back(version);
        }

        uint32_t freeCount = 0;
        if (!reader.U32(freeCount)) {
            return false;
        }
        outSnapshot.entities.freeIndices.clear();
        for (uint32_t i = 0; i < freeCount; ++i) {
            uint32_t freeIndex = 0;
            if (!reader.U32(freeIndex)) {
                return false;
            }
            outSnapshot.entities.freeIndices.push_back(freeIndex);
        }

        if (!reader.U32(typeCount)) {
            return false;
        }

        outSnapshot.typeTable.clear();
        outSnapshot.typeTable.reserve(SafeReserveCount(typeCount, reader));
        for (uint32_t i = 0; i < typeCount; ++i) {
            ComponentTypeDescriptor type;
            if (!ReadType(reader, type)) {
                return false;
            }
            outSnapshot.typeTable.push_back(type);
        }

        if (!reader.U32(componentCount)) {
            return false;
        }

        outSnapshot.components.clear();
        outSnapshot.components.reserve(SafeReserveCount(componentCount, reader));
        for (uint32_t i = 0; i < componentCount; ++i) {
            ComponentSnapshot component;
            uint32_t entityCount = 0;
            uint32_t byteCount = 0;
            if (!ReadType(reader, component.type) || !reader.U32(entityCount)) {
                return false;
            }

            component.entities.reserve(SafeReserveCount(entityCount, reader));
            for (uint32_t entityIndex = 0; entityIndex < entityCount; ++entityIndex) {
                uint32_t entity = 0;
                if (!reader.U32(entity)) {
                    return false;
                }
                component.entities.push_back(entity);
            }

            if (!reader.U32(byteCount) || !reader.Bytes(component.bytes, byteCount)) {
                return false;
            }
            outSnapshot.components.push_back(std::move(component));
        }

        return reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeInputCommand(const InputCommand& command) {
        Writer writer;
        WriteInputCommand(writer, command);
        return writer.Take();
    }

    bool NetworkManager::DecodeInputCommand(const std::vector<uint8_t>& payload, InputCommand& outCommand) {
        Reader reader(payload);
        return ReadInputCommand(reader, outCommand) && reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeInputFrame(const InputFrame& frame) {
        Writer writer;
        writer.U64(frame.tick);

        // ⚠️ 세션 이벤트가 입력보다 **먼저** 나간다. 읽는 쪽도 같은 순서여야 한다.
        //    적용 순서(events → inputs)와 맞춰 둔 것이라, 한쪽만 바꾸면 조용히 어긋난다.
        writer.U32(static_cast<uint32_t>(frame.events.size()));
        for (const SessionEvent& event : frame.events) {
            writer.U8(static_cast<uint8_t>(event.kind));
            writer.U32(event.clientId);
        }

        writer.U32(static_cast<uint32_t>(frame.inputs.size()));
        for (const InputCommand& command : frame.inputs) {
            WriteInputCommand(writer, command);
        }
        return writer.Take();
    }

    bool NetworkManager::DecodeInputFrame(const std::vector<uint8_t>& payload, InputFrame& outFrame) {
        Reader reader(payload);
        uint32_t eventCount = 0;
        if (!reader.U64(outFrame.tick) || !reader.U32(eventCount)) {
            return false;
        }

        // 세션 이벤트가 먼저다(EncodeInputFrame과 같은 순서).
        outFrame.events.clear();
        outFrame.events.reserve(SafeReserveCount(eventCount, reader));
        for (uint32_t i = 0; i < eventCount; ++i) {
            uint8_t kind = 0;
            SessionEvent event;
            if (!reader.U8(kind) || !reader.U32(event.clientId)) {
                return false;
            }

            // 모르는 kind는 프레임 전체를 거부한다. 조용히 무시하면 이 peer만
            // 그 사건을 놓친 채 계속 돌아가고, 그게 곧 디싱크다.
            if (kind > static_cast<uint8_t>(SessionEvent::Kind::Leave)) {
                return false;
            }
            event.kind = static_cast<SessionEvent::Kind>(kind);
            outFrame.events.push_back(event);
        }

        uint32_t count = 0;
        if (!reader.U32(count)) {
            return false;
        }

        outFrame.inputs.clear();
        outFrame.inputs.reserve(SafeReserveCount(count, reader));
        for (uint32_t i = 0; i < count; ++i) {
            InputCommand command;
            if (!ReadInputCommand(reader, command)) {
                return false;
            }
            outFrame.inputs.push_back(command);
        }
        return reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeTickHash(const TickHash& tickHash) {
        Writer writer;
        writer.U64(tickHash.tick);
        writer.U64(tickHash.worldHash);
        return writer.Take();
    }

    bool NetworkManager::DecodeTickHash(const std::vector<uint8_t>& payload, TickHash& outTickHash) {
        Reader reader(payload);
        return reader.U64(outTickHash.tick) &&
               reader.U64(outTickHash.worldHash) &&
               reader.Finished();
    }

    bool NetworkManager::DecodeResyncRequest(
        const std::vector<uint8_t>& payload,
        uint64_t& outTick,
        uint64_t& outLocalHash,
        uint64_t& outRemoteHash) {
        Reader reader(payload);
        return reader.U64(outTick) &&
               reader.U64(outLocalHash) &&
               reader.U64(outRemoteHash) &&
               reader.Finished();
    }
}
