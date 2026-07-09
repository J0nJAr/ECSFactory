#include "MemoryConnection.hpp"

#include <atomic>
#include <utility>

namespace EF::Network {
    namespace {
        // pair마다 유일한 peer id 두 개를 발급한다. 값 자체에는 의미가 없고
        // 같은 서버 NetworkManager가 여러 peer를 구분할 수 있게 하는 용도다.
        uint32_t NextMemoryPeerId() {
            static std::atomic<uint32_t> counter{ 1 };
            return counter.fetch_add(1, std::memory_order_relaxed);
        }
    }

    MemoryConnection::Pair MemoryConnection::CreatePair() {
        std::shared_ptr<MemoryConnection> first(new MemoryConnection());
        std::shared_ptr<MemoryConnection> second(new MemoryConnection());

        const Endpoint firstEndpoint = Endpoint::Memory(NextMemoryPeerId());
        const Endpoint secondEndpoint = Endpoint::Memory(NextMemoryPeerId());

        first->Bind(second, firstEndpoint, secondEndpoint);
        second->Bind(first, secondEndpoint, firstEndpoint);

        return { first, second };
    }

    MemoryConnection::~MemoryConnection() {
        Close();
    }

    bool MemoryConnection::SendTo(const Endpoint& target, std::span<const uint8_t> message) {
        static_cast<void>(target); // point-to-point라 대상은 항상 peer 하나다.
        if (m_isClosed) {
            return false;
        }
        if (message.empty()) {
            return true; // 보낼 게 없으면 성공 취급
        }

        std::shared_ptr<MemoryConnection> peer = m_peer.lock();
        if (!peer) {
            return false;
        }

        // 메시지를 통째로 상대 큐에 넣는다. 받는 쪽은 from=내 endpoint로 본다.
        peer->Enqueue(Datagram{
            .from = m_selfEndpoint,
            .bytes = std::vector<uint8_t>(message.begin(), message.end())
        });
        return true;
    }

    std::optional<Datagram> MemoryConnection::ReceiveFrom() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_inboundMessages.empty()) {
            return std::nullopt; // 논블로킹: 없으면 즉시 nullopt
        }

        Datagram datagram = std::move(m_inboundMessages.front());
        m_inboundMessages.pop();
        return datagram;
    }

    void MemoryConnection::Close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isClosed = true;
    }

    void MemoryConnection::Bind(const std::shared_ptr<MemoryConnection>& peer, Endpoint self, Endpoint peerEndpoint) {
        m_peer = peer;
        m_selfEndpoint = self;
        m_peerEndpoint = peerEndpoint;
    }

    void MemoryConnection::Enqueue(Datagram datagram) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inboundMessages.push(std::move(datagram));
    }

}
