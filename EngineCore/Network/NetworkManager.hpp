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
#include "Network/INetworkConnection.hpp"
#include "Network/Packet.hpp"

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
        uint64_t lastReceivedTick = 0;
        bool hasTypeTable = false;
        bool hasSnapshot = false;
        std::vector<ComponentTypeDescriptor> remoteTypes;
        std::deque<InputCommand> pendingInputs;

        // 입력 시퀀스 추적: 중복/구식 InputCommand를 걸러내는 데 쓴다.
        uint32_t lastInputSequence = 0;
        bool hasInput = false;
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

        NetworkClientState& GetOrCreateClient(uint32_t clientId);
        // 시퀀스 기준 중복/구식 입력은 폐기한다. 수락되면 true.
        bool EnqueueInput(const InputCommand& command);
        [[nodiscard]] const std::deque<InputCommand>& PendingInputs(uint32_t clientId) const;

        bool SendHello(uint32_t requestedClientId = 0);
        bool SendTypeTable(const std::vector<ComponentTypeDescriptor>& types);
        bool SendWorldSnapshotRequest(uint64_t lastKnownTick);
        bool SendWorldSnapshot(const WorldSnapshot& snapshot);
        bool SendInputCommand(const InputCommand& command);
        bool SendTickHash(const TickHash& tickHash);
        bool SendResyncRequest(uint64_t tick, uint64_t localHash, uint64_t remoteHash);

        [[nodiscard]] std::optional<Network::NetworkPacket> TryReceivePacket();

        static std::vector<uint8_t> EncodeTypeTable(const std::vector<ComponentTypeDescriptor>& types);
        static bool DecodeTypeTable(const std::vector<uint8_t>& payload, std::vector<ComponentTypeDescriptor>& outTypes);

        static std::vector<uint8_t> EncodeWorldSnapshot(const WorldSnapshot& snapshot);
        static bool DecodeWorldSnapshot(const std::vector<uint8_t>& payload, WorldSnapshot& outSnapshot);

        static std::vector<uint8_t> EncodeInputCommand(const InputCommand& command);
        static bool DecodeInputCommand(const std::vector<uint8_t>& payload, InputCommand& outCommand);

        static std::vector<uint8_t> EncodeTickHash(const TickHash& tickHash);
        static bool DecodeTickHash(const std::vector<uint8_t>& payload, TickHash& outTickHash);

    private:
        bool SendPacket(Network::PacketType type, const std::vector<uint8_t>& payload);

        std::shared_ptr<Network::INetworkConnection> m_connection;
        uint32_t m_localClientId = 0;
        std::unordered_map<uint32_t, NetworkClientState> m_clients;

        // 수신 프레임 버퍼. 한 프레임에 여러 패킷이 들어있을 수 있어서
        // 프레임을 들고 다니며 레코드를 하나씩 꺼낸다(배칭 대비).
        std::vector<uint8_t> m_currentFrame;
        std::size_t m_frameOffset = 0;
    };

}
