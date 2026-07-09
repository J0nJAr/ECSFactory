#pragma once


#include <cstdint>
#include <vector>

#include "Base/Defines.hpp"
#include "INetworkConnection.hpp"   

namespace EF::Network {
    // 프레임 맨 앞에 1바이트로 붙는 프로토콜 버전. 와이어 포맷이 바뀌면 올린다.
    inline constexpr uint8_t kProtocolVersion = 1;

    enum class PacketType : uint8_t {
        Hello = 1,
        TypeTable = 2,
        WorldSnapshotRequest = 3,
        WorldSnapshot = 4,
        InputCommand = 5,
        TickHash = 6,
        ResyncRequest = 7,
        Welcome = 8
    };

    struct PacketHeader {
        uint32_t sequenceNumber;
        uint32_t ackNumber;
    };
    
    struct ReliablePacket {
        std::vector<uint8_t> payload;
    };

    struct NotReliablePacket {
        std::vector<uint8_t> payload;
    };

    struct NetworkPacket {
        PacketType type = PacketType::Hello;
        std::vector<uint8_t> payload;
    };

}
