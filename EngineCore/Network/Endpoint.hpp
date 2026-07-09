#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace EF::Network {

    // 전송 계층 공용 주소.
    //  - IPv4   : UDP 소켓용. address/port는 네트워크 바이트 오더 그대로 담는다
    //             (recvfrom/sendto 사이에서 변환 없이 왕복시키기 위함).
    //  - Memory : 같은 프로세스 시뮬레이션용. address에 peer id를 담는다.
    struct Endpoint {
        enum class Kind : uint8_t {
            None = 0,
            IPv4 = 1,
            Memory = 2
        };

        Kind kind = Kind::None;
        uint32_t address = 0; // IPv4: sin_addr.s_addr(네트워크 오더), Memory: peer id
        uint16_t port = 0;    // IPv4: sin_port(네트워크 오더), Memory: 0

        [[nodiscard]] bool IsValid() const noexcept {
            return kind != Kind::None;
        }

        friend bool operator==(const Endpoint& lhs, const Endpoint& rhs) noexcept {
            return lhs.kind == rhs.kind && lhs.address == rhs.address && lhs.port == rhs.port;
        }

        friend bool operator!=(const Endpoint& lhs, const Endpoint& rhs) noexcept {
            return !(lhs == rhs);
        }

        static Endpoint IPv4(uint32_t networkOrderAddress, uint16_t networkOrderPort) noexcept {
            return Endpoint{ Kind::IPv4, networkOrderAddress, networkOrderPort };
        }

        static Endpoint Memory(uint32_t peerId) noexcept {
            return Endpoint{ Kind::Memory, peerId, 0 };
        }
    };

}

template<>
struct std::hash<EF::Network::Endpoint> {
    std::size_t operator()(const EF::Network::Endpoint& endpoint) const noexcept {
        // FNV-1a 방식으로 세 필드를 섞는다.
        std::size_t hash = 14695981039346656037ull;
        auto mix = [&hash](uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        mix(static_cast<uint64_t>(endpoint.kind));
        mix(endpoint.address);
        mix(endpoint.port);
        return hash;
    }
};
