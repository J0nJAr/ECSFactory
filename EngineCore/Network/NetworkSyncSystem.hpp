#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <vector>

#include "ECS/ISystem.hpp"
#include "ECS/World.hpp"
#include "Network/NetworkManager.hpp"

namespace EF {

    enum class NetworkSyncMode {
        Client,
        Server
    };

    // 동기화 상태 머신. 클라이언트와 서버 두 역할을 한 타입이 겸한다(NetworkSyncMode).
    //
    // 책임:
    //  - 접속 handshake: Hello → Welcome → TypeTable → WorldSnapshot
    //  - 서버: 매 tick 입력을 확정하고(ConfirmInputs) 서버 월드에 적용한 뒤,
    //          확정셋을 InputFrame으로 전 클라에 재전파하고 TickHash를 방송
    //  - 클라: InputFrame을 tick 순서대로 버퍼링했다가 clientSimTick+1부터 적용,
    //          TickHash로 디싱크를 검사하고 어긋나면 ResyncRequest
    //  - 클라: 서버 tick 추정, 입력 지연(kInputDelayTicks), 입력 이중화 재전송
    //
    // 보장:
    //  - 입력은 "확정된 tick"에만 적용된다. 도착하는 대로 적용하지 않는다.
    //  - InputFrame은 reliable-ordered라 구멍이 나면 그 tick에서 멈춘다(재전송으로 채워짐).
    //  - TickHash 비교는 "그 tick을 시뮬레이션한 순간의" 로컬 해시와 한다.
    //    프레임이 몰아서 적용되어도 허위 불일치가 나지 않는다.
    //
    // 이 타입이 하지 않는 일:
    //  - 예측이나 롤백을 하지 않는다. 클라는 입력 지연 + RTT만큼 뒤처진다.
    //  - 소켓·재전송을 직접 다루지 않는다(NetworkManager와 ReliabilityChannel의 몫).
    //
    // ⚠️ 클라 쪽 시뮬레이션은 엔진의 tick 번호가 아니라 확정 프레임(clientSimTick)이
    //    구동한다. 그래서 클라 이동을 자동 실행되는 ISystem으로 등록하면 안 된다 —
    //    적용 순서를 통제하기 위해 이 시스템이 명시적으로 돌린다.
    class NetworkSyncSystem final : public ISystem {
    public:
        explicit NetworkSyncSystem(NetworkSyncMode mode);

        void Initialize(World& world) override;
        void FixedUpdate(World& world, Fixed64 fixedDeltaTime, uint64_t tick) override;
        void FrameUpdate(World& world, float frameDeltaSeconds) override;
        void Shutdown(World& world) override;

        [[nodiscard]] bool HasTypeTable() const noexcept {
            return m_hasTypeTable;
        }

        [[nodiscard]] bool HasWelcome() const noexcept {
            return m_hasWelcome;
        }

        [[nodiscard]] bool HasSnapshot() const noexcept {
            return m_hasSnapshot;
        }

        [[nodiscard]] uint64_t LastSnapshotTick() const noexcept {
            return m_lastSnapshotTick;
        }

        // 지금까지 실제로 적용한 스냅샷 수.
        //
        // LastSnapshotTick()의 변화로 세면 **같은 서버 tick에 만들어진 스냅샷 여러 개가
        // 한 건으로 뭉개진다.** resync 폭주를 재려면 그 구분이 정확히 필요해서 따로 센다.
        [[nodiscard]] uint64_t AppliedSnapshotCount() const noexcept {
            return m_appliedSnapshotCount;
        }

        [[nodiscard]] bool HasHashMismatch() const noexcept {
            return m_hasHashMismatch;
        }

        [[nodiscard]] TickHash LastRemoteTickHash() const noexcept {
            return m_lastRemoteTickHash;
        }

        [[nodiscard]] uint64_t ClientSimTick() const noexcept {
            return m_clientSimTick;
        }

        [[nodiscard]] uint64_t ServerTickEstimate() const noexcept {
            return m_serverTickEstimate;
        }

        void QueueLocalInput(const InputCommand& command);

