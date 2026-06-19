#include "NetworkManager.hpp"

#include <limits>
#include <span>

#include "Network/NetUtils.hpp"

namespace EF {
    namespace {
        using Writer = Network::PacketWriter;
        using Reader = Network::PacketReader;

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
    }

    NetworkManager::NetworkManager(std::shared_ptr<Network::INetworkConnection> connection)
        : m_connection(std::move(connection)) {
    }

    NetworkClientState& NetworkManager::GetOrCreateClient(uint32_t clientId) {
        NetworkClientState& state = m_clients[clientId];
        state.clientId = clientId;
        return state;
    }

    bool NetworkManager::EnqueueInput(const InputCommand& command) {
        NetworkClientState& state = GetOrCreateClient(command.clientId);

        // 시퀀스 기반 중복/구식 입력 폐기.
        // 부호 있는 차이로 비교해 32비트 wraparound도 안전하게 처리한다.
        if (state.hasInput &&
            static_cast<int32_t>(command.sequence - state.lastInputSequence) <= 0) {
            return false;
        }

        state.hasInput = true;
        state.lastInputSequence = command.sequence;
        state.lastReceivedTick = command.tick;
        state.pendingInputs.push_back(command);
        return true;
    }

    const std::deque<InputCommand>& NetworkManager::PendingInputs(uint32_t clientId) const {
        static const std::deque<InputCommand> kEmpty;
        auto it = m_clients.find(clientId);
        return it == m_clients.end() ? kEmpty : it->second.pendingInputs;
    }

    bool NetworkManager::SendHello(uint32_t requestedClientId) {
        Writer writer;
        writer.U32(requestedClientId);
        return SendPacket(Network::PacketType::Hello, writer.Take());
    }

    bool NetworkManager::SendTypeTable(const std::vector<ComponentTypeDescriptor>& types) {
        return SendPacket(Network::PacketType::TypeTable, EncodeTypeTable(types));
    }

    bool NetworkManager::SendWorldSnapshotRequest(uint64_t lastKnownTick) {
        Writer writer;
        writer.U64(lastKnownTick);
        return SendPacket(Network::PacketType::WorldSnapshotRequest, writer.Take());
    }

    bool NetworkManager::SendWorldSnapshot(const WorldSnapshot& snapshot) {
        return SendPacket(Network::PacketType::WorldSnapshot, EncodeWorldSnapshot(snapshot));
    }

    bool NetworkManager::SendInputCommand(const InputCommand& command) {
        return SendPacket(Network::PacketType::InputCommand, EncodeInputCommand(command));
    }

    bool NetworkManager::SendTickHash(const TickHash& tickHash) {
        return SendPacket(Network::PacketType::TickHash, EncodeTickHash(tickHash));
    }

    bool NetworkManager::SendResyncRequest(uint64_t tick, uint64_t localHash, uint64_t remoteHash) {
        Writer writer;
        writer.U64(tick);
        writer.U64(localHash);
        writer.U64(remoteHash);
        return SendPacket(Network::PacketType::ResyncRequest, writer.Take());
    }

