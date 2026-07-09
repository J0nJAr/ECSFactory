#include "ReliabilityChannel.hpp"

#include "Network/NetUtils.hpp"

namespace EF::Network {
    namespace {
        constexpr uint8_t kReliabilityVersion = 1;

        // 신뢰 채널 조각 하나에 담는 레코드 바이트 수. MTU 아래로 유지한다.
        constexpr std::size_t kFragmentSize = 1024;

        // 한 데이터그램에 담는 메시지 총량 상한(대략적인 MTU 여유).
        constexpr std::size_t kMaxDatagramPayload = 1200;

        // 신뢰 메시지 재전송 간격(틱). ack가 안 오면 이 간격마다 다시 보낸다.
        constexpr uint64_t kResendIntervalTicks = 4;

        constexpr uint8_t kChannelReliable = 0;
        constexpr uint8_t kChannelSequenced = 1;
        constexpr uint8_t kChannelUnreliable = 2;

        // 시퀀스 wraparound에 안전한 "더 최신인가" 비교.
        bool IsMoreRecent(uint16_t candidate, uint16_t reference) {
            return static_cast<int16_t>(candidate - reference) > 0;
        }
    }

    void ReliabilityChannel::Enqueue(Delivery delivery, std::span<const uint8_t> record) {
        std::vector<uint8_t> bytes(record.begin(), record.end());
        switch (delivery) {
        case Delivery::Reliable: {
            OutgoingReliable outgoing;
            outgoing.sequence = m_nextReliableSeq++;
            outgoing.record = std::move(bytes);
            m_outgoingReliable.push_back(std::move(outgoing));
            break;
        }
        case Delivery::Sequenced:
            m_pendingSequenced.push_back(std::move(bytes));
            break;
        case Delivery::Unreliable:
            m_pendingUnreliable.push_back(std::move(bytes));
            break;
        }
    }

    std::vector<std::vector<uint8_t>> ReliabilityChannel::Flush(uint64_t currentTick) {
        // 1) 이번에 내보낼 메시지 블롭을 모은다.
        std::vector<std::vector<uint8_t>> messages;

        // 신뢰: 미전송 또는 재전송 시점이 된 것. 조각으로 나눠 담는다.
        for (OutgoingReliable& outgoing : m_outgoingReliable) {
            const bool resendDue = outgoing.everSent &&
                (currentTick - outgoing.lastSentTick) >= kResendIntervalTicks;
            if (outgoing.everSent && !resendDue) {
                continue;
            }

            const std::size_t recordSize = outgoing.record.size();
            const std::size_t fragmentCount = recordSize == 0
                ? 1
                : (recordSize + kFragmentSize - 1) / kFragmentSize;

            for (std::size_t index = 0; index < fragmentCount; ++index) {
                const std::size_t offset = index * kFragmentSize;
                const std::size_t length = (recordSize - offset) < kFragmentSize
                    ? (recordSize - offset)
                    : kFragmentSize;

                PacketWriter writer;
                writer.U8(kChannelReliable);
                writer.U32(outgoing.sequence);
                writer.U16(static_cast<uint16_t>(index));
                writer.U16(static_cast<uint16_t>(fragmentCount));
                writer.U16(static_cast<uint16_t>(length));
                writer.WriteRaw(outgoing.record.data() + offset, length);
                messages.push_back(writer.Take());
            }

            outgoing.everSent = true;
            outgoing.lastSentTick = currentTick;
        }

        // 시퀀스드: 한 번만 보내고 비운다.
        for (std::vector<uint8_t>& record : m_pendingSequenced) {
            PacketWriter writer;
            writer.U8(kChannelSequenced);
            writer.U16(m_nextSequencedId++);
            writer.U16(static_cast<uint16_t>(record.size()));
            writer.WriteRaw(record);
            messages.push_back(writer.Take());
        }
        m_pendingSequenced.clear();

        // 비신뢰: 한 번만 보내고 비운다.
        for (std::vector<uint8_t>& record : m_pendingUnreliable) {
            PacketWriter writer;
            writer.U8(kChannelUnreliable);
            writer.U16(static_cast<uint16_t>(record.size()));
            writer.WriteRaw(record);
            messages.push_back(writer.Take());
        }
        m_pendingUnreliable.clear();

        // 2) 보낼 것도 없고 ack도 밀리지 않았으면 아무것도 안 낸다.
        if (messages.empty() && !m_ackDirty) {
            return {};
        }

        // 3) 메시지들을 데이터그램으로 포장한다. 각 헤더에 누적 ack를 싣는다.
        std::vector<std::vector<uint8_t>> datagrams;
        auto startDatagram = []() {
            PacketWriter writer;
            return writer;
        };

        auto finalizeInto = [&](PacketWriter&& body) {
            PacketWriter header;
            header.U8(kReliabilityVersion);
            header.U32(m_expectedReliableSeq); // 누적 ack: 이 값 미만은 모두 순서대로 받음
            std::vector<uint8_t> datagram = header.Take();
            std::vector<uint8_t> bodyBytes = body.Take();
            datagram.insert(datagram.end(), bodyBytes.begin(), bodyBytes.end());
            datagrams.push_back(std::move(datagram));
        };

        constexpr std::size_t kHeaderSize = 1 + 4;
        PacketWriter current = startDatagram();
        std::size_t currentSize = 0;
        bool currentHasMessage = false;

        for (std::vector<uint8_t>& message : messages) {
            if (currentHasMessage && (kHeaderSize + currentSize + message.size()) > kMaxDatagramPayload) {
                finalizeInto(std::move(current));
                current = startDatagram();
                currentSize = 0;
                currentHasMessage = false;
            }
            current.WriteRaw(message);
            currentSize += message.size();
            currentHasMessage = true;
        }

        if (currentHasMessage) {
            finalizeInto(std::move(current));
        } else {
            // 메시지는 없지만 ack를 흘려보내야 하는 경우: ack-only 데이터그램.
            finalizeInto(startDatagram());
        }

        // 데이터그램이 하나라도 나갔으면 ack가 실려 나간 것이다.
        m_ackDirty = false;
        return datagrams;
    }

