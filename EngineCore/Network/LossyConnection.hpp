#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include "Network/Endpoint.hpp"
#include "Network/INetworkConnection.hpp"

namespace EF::Network {

    // 링크 열화(degradation) 파라미터.
    //
    // ⚠️ 기본값은 전부 "아무것도 하지 않음"이다. 실수로 켠 채 배포되는 사고를 막으려면
    //    기본 생성이 무해해야 한다. 셋 다 0이면 LossyConnection은 순수 통과(pass-through)로
    //    동작하고 난수조차 뽑지 않는다(RNG 상태가 전혀 움직이지 않는다).
    struct LossyLinkSettings {
        // 데이터그램을 버릴 확률 [0,1]. 1이면 전부 버린다.
        float lossRate = 0.0f;

        // 데이터그램을 한 번 더 보낼 확률 [0,1]. 복사본은 원본과 별개로 지연을 뽑으므로
        // 원본보다 늦게(또는 같은 tick에) 도착한다.
        float duplicateRate = 0.0f;

        // 지연 구간(틱). 두 값이 같으면 "고정 지연", 다르면 [min,max] 균등 분포 = 지터.
        // 지터가 있으면 재정렬은 저절로 따라온다(먼저 보낸 것이 더 큰 지연을 뽑을 수 있다).
        // 그래서 SimulatedNetwork처럼 별도 reorder 파라미터를 두지 않았다.
        uint32_t minDelayTicks = 0;
        uint32_t maxDelayTicks = 0;

        // 난수 시드. 같은 시드 + 같은 송신 순서 = 같은 손실/중복/지연 패턴.
        uint64_t seed = 0;
    };

    // 임의의 INetworkConnection 위에 손실·중복·지연을 주입하는 **데코레이터**.
    //
    // 왜 SimulatedNetwork로 안 되는가:
    //   SimulatedNetwork는 자기 자신이 허브인 인메모리 전송 구현이라, 이미 존재하는
    //   UDPConnection이나 MemoryConnection 위에 씌울 수가 없다. 즉 실행 파일로 띄운
    //   서버·클라이언트 사이에서는 못 쓴다. 이 클래스는 반대로 전송 구현을 고르지 않고
    //   "무엇이든 하나 감싸서" 같은 인터페이스를 다시 내놓는다. 그래서 GameClient가
    //   UDPConnection을 이걸로 한 겹 감싸기만 하면 실제 소켓 경로에 손실을 넣을 수 있다.
    //
    // 어느 방향에 주입하는가 — **송신 경로에만** 넣는다(SendTo). ReceiveFrom은 통과다.
    //   버린 대안: 수신 경로에서 버리기. 앱 관점 효과는 비슷하지만 "이미 받은 것을 버린다"라
    //   실제 링크의 모델과 어긋나고(대역폭·ack 타이밍 해석이 미묘하게 달라진다),
    //   무엇보다 한쪽만 감싸도 양방향이 죽어버려 방향별로 다른 손실률을 줄 수 없다.
    //   양방향을 열화시키고 싶으면 **양 끝을 각각 감싼다** — 비대칭 링크(업만 나쁜 회선)를
    //   재현할 수 있어 표현력이 더 좋다.
    //
    // 지연을 왜 시간이 아니라 tick으로 다루는가:
    //   INetworkConnection에는 tick 개념이 없다. 세 가지 중에 골랐다.
    //     (1) wall-clock(steady_clock) — 실제 지연에 가장 가깝다. 버렸다. 테스트가 진짜로
    //         sleep해야 하고, 스케줄러에 따라 결과가 흔들린다. 이 클래스의 존재 이유가
    //         "사람 개입 없이 재현 가능한 복구 검증"인데 재현성을 깨는 선택은 자기모순이다.
    //     (2) ReceiveFrom() 호출 횟수 = 논리 시계 — 외부 펌프가 필요 없어 편하다. 버렸다.
    //         "폴링 횟수"는 단위로서 의미가 없고, 수신을 안 하는 tick에는 시계가 멈춰
    //         "3틱 뒤 도착"이라는 문장이 참이 아니게 된다.
    //     (3) **채택** — 논리 tick을 외부에서 주입한다(Advance(tick)). 엔진의 네트워크
    //         펌프는 이미 전부 tick을 받는 형태다(NetworkManager::Update(tick)). 호출자가
    //         이미 손에 들고 있는 값이라 새 개념이 늘지 않고, 고정 timestep 루프 위에서
    //         "지연 = N틱"이 곧 "지연 = N * 20ms"로 정확히 읽힌다.
    //
    // ⚠️ 함정: 지연을 켜 놓고 Advance(tick)을 안 부르면 지연 큐가 **영원히 안 풀린다.**
    //    NetworkManager::Update(tick) 바로 옆에서 같이 불러라. 지연이 0이면 큐 자체를
    //    쓰지 않으므로 이 함정에 걸리지 않는다(기본값이 무해한 또 다른 이유).
    //
    // ⚠️ 여기 float와 std::mt19937_64가 있는 것은 **결정론 규칙 위반이 아니다.**
    //    이 난수는 전송 계층의 링크 열화용이지 시뮬레이션 상태를 만들지 않는다. 어떤 peer도
    //    이 값으로 월드를 바꾸지 않는다 — 바뀌는 것은 "패킷이 도착했는가"뿐이고, 그건
    //    원래 네트워크가 마음대로 정하는 것이다. 그래서 Fixed64 규칙의 대상이 아니다.
    //    (반대로 시뮬레이션 경로에 이 클래스를 끌어들이면 그 순간 규칙 위반이 된다.)
    //
    // ⚠️ 스레드 안전하지 않다. 재현성의 정의 자체가 "난수를 소비하는 순서"라서, 여러
    //    스레드가 동시에 SendTo를 부르면 시드가 같아도 패턴이 달라진다. 뮤텍스를 붙여도
    //    순서는 못 고정하므로 아예 붙이지 않았다. 펌프 스레드 하나에서만 써라.
    class LossyConnection final : public INetworkConnection {
    public:
        // inner는 널이 아니어야 한다. 소유권을 공유한다(NetworkManager도 shared_ptr를 쓴다).
        LossyConnection(std::shared_ptr<INetworkConnection> inner, const LossyLinkSettings& settings);

