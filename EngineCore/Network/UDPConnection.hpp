#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "Base/GameEngine.hpp"
#include "Base/IGameApp.hpp"
#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 일단 window 전용. 데이터그램 하나 = 메시지 하나.
    class UDPConnection final : public INetworkConnection {
    public:
        UDPConnection();
        ~UDPConnection() override;

        // 초기화 및 바인딩 (서버용 또는 로컬 수신용)
        bool Bind(uint16_t port);

        // 보낼 대상 설정 (클라이언트용)
        void Connect(const std::string_view &address, uint16_t port);

        // 메시지(데이터그램) 단위 논블로킹 송수신.
        bool Send(std::span<const uint8_t> message) override;
        std::optional<std::vector<uint8_t>> Receive() override;

    private:
        SOCKET m_socket;
        sockaddr_in m_peerAddress;
        bool m_isConnected;

        void SetNonBlocking();
    };
}
