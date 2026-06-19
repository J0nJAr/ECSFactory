#include "MemoryConnection.hpp"

#include <utility>

namespace EF::Network {

    MemoryConnection::Pair MemoryConnection::CreatePair() {
        std::shared_ptr<MemoryConnection> first(new MemoryConnection());
        std::shared_ptr<MemoryConnection> second(new MemoryConnection());

        first->SetPeer(second);
        second->SetPeer(first);

        return { first, second };
    }

    MemoryConnection::~MemoryConnection() {
        Close();
    }

    bool MemoryConnection::Send(std::span<const uint8_t> message) {
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

        // 메시지를 통째로 상대 큐에 넣는다. 경계는 vector 그 자체가 들고 있다.
        peer->Enqueue(std::vector<uint8_t>(message.begin(), message.end()));
        return true;
    }

    std::optional<std::vector<uint8_t>> MemoryConnection::Receive() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_inboundMessages.empty()) {
            return std::nullopt; // 논블로킹: 없으면 즉시 nullopt
        }

        std::vector<uint8_t> message = std::move(m_inboundMessages.front());
        m_inboundMessages.pop();
        return message;
    }

    void MemoryConnection::Close() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_isClosed = true;
    }

    void MemoryConnection::SetPeer(const std::shared_ptr<MemoryConnection>& peer) {
        m_peer = peer;
    }

    void MemoryConnection::Enqueue(std::vector<uint8_t> message) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_inboundMessages.push(std::move(message));
    }

}