        // 주입 후 inner로 넘긴다.
        //  - 버려졌으면: inner를 부르지 않고 true를 돌려준다. 소켓 관점에서 송신은 성공했고
        //    패킷은 "회선에서" 사라진 것이다. 여기서 false를 돌려주면 상위 계층이 송신 실패로
        //    오해해 재시도 로직이 이중으로 걸린다.
        //  - 지연되었으면: 큐에 넣고 true. 즉 지연이 켜져 있으면 반환값의 의미가
        //    "보냈다"가 아니라 "받아들였다"로 바뀐다. 나중의 실제 전송 실패는
        //    InnerFailureCount()로만 관찰할 수 있다.
        bool SendTo(const Endpoint& target, std::span<const uint8_t> message) override;

        // 통과. 수신 경로에는 아무것도 주입하지 않는다(위 설계 근거 참조).
        std::optional<Datagram> ReceiveFrom() override;

        [[nodiscard]] Endpoint DefaultPeer() const override;

        // 논리 시계를 tick으로 옮기고, 도착 시각이 된 지연 데이터그램을 inner로 흘려보낸다.
        // 매 fixed tick 불러야 한다. 되감기(tick < 현재)는 무시한다.
        void Advance(uint64_t tick);

        // 아직 회선 위에 떠 있는(지연 중인) 데이터그램 수. 0이 아닌데 안 줄어들면
        // Advance를 안 부르고 있다는 뜻이다.
        [[nodiscard]] std::size_t PendingDelayedCount() const noexcept {
            return m_delayed.size();
        }

        [[nodiscard]] uint64_t DroppedCount() const noexcept { return m_dropped; }
        [[nodiscard]] uint64_t DuplicatedCount() const noexcept { return m_duplicated; }
        [[nodiscard]] uint64_t ForwardedCount() const noexcept { return m_forwarded; }
        [[nodiscard]] uint64_t InnerFailureCount() const noexcept { return m_innerFailures; }

        [[nodiscard]] uint64_t CurrentTick() const noexcept { return m_currentTick; }

        [[nodiscard]] const LossyLinkSettings& Settings() const noexcept { return m_settings; }

        // 런타임 조정(콘솔 명령·디버그 UI용). 이미 큐에 든 지연 데이터그램의 도착 시각은
        // 바뀌지 않는다 — 이미 회선에 올라간 패킷이므로.
        // 시드는 다시 심는다: 파라미터를 바꾼 시점부터 새 재현 구간이 시작된다고 본다.
        void SetSettings(const LossyLinkSettings& settings);

        [[nodiscard]] INetworkConnection& Inner() noexcept { return *m_inner; }

    private:
        struct DelayedDatagram {
            Endpoint target;
            std::vector<uint8_t> bytes;
            uint64_t releaseTick = 0;
        };

        // 파라미터가 전부 0인가. true면 SendTo가 난수도 큐도 건드리지 않고 곧장 통과한다.
        [[nodiscard]] bool IsPassthrough() const noexcept;

        // rate 확률로 true. rate <= 0이면 **난수를 소비하지 않는다**(pass-through 보장의 핵심).
        bool Roll(float rate);

        // [min,max]에서 지연 틱을 뽑는다. min == max면 난수를 소비하지 않는다 —
        // 그래야 "고정 지연을 켜도 손실 패턴은 그대로"가 성립해서 변수를 하나씩 격리할 수 있다.
        uint32_t DrawDelayTicks();

        void Emit(const Endpoint& target, std::span<const uint8_t> message, uint32_t delayTicks);
        void ReleaseDue();

        std::shared_ptr<INetworkConnection> m_inner;
        LossyLinkSettings m_settings;

        // 표준이 수열을 못 박고 있는 엔진이라 컴파일러가 달라도 같은 시드 = 같은 수열이다.
        // (std::uniform_real_distribution은 그 보장이 없어서 쓰지 않는다 — .cpp 주석 참조)
        std::mt19937_64 m_rng;

        std::vector<DelayedDatagram> m_delayed;
        uint64_t m_currentTick = 0;

        uint64_t m_dropped = 0;
        uint64_t m_duplicated = 0;
        uint64_t m_forwarded = 0;
        uint64_t m_innerFailures = 0;
    };

}
