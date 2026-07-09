#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 같은 프로세스 내에서 네트워크 연결을 시뮬레이트하는 메모리 기반 구현.
    // 메시지를 통째로 큐에 넣고 빼므로, 길이 접두 없이 메시지 경계가 보존된다.
    // 한 쌍(pair)은 서로를 유일한 peer로 삼는 point-to-point 연결이다.
    class MemoryConnection final : public INetworkConnection {
    public:
        using Pair = std::pair<std::shared_ptr<MemoryConnection>, std::shared_ptr<MemoryConnection>>;

        static Pair CreatePair();

        ~MemoryConnection() override;

        MemoryConnection(const MemoryConnection&) = delete;
        MemoryConnection& operator=(const MemoryConnection&) = delete;

        bool SendTo(const Endpoint& target, std::span<const uint8_t> message) override;
        std::optional<Datagram> ReceiveFrom() override;
        [[nodiscard]] Endpoint DefaultPeer() const override {
            return m_peerEndpoint;
        }

        [[nodiscard]] Endpoint SelfEndpoint() const noexcept {
            return m_selfEndpoint;
        }

        void Close();

    private:
        MemoryConnection() = default;

        void Bind(const std::shared_ptr<MemoryConnection>& peer, Endpoint self, Endpoint peerEndpoint);
        void Enqueue(Datagram datagram);

    private:
        std::weak_ptr<MemoryConnection> m_peer;
        Endpoint m_selfEndpoint;
        Endpoint m_peerEndpoint;
        std::mutex m_mutex;
        std::queue<Datagram> m_inboundMessages;
        bool m_isClosed = false;
    };

}
