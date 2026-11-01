#include "SimulatedNetwork.hpp"

#include <utility>

namespace EF::Network {

    uint64_t SimulatedNetwork::State::NextU64() {
        // xorshift64* : 결정론적이고 시드로 재현 가능한 PRNG.
        uint64_t x = rng == 0 ? 0x9E3779B97F4A7C15ull : rng;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rng = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    float SimulatedNetwork::State::NextFloat() {
        // 상위 53비트로 [0,1) 실수를 만든다.
        const uint64_t bits = NextU64() >> 11;
        return static_cast<float>(static_cast<double>(bits) * (1.0 / 9007199254740992.0));
    }

    void SimulatedNetwork::State::Deliver(const Endpoint& from, const Endpoint& target, std::span<const uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(mutex);

        // 손실: 확률적으로 버린다(소켓 관점에선 전송 성공).
        if (lossRate > 0.0f && NextFloat() < lossRate) {
            return;
        }

        // 수신 버퍼가 꽉 찼으면 새로 온 것을 버린다(꼬리 버림). SetInboxCapacity 주석 참조.
        std::size_t& queuedBytes = inboxBytes[target];
        auto fits = [&]() {
            return inboxCapacity == 0 || queuedBytes + bytes.size() <= inboxCapacity;
        };
        if (!fits()) {
            return;
        }

        Datagram datagram{ from, std::vector<uint8_t>(bytes.begin(), bytes.end()) };
        inboxes[target].push_back(datagram);
        queuedBytes += bytes.size();

        // 중복: 확률적으로 한 번 더 넣는다(중복본도 버퍼를 차지한다).
        if (duplicateRate > 0.0f && NextFloat() < duplicateRate && fits()) {
            inboxes[target].push_back(std::move(datagram));
            queuedBytes += bytes.size();
        }
    }

    std::optional<Datagram> SimulatedNetwork::State::Pull(const Endpoint& self) {
        std::lock_guard<std::mutex> lock(mutex);

        auto it = inboxes.find(self);
        if (it == inboxes.end() || it->second.empty()) {
            return std::nullopt;
        }

        std::deque<Datagram>& inbox = it->second;

        // 재정렬: 확률적으로 transit 버퍼의 앞쪽 윈도우에서 임의 위치를 꺼낸다.
        std::size_t index = 0;
        if (reorderRate > 0.0f && inbox.size() > 1 && NextFloat() < reorderRate) {
            const std::size_t window = reorderWindow < inbox.size()
                ? static_cast<std::size_t>(reorderWindow)
                : inbox.size();
            index = static_cast<std::size_t>(NextU64() % window);
        }

        Datagram datagram = std::move(inbox[index]);
        inbox.erase(inbox.begin() + static_cast<std::ptrdiff_t>(index));
        inboxBytes[self] -= datagram.bytes.size();
        return datagram;
    }

    SimulatedNetwork::Host::Host(std::shared_ptr<State> state, Endpoint self, Endpoint defaultPeer)
        : m_state(std::move(state)), m_self(self), m_defaultPeer(defaultPeer) {
    }

    bool SimulatedNetwork::Host::SendTo(const Endpoint& target, std::span<const uint8_t> message) {
        if (!target.IsValid()) {
            return false;
        }
        m_state->Deliver(m_self, target, message);
        return true;
    }

    std::optional<Datagram> SimulatedNetwork::Host::ReceiveFrom() {
        return m_state->Pull(m_self);
    }

    SimulatedNetwork::SimulatedNetwork(uint64_t seed)
        : m_state(std::make_shared<State>()) {
        m_state->rng = seed == 0 ? 0x9E3779B97F4A7C15ull : seed;
    }

    std::shared_ptr<INetworkConnection> SimulatedNetwork::CreateHost(const Endpoint& self, const Endpoint& defaultPeer) {
        return std::make_shared<Host>(m_state, self, defaultPeer);
    }

    void SimulatedNetwork::SetLoss(float rate) noexcept {
        m_state->lossRate = rate;
    }

    void SimulatedNetwork::SetDuplicate(float rate) noexcept {
        m_state->duplicateRate = rate;
    }

    void SimulatedNetwork::SetInboxCapacity(std::size_t bytes) noexcept {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->inboxCapacity = bytes;
    }

    void SimulatedNetwork::SetReorder(float rate, uint32_t window) noexcept {
        m_state->reorderRate = rate;
        m_state->reorderWindow = window == 0 ? 1 : window;
    }

}
