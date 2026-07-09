#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 일단 window 전용. 데이터그램 하나 = 메시지 하나.
    // 소켓 하나로 여러 원격 peer를 상대할 수 있다(recvfrom의 sender를 endpoint로 노출).
    class UDPConnection final : public INetworkConnection {
    public:
        UDPConnection();
        ~UDPConnection() override;

        // 초기화 및 바인딩 (서버용 또는 로컬 수신용)
        bool Bind(uint16_t port);

        // 기본 상대 설정 (클라이언트용). 이후 Send()가 이 대상으로 나간다.
        void Connect(const std::string_view& address, uint16_t port);

        // endpoint 지정 송수신.
        bool SendTo(const Endpoint& target, std::span<const uint8_t> message) override;
        std::optional<Datagram> ReceiveFrom() override;
        [[nodiscard]] Endpoint DefaultPeer() const override {
            return m_peerEndpoint;
        }

    private:
        static Endpoint FromSockAddr(const sockaddr_in& address) noexcept;
        static sockaddr_in ToSockAddr(const Endpoint& endpoint) noexcept;

        void SetNonBlocking();

        SOCKET m_socket;
        Endpoint m_peerEndpoint;
    };
}
