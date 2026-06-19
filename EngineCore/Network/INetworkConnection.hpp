#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace EF::Network {

    // 통로는 "메시지(프레임) 단위"로 주고받는다.
    // 메시지 경계를 보장하는 책임은 각 구현체에 있다.
    //  - MemoryConnection : 메시지 큐 (길이 접두 불필요)
    //  - UDPConnection    : 데이터그램 하나 = 메시지 하나
    //  - (미래) TCPConnection : 내부에서 길이 접두 프레이밍
    class INetworkConnection {
    public:
        virtual ~INetworkConnection() = default;

        // 메시지 하나를 통째로 보낸다. 전부 보냈으면 true.
        virtual bool Send(std::span<const uint8_t> message) = 0;

        // 메시지 하나를 통째로 받는다. 받을 게 없으면 nullopt.
        virtual std::optional<std::vector<uint8_t>> Receive() = 0;
    };
}
