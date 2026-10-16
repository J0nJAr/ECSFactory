#include "LossyConnection.hpp"

#include <algorithm>
#include <utility>

#include "Base/Assert.hpp"

namespace EF::Network {
    namespace {
        // 확률 비교용 스케일 = 2^53.
        //
        // std::uniform_real_distribution / uniform_int_distribution을 쓰지 않는 이유:
        // 표준은 이들이 "어떤 수열을 내는지"를 구현 간에 못 박지 않는다. 엔진(mt19937_64)의
        // raw 출력 수열만 표준이 고정한다. 재현성이 이 클래스의 존재 이유이므로,
        // 컴파일러/표준 라이브러리가 바뀌어도 같은 시드가 같은 패턴을 내도록 raw 출력을
        // 직접 비교한다. 상위 53비트만 쓰는 것은 double이 정확히 표현할 수 있는 정수 범위
        // 안에 머물러 rate * kProbabilityScale이 반올림 없이 계산되게 하기 위함이다.
        constexpr double kProbabilityScale = 9007199254740992.0; // 2^53

        float ClampRate(float rate) noexcept {
            if (rate < 0.0f) {
                return 0.0f;
            }
            if (rate > 1.0f) {
                return 1.0f;
            }
            return rate;
        }

        LossyLinkSettings Sanitize(const LossyLinkSettings& settings) {
            LossyLinkSettings sanitized = settings;
            sanitized.lossRate = ClampRate(sanitized.lossRate);
            sanitized.duplicateRate = ClampRate(sanitized.duplicateRate);

            // min > max로 부르는 건 오타일 가능성이 크다. 조용히 뒤집어 의도를 살린다.
            if (sanitized.minDelayTicks > sanitized.maxDelayTicks) {
                std::swap(sanitized.minDelayTicks, sanitized.maxDelayTicks);
            }
            return sanitized;
        }
    }

    LossyConnection::LossyConnection(std::shared_ptr<INetworkConnection> inner, const LossyLinkSettings& settings)
        : m_inner(std::move(inner)), m_settings(Sanitize(settings)), m_rng(settings.seed) {
        EF_ASSERT(m_inner != nullptr, "LossyConnection은 감쌀 연결이 있어야 한다");
    }

    bool LossyConnection::IsPassthrough() const noexcept {
        return m_settings.lossRate <= 0.0f
            && m_settings.duplicateRate <= 0.0f
            && m_settings.maxDelayTicks == 0;
    }

    bool LossyConnection::Roll(float rate) {
        if (rate <= 0.0f) {
            // 난수를 소비하지 않는다. 이 덕분에 "손실만 켠 설정"과 "손실 + 고정 지연 설정"이
            // 완전히 같은 손실 패턴을 낸다 — 원인 하나씩 격리해서 실험할 수 있다.
            return false;
        }
        // rate == 1.0이면 임계값이 2^53이고 (m_rng() >> 11)은 항상 그 미만이라 언제나 true다.
        const uint64_t threshold = static_cast<uint64_t>(static_cast<double>(rate) * kProbabilityScale);
        return (m_rng() >> 11) < threshold;
    }

    uint32_t LossyConnection::DrawDelayTicks() {
        if (m_settings.minDelayTicks == m_settings.maxDelayTicks) {
            return m_settings.minDelayTicks;
        }
        const uint32_t span = m_settings.maxDelayTicks - m_settings.minDelayTicks + 1;
        // 모듈로 편향이 있지만 링크 열화용이라 무해하다(span은 작고, 균등성보다 재현성이 중요).
        return m_settings.minDelayTicks + static_cast<uint32_t>(m_rng() % span);
    }

    void LossyConnection::Emit(const Endpoint& target, std::span<const uint8_t> message, uint32_t delayTicks) {
        if (delayTicks == 0) {
            if (m_inner->SendTo(target, message)) {
                ++m_forwarded;
            }
            else {
                ++m_innerFailures;
            }
            return;
        }

        DelayedDatagram delayed;
        delayed.target = target;
        delayed.bytes.assign(message.begin(), message.end());
        delayed.releaseTick = m_currentTick + delayTicks;
        m_delayed.push_back(std::move(delayed));
    }

    void LossyConnection::ReleaseDue() {
        if (m_delayed.empty()) {
            return;
        }

        // 삽입 순서로 훑으면서 도착 시각이 된 것만 내보낸다. 지터가 있으면 먼저 넣은 것이
        // 나중에 나갈 수 있고, 그게 곧 재정렬이다(의도된 부수효과).
        std::size_t writeIndex = 0;
        for (std::size_t readIndex = 0; readIndex < m_delayed.size(); ++readIndex) {
            DelayedDatagram& entry = m_delayed[readIndex];
            if (entry.releaseTick <= m_currentTick) {
                if (m_inner->SendTo(entry.target, entry.bytes)) {
                    ++m_forwarded;
                }
                else {
                    ++m_innerFailures;
                }
                continue;
            }
            if (writeIndex != readIndex) {
                m_delayed[writeIndex] = std::move(entry);
            }
            ++writeIndex;
        }
        m_delayed.resize(writeIndex);
    }

    bool LossyConnection::SendTo(const Endpoint& target, std::span<const uint8_t> message) {
        if (IsPassthrough()) {
            // 무해한 기본값 경로. 난수도 큐도 건드리지 않는다.
            const bool sent = m_inner->SendTo(target, message);
            if (sent) {
                ++m_forwarded;
            }
            else {
                ++m_innerFailures;
            }
            return sent;
        }

        // 1) 손실. 버렸어도 true를 돌려준다 — 소켓은 성공했고 회선이 먹은 것이다.
        if (Roll(m_settings.lossRate)) {
            ++m_dropped;
            return true;
        }

        // 2) 원본 전송(또는 지연 큐 투입).
        Emit(target, message, DrawDelayTicks());

        // 3) 중복본. 지연을 따로 뽑으므로 원본과 다른 시각에 도착할 수 있다.
        if (Roll(m_settings.duplicateRate)) {
            ++m_duplicated;
            Emit(target, message, DrawDelayTicks());
        }

        return true;
    }

    std::optional<Datagram> LossyConnection::ReceiveFrom() {
        return m_inner->ReceiveFrom();
    }

    Endpoint LossyConnection::DefaultPeer() const {
        return m_inner->DefaultPeer();
    }

    void LossyConnection::Advance(uint64_t tick) {
        // 되감기는 무시한다. 지연 큐의 releaseTick이 절대 tick이라 시계를 되돌리면
        // 이미 도착했어야 할 패킷이 다시 회선으로 돌아가는 셈이 되어 해석이 깨진다.
        EF_ASSERT(tick >= m_currentTick, "LossyConnection::Advance는 뒤로 갈 수 없다");
        if (tick < m_currentTick) {
            return;
        }
        m_currentTick = tick;
        ReleaseDue();
    }

    void LossyConnection::SetSettings(const LossyLinkSettings& settings) {
        m_settings = Sanitize(settings);
        m_rng.seed(settings.seed);
    }

}
