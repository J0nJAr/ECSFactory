#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Network/Endpoint.hpp"

namespace EF::Network {

    // 수신된 데이터그램 하나 = 보낸 사람 endpoint + 바이트.
    struct Datagram {
        Endpoint from;
        std::vector<uint8_t> bytes;
    };

    // 통로는 "메시지(데이터그램) 단위"로, endpoint를 붙여서 주고받는다.
    // 메시지 경계를 보장하는 책임은 각 구현체에 있다.
    //  - MemoryConnection : 메시지 큐 (길이 접두 불필요, 단일 peer)
    //  - UDPConnection    : 데이터그램 하나 = 메시지 하나 (다중 peer)
    //  - SimulatedNetwork : 손실/재정렬 주입 (테스트용, 다중 peer)
    //
    // 클라이언트는 서버 하나만 상대하므로 DefaultPeer()로 point-to-point
    // 편의 래퍼(Send/Receive)를 쓸 수 있다. 서버는 endpoint를 명시하는
    // SendTo/ReceiveFrom으로 여러 peer를 디먹스한다.
    class INetworkConnection {
    public:
        virtual ~INetworkConnection() = default;

        // 지정한 endpoint로 메시지 하나를 통째로 보낸다. 전부 보냈으면 true.
        virtual bool SendTo(const Endpoint& target, std::span<const uint8_t> message) = 0;

        // 메시지 하나를 통째로 받는다(보낸 사람 endpoint 포함). 받을 게 없으면 nullopt.
        virtual std::optional<Datagram> ReceiveFrom() = 0;

        // 이 연결의 기본 상대. point-to-point 편의 래퍼가 대상으로 삼는다.
        [[nodiscard]] virtual Endpoint DefaultPeer() const = 0;

        // point-to-point 편의 래퍼 (기본 상대로 송수신). 기존 코드/테스트 호환용.
        bool Send(std::span<const uint8_t> message) {
            return SendTo(DefaultPeer(), message);
        }

        std::optional<std::vector<uint8_t>> Receive() {
            std::optional<Datagram> datagram = ReceiveFrom();
            if (!datagram) {
                return std::nullopt;
            }
            return std::move(datagram->bytes);
        }
    };
}
