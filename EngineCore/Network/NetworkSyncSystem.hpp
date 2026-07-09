#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "ECS/ISystem.hpp"
#include "ECS/World.hpp"
#include "Network/NetworkManager.hpp"

namespace EF {

    enum class NetworkSyncMode {
        Client,
        Server
    };

    class NetworkSyncSystem final : public ISystem {
    public:
        explicit NetworkSyncSystem(NetworkSyncMode mode);

        void Initialize(World& world) override;
        void FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) override;
        void FrameUpdate(World& world, float frameDeltaSeconds) override;
        void Shutdown(World& world) override;

        [[nodiscard]] bool HasTypeTable() const noexcept {
            return m_hasTypeTable;
        }

        [[nodiscard]] bool HasWelcome() const noexcept {
            return m_hasWelcome;
        }

        [[nodiscard]] bool HasSnapshot() const noexcept {
            return m_hasSnapshot;
        }

        [[nodiscard]] uint64_t LastSnapshotTick() const noexcept {
            return m_lastSnapshotTick;
        }

        [[nodiscard]] bool HasHashMismatch() const noexcept {
            return m_hasHashMismatch;
        }

        [[nodiscard]] TickHash LastRemoteTickHash() const noexcept {
            return m_lastRemoteTickHash;
        }

        void QueueLocalInput(const InputCommand& command);

    private:
        void UpdateClient(World& world, NetworkManager& network);
        void UpdateServer(World& world, NetworkManager& network, uint64_t tick);
        void SendQueuedLocalInputs(NetworkManager& network, uint64_t tick);

        void HandlePacketOnClient(World& world, NetworkManager& network, const Network::NetworkPacket& packet);
        void HandlePacketOnServer(World& world, NetworkManager& network, const Network::Endpoint& from, const Network::NetworkPacket& packet, uint64_t tick);

    private:
        NetworkSyncMode m_mode;
        bool m_sentHello = false;
        bool m_hasWelcome = false;
        bool m_requestedSnapshot = false;
        bool m_hasTypeTable = false;
        bool m_hasSnapshot = false;
        bool m_hasHashMismatch = false;
        uint64_t m_lastSnapshotTick = 0;
        uint32_t m_nextInputSequence = 1;
        TickHash m_lastRemoteTickHash{};
        std::deque<InputCommand> m_localInputs;
        std::vector<ComponentTypeDescriptor> m_remoteTypes;
    };

}