        /**
         * ⚠️ **디버그 전용 — 디싱크를 일부러 만든다. 게임 코드에서 부르지 말 것.**
         *
         * 다음에 적용될 확정 InputFrame **한 개**를 "받았지만 적용하지 않고" 넘긴다.
         * tick 번호(`clientSimTick`)는 정상적으로 진행시킨다 — 여기서 멈추면 디싱크가
         * 아니라 그냥 뒤처진 클라이언트가 되어, 검증하려는 상황이 재현되지 않는다.
         *
         * **왜 이런 게 필요한가.** `TickHash` 불일치 → `ResyncRequest` → `WorldSnapshot`
         * 복구 경로는 정상 세션에서 절대 밟히지 않는다. 밟히지 않는 코드는 조용히 썩고,
         * 정작 필요한 날(실전 디싱크) 처음 실행된다. 그래서 테스트가 디싱크를 만들어낼
         * 수 있어야 한다.
         *
         * **왜 하필 "확정 프레임 하나 건너뛰기"인가.** 현실의 lockstep 디싱크는 대부분
         * "한쪽이 확정 입력을 놓쳤거나 다르게 적용했다"이다. 이 주입이 만드는 클라 월드는
         * 여전히 *시뮬레이션이 만들어낼 수 있는* 상태이면서 서버와 한 스텝 어긋나 있고,
         * 그 뒤로도 어긋난 채 정상 진행한다. 컴포넌트 값을 아무 숫자로 덮는 것보다 실제
         * 버그에 가깝다.
         *
         * **버린 대안:**
         *  - `#ifdef EF_DEBUG_DESYNC` 컴파일타임 훅 — 테스트가 특수 빌드에서만 돌게 되고,
         *    CI의 Release 빌드에서는 복구 경로가 검증되지 않는다. 확인하고 싶은 게 정확히
         *    "평소 빌드에서도 이 경로가 살아 있다"인데 그걸 놓친다.
         *  - 테스트에서 이 클래스를 상속해 적용 루프를 오버라이드 — 적용 경로가 private·비가상
         *    이라 가상 함수를 새로 뚫어야 한다. 훅 하나보다 프로덕션 표면을 더 늘린다.
         *  - 테스트가 클라 월드의 컴포넌트 값을 직접 덮기 — 이미
         *    `Network.SyncSystemRequestsAndAppliesResyncOnHashMismatch`가 그렇게 하고 있고
         *    그건 그대로 둔다. 다만 "값이 튄" 월드는 시뮬레이션이 도달할 수 없는 상태라,
         *    가장 흔한 형태인 "입력 하나를 놓친 디싱크"를 대신하지 못한다.
         *
         * **실수로 켜지지 않게 하는 장치:**
         *  - 기본값이 꺼짐이고, 이 함수를 명시적으로 부르지 않으면 절대 켜지지 않는다.
         *  - 한 번 발동하면 스스로 꺼진다(1회성). 켜 둔 걸 잊어서 세션 전체가 망가지는
         *    사고가 나지 않는다.
         *  - 클라이언트 모드에서만 의미가 있다(확정 프레임 적용은 클라 쪽 경로다).
         *    서버 모드에서 불러도 아무 일도 일어나지 않는다.
         *
         * 주의: 건너뛴 프레임의 확정 입력이 **월드를 실제로 바꾸지 않으면**(예: buttonMask가
         * 전부 0) 디싱크가 생기지 않는다. 이 함수를 쓰는 테스트는 주입 직후 해시가 실제로
         * 갈라졌는지를 함께 단언해야 한다. 아니면 "아무 일도 안 일어나서" 통과한다.
         */
        void DebugSkipNextConfirmedInputFrameOnce() noexcept {
            m_debugSkipNextConfirmedFrame = true;
        }

    private:
        void UpdateClient(World& world, NetworkManager& network);
        void UpdateServer(World& world, NetworkManager& network, uint64_t tick);
        void DropTimedOutClients(World& world, NetworkManager& network, uint64_t tick);
        void SendQueuedLocalInputs(NetworkManager& network, uint64_t tick);
        void ApplyBufferedInputFrames(World& world, NetworkManager& network);
        void VerifyPendingTickHashes(NetworkManager& network);

        // 지금 시뮬레이션이 끝난 tick의 해시를 기록하고, 오래된 항목을 잘라낸다.
        void RecordLocalHash(World& world, uint64_t tick);

        void HandlePacketOnClient(World& world, NetworkManager& network, const Network::NetworkPacket& packet);
        void HandlePacketOnServer(World& world, NetworkManager& network, const Network::Endpoint& from, const Network::NetworkPacket& packet, uint64_t tick);

    private:
        struct RedundantInput {
            InputCommand command;
            uint32_t remainingSends = 0;
        };

        NetworkSyncMode m_mode;
        bool m_sentHello = false;
        bool m_hasWelcome = false;
        bool m_requestedSnapshot = false;
        bool m_hasTypeTable = false;
        bool m_hasSnapshot = false;
        bool m_hasHashMismatch = false;
        bool m_hasServerTickEstimate = false;

        // 디버그 주입 플래그. DebugSkipNextConfirmedInputFrameOnce()만이 이걸 켠다.
        // 켜진 상태로 남지 않는다 — 다음 확정 프레임 하나를 건너뛰면서 스스로 꺼진다.
        bool m_debugSkipNextConfirmedFrame = false;

        // ResyncRequest를 하나만 띄워 두기 위한 플래그.
        // 디싱크가 나면 그 뒤 tick들도 줄줄이 어긋나는데, 그때마다 요청하면 서버가
        // 요청마다 월드 전체 스냅샷을 reliable로 답해서 대역폭이 무너진다.
        // 스냅샷을 적용하면 내려간다(VerifyPendingTickHashes / WorldSnapshot 처리 참조).
        bool m_resyncRequestInFlight = false;
        uint64_t m_appliedSnapshotCount = 0;
        uint64_t m_lastSnapshotTick = 0;
        uint64_t m_clientSimTick = 0;
        uint64_t m_serverTickEstimate = 0;
        uint32_t m_nextInputSequence = 1;
        TickHash m_lastRemoteTickHash{};
        std::deque<InputCommand> m_localInputs;
        std::deque<RedundantInput> m_redundantInputs;
        std::map<uint64_t, std::vector<InputCommand>> m_frameBuffer;
        std::map<uint64_t, uint64_t> m_pendingTickHashes;

        // tick별 로컬 월드 해시. TickHash 비교의 기준이 된다.
        //
        // 왜 "현재" 해시를 그때그때 계산하면 안 되나:
        // ApplyBufferedInputFrames는 밀린 프레임을 한 번에 몰아 적용하므로
        // clientSimTick이 한 FixedUpdate에 여러 칸 점프할 수 있다. 그 상태에서
        // tick T의 원격 해시를 현재(T+n) 월드와 비교하면 정상인데도 불일치가 난다.
        // 그래서 각 tick을 시뮬레이션한 "그 순간"의 해시를 남겨 두고 tick으로 맞춰 본다.
        std::map<uint64_t, uint64_t> m_localHashByTick;
        std::vector<ComponentTypeDescriptor> m_remoteTypes;
    };

}
