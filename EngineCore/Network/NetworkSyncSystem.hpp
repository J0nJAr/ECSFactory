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

    private:
        void UpdateClient(World& world, NetworkManager& network);
        void UpdateServer(World& world, NetworkManager& network, uint64_t tick);
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
