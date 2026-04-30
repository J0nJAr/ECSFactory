#pragma once


#include <cstdint>
#include <vector>

#include "Base/Defines.hpp"
#include "INetworkConnection.hpp"   

namespace EF::Network {
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

}