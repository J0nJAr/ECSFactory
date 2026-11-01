#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <unordered_map>
#include <vector>

namespace EF::Network {

    // ── 와이어 크기 상수 ─────────────────────────────────────────────────────
    // 근거와 버린 대안은 docs/design/large-record-transport.md.

    // 신뢰 채널 조각 하나에 담는 레코드 바이트 수. MTU 아래로 유지한다
    // (키우면 IP 단편화가 생겨 단편 하나만 잃어도 조각 전체가 사라진다).
    inline constexpr std::size_t kFragmentSize = 1024;

    // 한 데이터그램에 담는 메시지 총량 상한(대략적인 MTU 여유).
    inline constexpr std::size_t kMaxDatagramPayload = 1200;

    // 데이터그램 헤더: [version u8][누적 ack u32].
    inline constexpr std::size_t kDatagramHeaderSize = 1 + 4;

    // 신뢰 레코드 하나의 상한(16 MiB = 조각 16,384개). 조각 수는 와이어에 u16으로 실리므로
    // 형식상 65535개까지 되지만, 그보다 큰 월드는 한 레코드로 보낼 게 아니라 스냅샷 자체를
    // 쪼개야 할 문제다(관심 영역·청크). 받는 쪽 재조립 메모리의 상한이기도 하다.
    inline constexpr std::size_t kMaxReliableRecordBytes = std::size_t{ 16 } * 1024 * 1024;
    inline constexpr std::size_t kMaxReliableFragments = kMaxReliableRecordBytes / kFragmentSize;

    // 비신뢰(Sequenced/Unreliable) 레코드 하나의 상한 = 데이터그램 하나에 들어가는 크기.
    // 메시지 헤더가 가장 긴 Sequenced 기준이다: [channel u8][sequenceId u16][length u16].
    // 이보다 크면 IP 단편화에 맡기게 되는데, 재전송이 없으니 단편 하나만 잃어도 끝이다.
    inline constexpr std::size_t kMaxUnfragmentedRecordBytes = kMaxDatagramPayload - kDatagramHeaderSize - (1 + 2 + 2);

    // 받는 쪽이 재조립을 받아 주는 시퀀스 창: 다음 기대 시퀀스 + 이 값 미만.
    // 정상 송신자는 ack 없이 이만큼 앞서 가지 않는다(tick당 신뢰 레코드는 몇 개뿐이고 ack는
    // 왕복마다 온다). 창 밖 조각은 버린다 — 송신자가 재전송하므로 정상 흐름은 깨지지 않는다.
    inline constexpr uint32_t kMaxReassemblyWindow = 256;

    // tick당 한 상대에게 내보내는 신뢰 조각 수의 상한(페이싱). 재전송도 이 예산 안에서 한다.
    //
    // 48을 고른 근거: 48 × (조각 1024 + 헤더) ≈ 50KB로, Windows UDP 수신 버퍼 기본값(64KB 안팎)
    // 안에 들어간다. 받는 쪽은 tick마다 버퍼를 비우므로, 소켓 버퍼를 키우지 못한 환경에서도
    // 한 tick 분량이 넘치지 않는다. 대역폭으로는 50Hz × 50KB ≈ 2.4MB/s, 1 MiB 스냅샷 ≈ 0.4초.
    // 키우면 큰 조인이 빨라지지만 버퍼 넘침 위험이 커진다(large-record-transport.md §4.2).
    inline constexpr std::size_t kReliableFragmentBudgetPerTick = 48;

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
        // 등급별 상한(kMaxReliableRecordBytes / kMaxUnfragmentedRecordBytes)을 넘으면
        // 넣지 않고 false. 상한을 아는 쪽이 검사한다 — NetworkManager는 결과를 돌려줄 뿐이다.
        bool Enqueue(Delivery delivery, std::span<const uint8_t> record);

        // 지금 내보낼 데이터그램들을 만든다.
        //  - Reliable: 미전송/재전송 대상 조각을 담는다. 단 tick당 kReliableFragmentBudgetPerTick개까지만
        //    (페이싱). 큰 레코드는 여러 tick에 걸쳐 한 "패스"를 다 보내고, 재전송 간격은 그 패스의
        //    마지막 조각을 보낸 tick부터 센다. 같은 tick에 여러 번 불러도 예산은 tick 단위로 공유된다.
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
            // 마지막으로 **패스를 끝낸**(모든 조각을 한 번씩 보낸) tick. 재전송 간격의 기준이다.
            // 패스를 시작한 tick으로 세면, 여러 tick에 걸쳐 보내는 큰 레코드는 다 보내기도 전에
            // 앞부분 재전송이 시작돼 대역폭을 두 번 쓴다.
            uint64_t lastSentTick = 0;
            bool everSent = false;
            // 지금 진행 중인 패스에서 다음에 보낼 조각 번호. passInProgress일 때만 의미가 있다.
            std::size_t nextFragment = 0;
            bool passInProgress = false;
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
        // 페이싱 예산: m_budgetTick에 이미 내보낸 신뢰 조각 수. tick이 바뀌면 0으로 돌아간다.
        uint64_t m_budgetTick = ~uint64_t{ 0 };
        std::size_t m_budgetUsed = 0;
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
