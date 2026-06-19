#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <utility>
#include <vector>

#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 같은 프로세스 내에서 네트워크 연결을 시뮬레이트하는 메모리 기반 구현.
    // 메시지를 통째로 큐에 넣고 빼므로, 길이 접두 없이 메시지 경계가 보존된다.
    class MemoryConnection final : public INetworkConnection {
    public:
        using Pair = std::pair<std::shared_ptr<MemoryConnection>, std::shared_ptr<MemoryConnection>>;

        static Pair CreatePair();

        ~MemoryConnection() override;

        MemoryConnection(const MemoryConnection&) = delete;
        MemoryConnection& operator=(const MemoryConnection&) = delete;

        bool Send(std::span<const uint8_t> message) override;
        std::optional<std::vector<uint8_t>> Receive() override;

        void Close();

    private:
        MemoryConnection() = default;

        void SetPeer(const std::shared_ptr<MemoryConnection>& peer);
        void Enqueue(std::vector<uint8_t> message);

    private:
        std::weak_ptr<MemoryConnection> m_peer;
        std::mutex m_mutex;
        std::queue<std::vector<uint8_t>> m_inboundMessages;
        bool m_isClosed = false;
    };

}