    std::optional<Network::NetworkPacket> NetworkManager::TryReceivePacket() {
        if (!m_connection) {
            return std::nullopt;
        }

        while (true) {
            // 현재 프레임을 다 소비했으면 다음 프레임(메시지)을 통로에서 가져온다.
            if (m_frameOffset >= m_currentFrame.size()) {
                std::optional<std::vector<uint8_t>> frame = m_connection->Receive();
                if (!frame) {
                    return std::nullopt;
                }

                m_currentFrame = std::move(*frame);
                m_frameOffset = 0;

                // 프레임 맨 앞 1바이트 = 프로토콜 버전.
                if (m_currentFrame.empty() || m_currentFrame[0] != Network::kProtocolVersion) {
                    m_currentFrame.clear();
                    m_frameOffset = 0;
                    continue; // 우리 프레임이 아니거나 손상 → 버리고 다음 프레임
                }
                m_frameOffset = 1;
            }

            // 레코드 헤더 = [type(1)][size(4)]. 프레임 안에서 끝까지 반복해 꺼낸다.
            Reader reader(std::span<const uint8_t>(m_currentFrame).subspan(m_frameOffset));
            uint8_t rawType = 0;
            uint32_t payloadSize = 0;
            if (!reader.U8(rawType) || !reader.U32(payloadSize)) {
                m_frameOffset = m_currentFrame.size(); // 손상 → 프레임 폐기
                continue;
            }

            const std::size_t payloadStart = m_frameOffset + 5;
            if (payloadStart + payloadSize > m_currentFrame.size()) {
                m_frameOffset = m_currentFrame.size(); // 길이 모순 → 프레임 폐기
                continue;
            }

            Network::NetworkPacket packet;
            packet.type = static_cast<Network::PacketType>(rawType);
            packet.payload.assign(
                m_currentFrame.begin() + static_cast<std::ptrdiff_t>(payloadStart),
                m_currentFrame.begin() + static_cast<std::ptrdiff_t>(payloadStart + payloadSize));
            m_frameOffset = payloadStart + payloadSize;
            return packet;
        }
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
        outTypes.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            ComponentTypeDescriptor type;
            if (!ReadType(reader, type)) {
                return false;
            }
            outTypes.push_back(type);
        }
        return reader.Finished();
    }

    std::vector<uint8_t> NetworkManager::EncodeWorldSnapshot(const WorldSnapshot& snapshot) {
        Writer writer;
        writer.U64(snapshot.tick);

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
        if (!reader.U64(outSnapshot.tick) || !reader.U32(typeCount)) {
            return false;
        }

        outSnapshot.typeTable.clear();
        outSnapshot.typeTable.reserve(typeCount);
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
        outSnapshot.components.reserve(componentCount);
        for (uint32_t i = 0; i < componentCount; ++i) {
            ComponentSnapshot component;
            uint32_t entityCount = 0;
            uint32_t byteCount = 0;
            if (!ReadType(reader, component.type) || !reader.U32(entityCount)) {
                return false;
            }

            component.entities.reserve(entityCount);
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
        writer.U32(command.clientId);
        writer.U64(command.tick);
        writer.U32(command.sequence);
        writer.U64(command.buttonMask);
        writer.WriteFixed64(command.aimYaw);
        writer.WriteFixed64(command.aimPitch);
        return writer.Take();
    }

    bool NetworkManager::DecodeInputCommand(const std::vector<uint8_t>& payload, InputCommand& outCommand) {
        Reader reader(payload);
        return reader.U32(outCommand.clientId) &&
               reader.U64(outCommand.tick) &&
               reader.U32(outCommand.sequence) &&
               reader.U64(outCommand.buttonMask) &&
               reader.ReadFixed64(outCommand.aimYaw) &&
               reader.ReadFixed64(outCommand.aimPitch) &&
               reader.Finished();
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

    bool NetworkManager::SendPacket(Network::PacketType type, const std::vector<uint8_t>& payload) {
        if (!m_connection || payload.size() > std::numeric_limits<uint32_t>::max()) {
            return false;
        }

        // 프레임 = [version][type][size][payload]
        // 지금은 한 프레임에 패킷 하나(N=1)지만, 수신은 프레임 안의 레코드를
        // 끝까지 읽으므로 나중에 틱 단위 배칭을 켜도 와이어 포맷은 그대로다.
        Writer writer;
        writer.U8(Network::kProtocolVersion);
        writer.U8(static_cast<uint8_t>(type));
        writer.U32(static_cast<uint32_t>(payload.size()));
        std::vector<uint8_t> frame = writer.Take();
        frame.insert(frame.end(), payload.begin(), payload.end());

        return m_connection->Send(frame);
    }
}
