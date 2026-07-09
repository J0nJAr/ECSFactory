// UDPConnection.cpp
#include "UDPConnection.hpp"

namespace EF::Network {
    namespace {
        constexpr std::size_t kMaxDatagramSize = 65536;

        bool EnsureWinsockInitialized() {
            static const bool initialized = []() {
                WSADATA data{};
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            return initialized;
        }
    }

    UDPConnection::UDPConnection() : m_socket(INVALID_SOCKET) {
        if (!EnsureWinsockInitialized()) {
            return;
        }
        // 단 하나의 소켓만 생성합니다.
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket != INVALID_SOCKET) {
            SetNonBlocking();
        }
    }

    UDPConnection::~UDPConnection() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket); // 자원 해제
        }
    }

    void UDPConnection::SetNonBlocking() {
        u_long mode = 1;
        ioctlsocket(m_socket, FIONBIO, &mode);
    }

    bool UDPConnection::Bind(uint16_t port) {
        if (m_socket == INVALID_SOCKET) return false;

        sockaddr_in localAddress = {};
        localAddress.sin_family = AF_INET;
        localAddress.sin_addr.s_addr = INADDR_ANY;
        localAddress.sin_port = htons(port);

        // 내가 들고 있는 그 소켓에 바인딩합니다.
        return bind(m_socket, reinterpret_cast<sockaddr*>(&localAddress), sizeof(localAddress)) != SOCKET_ERROR;
    }

    void UDPConnection::Connect(const std::string_view& address, uint16_t port) {
        sockaddr_in peer = {};
        peer.sin_family = AF_INET;
        inet_pton(AF_INET, address.data(), &peer.sin_addr);
        peer.sin_port = htons(port);
        m_peerEndpoint = FromSockAddr(peer);
    }

    bool UDPConnection::SendTo(const Endpoint& target, std::span<const uint8_t> message) {
        if (m_socket == INVALID_SOCKET || !target.IsValid()) return false;

        sockaddr_in destination = ToSockAddr(target);

        // 메시지 하나 = 데이터그램 하나로 보낸다.
        const int sent = sendto(m_socket,
                                reinterpret_cast<const char*>(message.data()),
                                static_cast<int>(message.size()), 0,
                                reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
        return sent == static_cast<int>(message.size());
    }

    std::optional<Datagram> UDPConnection::ReceiveFrom() {
        if (m_socket == INVALID_SOCKET) return std::nullopt;

        sockaddr_in fromAddress{};
        int fromAddressSize = sizeof(fromAddress);

        // 한 번의 recvfrom = 데이터그램 하나 = 메시지 하나.
        std::vector<uint8_t> buffer(kMaxDatagramSize);
        const int bytesReceived = recvfrom(m_socket,
                                           reinterpret_cast<char*>(buffer.data()),
                                           static_cast<int>(buffer.size()), 0,
                                           reinterpret_cast<sockaddr*>(&fromAddress), &fromAddressSize);

        // 논블로킹이라 받을 게 없으면 SOCKET_ERROR(-1) → nullopt
        if (bytesReceived <= 0) {
            return std::nullopt;
        }

        buffer.resize(static_cast<std::size_t>(bytesReceived));
        return Datagram{
            .from = FromSockAddr(fromAddress),
            .bytes = std::move(buffer)
        };
    }

    Endpoint UDPConnection::FromSockAddr(const sockaddr_in& address) noexcept {
        // 네트워크 바이트 오더 그대로 담아 두어 sendto로 변환 없이 되돌린다.
        return Endpoint::IPv4(address.sin_addr.s_addr, address.sin_port);
    }

    sockaddr_in UDPConnection::ToSockAddr(const Endpoint& endpoint) noexcept {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = endpoint.address;
        address.sin_port = endpoint.port;
        return address;
    }
}
