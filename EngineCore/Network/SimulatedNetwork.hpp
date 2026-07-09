#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 손실/중복/재정렬을 결정론적으로 주입하는 인메모리 네트워크 시뮬레이터.
    // 하나의 hub에 여러 host(endpoint)를 붙여, 서버 하나가 여러 클라이언트를
    // 상대하는 상황도 재현할 수 있다. 시드 기반 PRNG라 실행마다 동일하게 반복된다.
    //
    //   SimulatedNetwork net(seed);
    //   auto server = net.CreateHost(serverEp, /*defaultPeer*/ {});
    //   auto client = net.CreateHost(clientEp, /*defaultPeer*/ serverEp);
    //   net.SetLoss(0.3f);
    class SimulatedNetwork {
    public:
        explicit SimulatedNetwork(uint64_t seed);

        // 지정 endpoint로 host 하나를 만든다. defaultPeer는 point-to-point 편의용.
        std::shared_ptr<INetworkConnection> CreateHost(const Endpoint& self, const Endpoint& defaultPeer = {});

        void SetLoss(float rate) noexcept;         // [0,1) 확률로 데이터그램 손실
        void SetDuplicate(float rate) noexcept;    // [0,1) 확률로 중복 전달
        void SetReorder(float rate, uint32_t window = 4) noexcept; // 재정렬 확률/윈도우

    private:
        struct State {
            std::mutex mutex;
            std::unordered_map<Endpoint, std::deque<Datagram>> inboxes;
            uint64_t rng = 0;
            float lossRate = 0.0f;
            float duplicateRate = 0.0f;
            float reorderRate = 0.0f;
            uint32_t reorderWindow = 4;

            uint64_t NextU64();
            float NextFloat(); // [0, 1)
            void Deliver(const Endpoint& from, const Endpoint& target, std::span<const uint8_t> bytes);
            std::optional<Datagram> Pull(const Endpoint& self);
        };

        class Host final : public INetworkConnection {
        public:
            Host(std::shared_ptr<State> state, Endpoint self, Endpoint defaultPeer);

            bool SendTo(const Endpoint& target, std::span<const uint8_t> message) override;
            std::optional<Datagram> ReceiveFrom() override;
            [[nodiscard]] Endpoint DefaultPeer() const override {
                return m_defaultPeer;
            }

        private:
            std::shared_ptr<State> m_state;
            Endpoint m_self;
            Endpoint m_defaultPeer;
        };

        std::shared_ptr<State> m_state;
    };

}
