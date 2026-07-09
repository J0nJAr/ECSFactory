#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace EF::Network {

    // 하나의 원격 endpoint에 대한 신뢰성 상태 머신.
    // 앱 레코드(= [type][payload...])를 전달 등급에 따라 데이터그램으로 포장하고,
    // ack/재전송/조각화/재조립/순서 보장을 처리한다. 소켓을 직접 만지지 않는다.
    //
    // 채널 3종:
    //  - Reliable(0)   : 순서 보장 + 재전송. 제어/스냅샷. MTU 초과 시 조각화.
    //  - Sequenced(1)  : 비신뢰, 최신만. TickHash. 구식은 폐기.
    //  - Unreliable(2) : 비신뢰, 전부 전달. InputCommand. 중복 제거는 앱 계층에.
    class ReliabilityChannel {
    public:
        enum class Delivery : uint8_t {
            Reliable = 0,
            Sequenced = 1,
            Unreliable = 2
        };

        // 앱 레코드를 전달 등급별 송신 큐에 넣는다(아직 와이어로 나가지는 않는다).
        void Enqueue(Delivery delivery, std::span<const uint8_t> record);

        // 지금 내보낼 데이터그램들을 만든다.
        //  - Reliable: 미전송/재전송 대상 조각을 담는다.
        //  - Sequenced/Unreliable: 대기 중인 것을 담고 비운다(재전송 없음).
        //  - 받은 신뢰 데이터가 있으면(ackDirty) ack만 담은 데이터그램이라도 낸다.
        // 각 데이터그램 헤더에는 누적 ack가 실린다.
        [[nodiscard]] std::vector<std::vector<uint8_t>> Flush(uint64_t currentTick);

        // 데이터그램 하나를 처리한다. 순서대로 전달 가능한 앱 레코드를 outDelivered에 넣는다.
        void OnReceive(std::span<const uint8_t> datagram, std::vector<std::vector<uint8_t>>& outDelivered);

        [[nodiscard]] bool HasPendingReliable() const noexcept {
            return !m_outgoingReliable.empty();
        }

    private:
        struct OutgoingReliable {
            uint32_t sequence = 0;
            std::vector<uint8_t> record;
            uint64_t lastSentTick = 0;
            bool everSent = false;
        };

        struct Reassembly {
            uint16_t fragmentCount = 0;
            uint16_t receivedCount = 0;
            std::vector<std::vector<uint8_t>> fragments;
            std::vector<bool> have;
            bool initialized = false;
        };

        void AckReliable(uint32_t ack);
        void DeliverInOrder(std::vector<std::vector<uint8_t>>& outDelivered);

        // 송신 상태
        uint32_t m_nextReliableSeq = 0;
        std::deque<OutgoingReliable> m_outgoingReliable;
        std::vector<std::vector<uint8_t>> m_pendingSequenced; // 레코드 그대로
        std::vector<std::vector<uint8_t>> m_pendingUnreliable;
        uint16_t m_nextSequencedId = 0;

        // 수신 상태
        uint32_t m_expectedReliableSeq = 0;
        std::unordered_map<uint32_t, Reassembly> m_reassembly;
        uint16_t m_lastSequencedId = 0;
        bool m_hasSequenced = false;
        bool m_ackDirty = false;
    };

}
