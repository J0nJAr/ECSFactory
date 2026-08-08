#pragma once


#include <cstdint>
#include <vector>

#include "Base/Defines.hpp"
#include "INetworkConnection.hpp"   

namespace EF::Network {
    // 프레임 맨 앞에 1바이트로 붙는 프로토콜 버전. 와이어 포맷이 바뀌면 올린다.
    //
    // ⚠️ 솔직한 현황: 이 상수는 **아직 어디에서도 읽히지 않는다.** 실제 프레임에 버전
    // 바이트를 붙이고 검사하는 코드가 없어서, 지금은 "와이어 포맷의 세대 번호"를 적어두는
    // 문서 역할만 한다. 버전 불일치 클라이언트는 조용히 깨진 데이터를 읽게 된다.
    // handshake(Hello/Welcome)에서 이 값을 교환하고 거르는 건 아직 할 일로 남아 있다.
    //
    // 이력:
    //   1 — 최초.
    //   2 — WorldSnapshot에 EntitySnapshot(nextEntityIndex / versions / freeIndices) 추가.
    inline constexpr uint8_t kProtocolVersion = 2;

    enum class PacketType : uint8_t {
        Hello = 1,
        TypeTable = 2,
        WorldSnapshotRequest = 3,
        WorldSnapshot = 4,
        InputCommand = 5,
        TickHash = 6,
        ResyncRequest = 7,
        Welcome = 8,
        InputFrame = 9
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
