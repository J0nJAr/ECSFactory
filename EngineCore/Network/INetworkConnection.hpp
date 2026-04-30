#pragma once

#include <cstdint>

namespace EF::Network {
    
    class INetworkConnection {
    public:
        virtual ~INetworkConnection() = default;

        virtual void Send(const void* data, size_t size) = 0;
        virtual void Receive(void* data, size_t size) = 0;
    };
}