    void ReliabilityChannel::OnReceive(std::span<const uint8_t> datagram, std::vector<std::vector<uint8_t>>& outDelivered) {
        PacketReader reader(datagram);

        uint8_t version = 0;
        uint32_t ack = 0;
        if (!reader.U8(version) || version != kReliabilityVersion || !reader.U32(ack)) {
            return; // 우리 프로토콜이 아니거나 손상 → 폐기
        }

        // 헤더 ack로 내 신뢰 송신 버퍼를 정리한다.
        AckReliable(ack);

        // 메시지 레코드들을 끝까지 읽는다.
        while (reader.Remaining() > 0) {
            uint8_t channel = 0;
            if (!reader.U8(channel)) {
                break;
            }

            if (channel == kChannelReliable) {
                uint32_t sequence = 0;
                uint16_t fragmentIndex = 0;
                uint16_t fragmentCount = 0;
                uint16_t length = 0;
                std::vector<uint8_t> fragment;
                if (!reader.U32(sequence) || !reader.U16(fragmentIndex) ||
                    !reader.U16(fragmentCount) || !reader.U16(length) ||
                    !reader.ReadRaw(fragment, length)) {
                    break;
                }

                m_ackDirty = true;

                // 이미 순서대로 전달한 시퀀스면 중복 → 무시(단, ack는 갱신됨).
                if (sequence < m_expectedReliableSeq) {
                    continue;
                }
                if (fragmentCount == 0 || fragmentIndex >= fragmentCount) {
                    continue; // 손상
                }

                Reassembly& reassembly = m_reassembly[sequence];
                if (!reassembly.initialized) {
                    reassembly.initialized = true;
                    reassembly.fragmentCount = fragmentCount;
                    reassembly.fragments.resize(fragmentCount);
                    reassembly.have.assign(fragmentCount, false);
                } else if (reassembly.fragmentCount != fragmentCount) {
                    continue; // 조각 수 불일치 → 손상
                }

                if (!reassembly.have[fragmentIndex]) {
                    reassembly.fragments[fragmentIndex] = std::move(fragment);
                    reassembly.have[fragmentIndex] = true;
                    ++reassembly.receivedCount;
                }
            } else if (channel == kChannelSequenced) {
                uint16_t sequenceId = 0;
                uint16_t length = 0;
                std::vector<uint8_t> record;
                if (!reader.U16(sequenceId) || !reader.U16(length) || !reader.ReadRaw(record, length)) {
                    break;
                }

                // 최신만 전달. 구식/중복은 폐기.
                if (!m_hasSequenced || IsMoreRecent(sequenceId, m_lastSequencedId)) {
                    m_hasSequenced = true;
                    m_lastSequencedId = sequenceId;
                    outDelivered.push_back(std::move(record));
                }
            } else if (channel == kChannelUnreliable) {
                uint16_t length = 0;
                std::vector<uint8_t> record;
                if (!reader.U16(length) || !reader.ReadRaw(record, length)) {
                    break;
                }
                outDelivered.push_back(std::move(record));
            } else {
                break; // 알 수 없는 채널 → 이후 파싱 불가
            }
        }

        DeliverInOrder(outDelivered);
    }

    void ReliabilityChannel::AckReliable(uint32_t ack) {
        // 누적 ack: sequence < ack 인 신뢰 메시지는 상대가 순서대로 받았다.
        while (!m_outgoingReliable.empty() && m_outgoingReliable.front().sequence < ack) {
            m_outgoingReliable.pop_front();
        }
    }

    void ReliabilityChannel::DeliverInOrder(std::vector<std::vector<uint8_t>>& outDelivered) {
        // 다음 기대 시퀀스가 완성되어 있으면 순서대로 조립해 전달한다.
        for (;;) {
            auto it = m_reassembly.find(m_expectedReliableSeq);
            if (it == m_reassembly.end()) {
                break;
            }

            Reassembly& reassembly = it->second;
            if (!reassembly.initialized || reassembly.receivedCount != reassembly.fragmentCount) {
                break; // 아직 조각이 덜 왔다
            }

            std::vector<uint8_t> record;
            for (std::vector<uint8_t>& fragment : reassembly.fragments) {
                record.insert(record.end(), fragment.begin(), fragment.end());
            }
            outDelivered.push_back(std::move(record));

            m_reassembly.erase(it);
            ++m_expectedReliableSeq;
        }
    }

}
