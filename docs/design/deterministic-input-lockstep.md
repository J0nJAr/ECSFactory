# 결정론 입력 lockstep — 설계 & 구현 핸드오프

> 대상 독자: 이 작업을 **이어서 구현할 에이전트/개발자**. 블로그(서술형)가 아니라 **왜 이렇게 하고, 어떻게 이어가는지**를 담은 실행용 스펙이다.
> 상태: **M1~M7 완료.** (아래 "마일스톤" 참조)
> 관련 문서: `docs/blog/07_reliable-udp-transport.md`(전송 계층), `docs/blog/06_network-sync-system.md`(동기화 상태머신). 이 문서는 그 위에 올라가는 **입력/시뮬레이션 계층**을 다룬다.

---

## 0. TL;DR (에이전트가 먼저 알 것)

- 목표: **결정론 lockstep + 입력 릴레이**. 상태를 스트리밍하지 않고 **입력을 주고받아 모두가 같은 시뮬레이션**을 돌린다.
- 흐름: `클라 → 서버`(미래 tick 입력 제출, unreliable+이중화) → `서버`(그 tick 입력 확정: 결측은 직전입력 hold로 대체) → `서버 → 전 클라`(확정 입력셋 `InputFrame`을 **reliable**로 재전파) → **서버·모든 클라가 같은 확정 입력으로 결정론 시뮬레이션** → `TickHash`로 검증, 어긋나면 `WorldSnapshot`으로 resync.
- 이미 있는 것(재활용): 전송 계층(`ReliabilityChannel` reliable/sequenced/unreliable), `WorldSnapshot`(조인/resync), `TickHash`(디싱크 감지), endpoint 라우팅.
- 새로 만든 것: 결정론 이동 시스템(M1✅), `InputFrame` 릴레이(M2✅), 클라 로컬 시뮬레이션(M3✅), 클럭 추정+입력지연+이중화(M4✅).
- **불변식(§2)을 절대 깨지 말 것.** 특히: 시뮬레이션은 Fixed64만, 상태에 영향 주는 순회는 결정론 순서(clientId 정렬), 입력은 "확정 tick"에 적용.

---

## 1. 왜 이 설계인가 (결정 배경)

### 1.1 엔진 전제
- 서버 권위 + **결정론 시뮬레이션**. 같은 엔진 코드가 3가지로 실행(dedicated / remote client / host·single). `CLAUDE.md` 참조.
- 결정론 수학: `Fixed64`(Q32.32), raw 정수로 직렬화 → 머신 간 비트 일치.

### 1.2 두 가지 멀티플레이 모델과 선택
| 모델 | 방식 | 결정론 엔진과의 관계 |
|---|---|---|
| 서버 권위 + 스냅샷 스트리밍 | 서버만 시뮬, 상태를 스냅샷으로 내려줌 | 결정론이 필수는 아님. 대역폭 큼 |
| **결정론 lockstep + 입력 릴레이** (채택) | 입력만 주고받고 전원이 같은 시뮬 | **결정론의 존재 이유.** 대역폭 작음 |

**왜 lockstep인가:** 이 엔진이 `Fixed64`·`TickHash`·resync를 갖춘 이유가 바로 이것이다 — 상태가 아니라 입력을 공유하고, 모두가 같은 상태를 *계산*한다. (과거 코드 리뷰에서 "현재 코드는 사실 스냅샷 스트리밍 쪽에 반쯤 걸쳐 있다"고 확인됨. 이 작업은 그걸 lockstep으로 정렬하는 것.)

### 1.3 기존 조각 → lockstep 역할 매핑
| 기존 조각 | lockstep 역할 | 상태 |
|---|---|---|
| `InputCommand`(클라→서버) | 미래 tick 입력 제출 | 있음(전송) |
| **`InputFrame`(서버→전 클라)** | 확정 입력셋 재전파("OK") | **M2 완료** |
| 결정론 시뮬(`ApplyConfirmedInputs`) | 같은 입력→같은 상태 | **M1 완료** |
| `TickHash`(sequenced) | 디싱크 감지 | 있음 |
| `WorldSnapshot`(reliable) | 조인 + resync | 있음 |
| endpoint↔clientId | 라우팅·인가 | 있음(07) |

### 1.4 확정된 결정(기본값)과 근거
- **결측 입력 = 직전 입력 hold 후 릴레이(옵션 A).** stall(옵션 B)은 한 명 렉에 전원 정지라 기각. 서버가 대체값을 확정셋에 넣어 전파 → 전원 일치. *주의: hold는 이동엔 맞지만 엣지형 행동입력(발사)엔 틀리다 → §2-(4) 참조.*
- **입력 지연 = 2 tick**, **이중화 창 = 최근 3 tick**, **다중 클라 적용 = clientId 정렬**, **서버 입력 저장 = applyTick 슬롯 맵**(deque 아님), **dt = 정수 유도**(FromDouble 금지).
- **`InputFrame`은 reliable-ordered** — lockstep 커맨드 스트림이라 하나라도 빠지면 그 클라 시뮬이 그 tick에서 멈춰야 한다(재전송으로 채워짐). 클라→서버 `InputCommand`는 unreliable+이중화(서버가 결측 시 대체).

---

## 2. 불변식 (이 계층을 건드리는 모든 코드가 지켜야 함)

1. **시뮬레이션 경로는 Fixed64만.** float/double 금지. 값 생성은 `FromInt`/`FromRaw`, 와이어는 `Raw()`(WriteFixed64). `FromFloat`/`FromDouble`/`ToFloat`는 init/렌더/디버그 전용.
2. **상태에 영향 주는 순회는 결정론 순서.** `std::unordered_map`(예: `NetworkManager::m_clients`)을 **그대로 순회해 상태를 만들면 안 된다** — clientId로 정렬 후 처리. (TickHash 브로드캐스트처럼 순서 무관한 곳은 예외.)
3. **입력은 "확정 tick"에 적용.** "도착하는 대로" 적용 금지. 서버는 tick T의 확정셋을 만들고, 클라는 그 확정셋(`InputFrame`)을 받아 tick T에 적용. 서버·전 클라가 **같은 tick에 같은 입력**을 넣어야 한다.
4. **결측 입력은 서버가 대체 후 릴레이.** 대체값이 확정셋에 포함되어 전파 → 아무도 임의 판단 안 함. (현재 대체 = 직전 입력 hold. **행동 입력 비트가 추가되면 그 비트는 반복하면 안 됨** — hold는 이동/aim에만.)
5. **`InputFrame` = reliable-ordered.** 빠지면 그 클라는 그 tick 시뮬 보류(채워질 때까지). 순서대로만 적용.
6. **`TickHash`는 검증용.** 불일치 → resync(스냅샷). 스냅샷은 **조인/resync 전용**, 매 tick 상태 스트리밍 아님.
7. **클럭:** 같은 `GameEngine`의 월드들은 tick 공유(host/single) — [GameEngine.cpp `ManualStepFixed`](../../EngineCore/Base/GameEngine.cpp)의 `m_currentTick`. 분리 프로세스(dedicated)는 각자 클럭 → 클라가 서버 tick을 추정해야 함(M4).

---

## 3. 데이터 흐름

```
[클라 tick t]  로컬 입력 수집
   applyTick = 추정서버tick + 2 (입력지연)
   InputCommand(applyTick, buttons, aim)  ── unreliable, 최근 3개 함께(이중화) ──▶  서버

[서버 tick T]  수신 입력을 client별 슬롯맵[applyTick]에 적재(빈 슬롯만)
   confirmed = ConfirmInputs(T)            // client정렬, 결측=직전 hold, slot<=T 정리
   ApplyConfirmedInputs(serverWorld, confirmed)   // 서버 로컬 시뮬
   각 client endpoint로 InputFrame(T, confirmed) ── reliable ──▶ 전 클라
   TickHash(T, serverWorld.ComputeHash()) ── sequenced ──▶ 전 클라

[클라]  InputFrame 순서대로 수신 → clientSimTick+1 프레임이 있으면
   ApplyConfirmedInputs(clientWorld, frame.inputs); clientSimTick++
   TickHash 수신 → localHash 비교, 불일치 & hasSnapshot → ResyncRequest
```

핵심: 서버가 **먼저** 확정·적용·릴레이하고, 클라는 릴레이를 받아 **뒤따라** 같은 tick을 시뮬(예측/rollback 없는 최소판이라 클라는 delay+RTT만큼 뒤처짐 — 예측은 향후 과제).

---

## 4. 마일스톤 (상태 + 어떻게)

### M1 — 결정론 이동 원시 ✅ 완료
- `Network/InputCommand.hpp` — `InputCommand`/`TickHash`를 NetworkManager.hpp에서 분리(gameplay가 네트워크 전체에 의존 안 하게).
- `Components/PlayerControl.hpp` — `struct PlayerControl { uint32_t clientId; }`(trivially copyable, 스냅샷 전파용).
- `Gameplay/PlayerMovement.{hpp,cpp}` — `void ApplyConfirmedInputs(World&, std::span<const InputCommand>)`, `kInputMove{Right,Left,Forward,Back}` 비트. buttonMask→Transform.position, 순수 Fixed64(tick당 1유닛).
- 테스트 `tests/GameplayTests.cpp`(4개): 이동 결정성, 상쇄, **두 월드 동일 입력→동일 해시**, clientId 매칭.
- **불변식 근거:** 이동이 입력만의 함수 + Fixed64라, 서버·클라가 같은 확정셋을 넣으면 `ComputeHash()` 일치.

### M2 — 서버 확정 + reliable 릴레이 (완료)
**구현한 것:**
1. `Network/Packet.hpp`: `PacketType::InputFrame = 9` 추가.
2. `Network/NetworkManager.*`:
   - `struct InputFrame { uint64_t tick; std::vector<InputCommand> inputs; };` (InputCommand.hpp 또는 여기).
   - Encode/Decode: `[tick u64][count u32][InputCommand × count]`. 항목 직렬화는 기존 `EncodeInputCommand` 필드 순서 재사용.
   - `bool SendInputFrame(const InputFrame&, const Endpoint& target)`. `DeliveryClassFor(InputFrame)`는 default→**Reliable**(확인만; TickHash/InputCommand 케이스에 안 걸리게).
   - **`NetworkClientState` 입력 저장 교체:** `std::deque pendingInputs` + `lastInputSequence`/`hasInput` → `std::map<uint64_t, InputCommand> inputByTick;`, `InputCommand lastConfirmed; bool hasConfirmed; uint64_t lastConfirmedTick;` (`#include <map>`).
   - `EnqueueInput(cmd)`: `cmd.tick > lastConfirmedTick`이고 슬롯이 비었을 때만 `inputByTick.try_emplace(cmd.tick, cmd)`. 반환 = 새로 넣었나(이중화 재전송 자연 dedup).
   - `std::vector<InputCommand> ConfirmInputs(uint64_t tick)`: **clientId 정렬** 순회 → 각 client: `inputByTick[tick]` 있으면 그것, 없고 `hasConfirmed`면 `lastConfirmed`(tick만 갱신, hold), 없으면 중립(clientId만). `lastConfirmed`/`lastConfirmedTick` 갱신, `inputByTick`에서 `<= tick` 슬롯 정리. 결과는 clientId 정렬된 확정셋.
   - `ConsumePendingInputs`/`PendingInputs`(deque) **제거**.
3. `Network/NetworkSyncSystem.cpp` `UpdateServer(tick)`: 패킷 수신 후 →
   ```
   auto confirmed = network.ConfirmInputs(tick);
   ApplyConfirmedInputs(world, confirmed);           // 서버 로컬 시뮬
   for each client with valid endpoint:
       network.SendInputFrame({tick, confirmed}, state.endpoint);   // reliable 릴레이
   // 그 뒤 기존 TickHash 브로드캐스트
   ```
   `HandlePacketOnServer`의 `InputCommand` 케이스는 그대로 `EnqueueInput`(인가된 clientId로) 호출 — 이제 슬롯 적재.
4. **기존 테스트 교체(의도된 것):** `ConsumePendingInputsReturnsAndRemovesInputsUpToTick`, `ServerDropsDuplicateAndStaleInputBySequence`, `SyncSystemSendsQueuedInputToServer`, `ServerClientConnectionTransfersInputAndSnapshot`, `ServerSynchronizesMultipleMemoryClientsWithDistinctAssignedIds`의 입력 검증부를 **슬롯/확정 모델**로 다시 씀(예: `ConfirmInputs`가 결측을 hold로 채우는지, 이중화 재전송이 슬롯 dedup되는지, 과거 tick 거부).
5. **주의:** `ApplyConfirmedInputs`는 `PlayerControl` 엔티티가 있어야 동작. 기존 handshake 테스트 월드엔 없으면 no-op(무해). M3 이후 클라는 `InputFrame`을 버퍼링하고 `clientSimTick + 1`부터 순서대로 적용한다.

### M3 — 클라 로컬 시뮬레이션 ✅ 완료
1. `NetworkSyncSystem`(Client): `HandlePacketOnClient`에 `InputFrame` 케이스 추가 → `std::map<uint64_t, std::vector<InputCommand>> m_frameBuffer`에 tick별 적재.
2. `clientSimTick`(멤버): 스냅샷 적용 시 `= snapshot.tick`로 초기화.
3. 매 `FixedUpdate`(또는 프레임 소비): `while (m_frameBuffer has clientSimTick+1) { ApplyConfirmedInputs(clientWorld, buffer[clientSimTick+1]); ++clientSimTick; erase; }`.
4. 스냅샷은 이제 **조인/resync 시에만** 상태를 세팅. 이후 상태 변화는 프레임 적용으로만.
5. 테스트: host 유사(공유 클럭) 경로에서 서버가 만든 프레임을 클라가 적용해 `clientWorld.ComputeHash() == serverWorld.ComputeHash()`.
6. **주의:** 최소판은 클라가 자기 입력도 릴레이로만 적용(로컬 예측 없음). 입력 지연·RTT만큼 체감 지연 — 예측/rollback은 별도(향후).

### M4 — 클럭 추정 + 입력지연 + 이중화 ✅ 완료
1. 클라 `NetworkSyncSystem`: `uint64_t m_serverTickEstimate`. `TickHash` 수신 시 `= tickHash.tick`, 매 `FixedUpdate`마다 `++`(TickHash 없이도 진행).
2. `SendQueuedLocalInputs`: `applyTick = m_serverTickEstimate + kInputDelayTicks(2)`. `command.tick` 채우는 로직을 이 값으로.
3. 이중화: 클라가 queued input을 `RedundantInput{command, remainingSends=3}`에 넣고, 같은 `InputCommand`를 최대 3번 개별 unreliable 패킷으로 보낸다(서버 슬롯 dedup).
4. 테스트: 손실 있는 링크에서 이중화로 결측이 줄고, 남는 결측은 서버 hold 대체로 결정론 유지.
5. **주의:** host/single은 공유 클럭이라 추정이 자명하게 정확. dedicated만 TickHash 케이던스에 의존.

### M5 — 정리 ✅ 완료
- `GameEngine.cpp` `CallFixedUpdates`: dt를 `Fixed64::FromDouble(...)` 대신 정수 `m_fixedTimeStepUs`에서 고정소수점으로 유도(`raw = fixedTimeStepUs * Fixed64::Scale / 1'000'000`). 불변식-(1).
- deque 잔재/죽은 API 제거, 주석 정리.

### M6 — 결정론 수렴 테스트 ✅ 완료
- `SimulatedNetwork`(손실/중복/재정렬, 시드) 위에서 **서버 + N 클라**, 각자 입력 주입, 여러 tick 구동 → **모든 월드 `ComputeHash()`가 서버와 수렴**.
- 케이스: (a) 무손실 다중 클라 완전 일치, (b) 손실+지터에서 결국 수렴, (c) 특정 tick 입력 유실이 이중화/hold로 결정론 유지(전원 동일 상태).
- 대표 테스트: `Network.LockstepConvergesTwoClientsOverLossyReorderingLink`.

### M7 — 문서 ✅ 완료
- `docs/blog/08_deterministic-input-lockstep.md` 신설(서술형: 왜 lockstep, 흐름, 호출구조, 07과의 관계).
- `07` 채널표와 설명은 `InputFrame → Reliable` M2 구현 완료 상태를 반영함.
- 이 설계 문서(본 파일)도 구현 완료분에 맞춰 갱신.

### M8 — 디싱크 탐지·복구 경로 테스트 ✅ 완료
- **동기**: `TickHash` 불일치 → `ResyncRequest` → `WorldSnapshot` 복구 경로는 정상 세션에서 한 번도 실행되지 않는다. M6의 수렴 테스트들은 전부 "디싱크가 안 난다"를 확인할 뿐, "디싱크가 났을 때 복구된다"는 아무도 확인하지 않았다.
- **주입 수단**: `NetworkSyncSystem::DebugSkipNextConfirmedInputFrameOnce()` — 다음 확정 `InputFrame` **하나**를 받았지만 적용하지 않고 `clientSimTick`만 진행시킨다. 기본 꺼짐 + 1회성 자동 해제. 고른 이유와 버린 대안(`#ifdef` 컴파일타임 훅, 상속 오버라이드, 테스트가 컴포넌트 값 직접 덮기)은 그 함수의 헤더 주석에 있다.
- **테스트**: `Network.SkippedConfirmedInputFrameIsDetectedByTickHash`(탐지), `Network.DesyncedClientRecoversThroughResyncSnapshot`(복구), `Network.RecoveredClientKeepsMatchingServerAfterResync`(복구 이후 지속), `Network.NoInjectionKeepsCleanSessionFreeOfHashMismatch`(대조군).
- 이 작업에서 드러난 결함은 §7의 "디싱크 복구 경로 — 실측과 알려진 결함"에 적었다. **고치지 않았다.**

---

## 5. 코드 리뷰 findings → 어느 마일스톤이 고치나

| # | 발견(결정론 결함) | 해결 |
|---|---|---|
| 1 | 원격 클럭/tick 동기화 없음 | **M4**(TickHash 기반 추정 + 입력지연) |
| 2 | 입력을 도착하는 대로 적용(`<=upToTick`) | **M2/M3**(확정 tick에만 적용) |
| 3 | `m_clients` unordered 순회 → 적용 순서 비결정론 | **M2**(`ConfirmInputs` clientId 정렬) |
| 4 | 결측 입력 정책 없음 | **M2**(직전 hold 대체 후 릴레이) |
| 5 | unreliable+무이중화로 입력 유실 | **M4**(이중화) + **M2**(InputFrame reliable 릴레이) |
| 6 | 실제 배선 안 됨(stub) | **M1~M4**(이동+릴레이+클라 시뮬) |
| 7 | dt를 FromDouble로 계산 | **M5**(정수 유도) |

---

## 6. 이 저장소에서 작업하는 법 (에이전트용)

- **빌드/테스트** (MinGW g++ / CMake 프리셋):
  ```
  cmake --preset default
  cmake --build --preset default
  ctest --preset default            # 또는 ./out/build/default/bin/EngineCoreTests.exe [Suite.Name]
  ```
- **소스는 명시적 등록**(글롭 아님). 새 `.cpp`→`EngineCore/CMakeLists.txt`의 `target_sources PRIVATE`, 새 `.hpp`→같은 파일 `FILE_SET HEADERS FILES`. 테스트는 `tests/CMakeLists.txt`의 `add_executable`에 `.cpp` 추가 + `add_test(...)`. 헬퍼: `tools/Create-CppArtifact.ps1`.
- **테스트 하네스**: 커스텀 헤더 `tests/TestHarness.hpp`. `TEST_CASE(Suite, Name) { EXPECT_TRUE/EXPECT_EQ(...); }`. GoogleTest 아님.
- **마일스톤마다 빌드·테스트 green 유지**(신뢰성 작업과 동일 리듬). 각 M은 독립 커밋 권장.
- **네임스페이스** `EF`(네트워크는 `EF::Network`). 주석·문서는 한국어.

---

## 7. 비자명 포인트 / 함정

- **즉시-flush(07 §5):** `SendX`는 즉시 데이터그램을 낸다. `InputFrame`은 reliable이라 손실 시 `NetworkManager::Update(tick)`(매 tick, `NetworkSyncSystem::FixedUpdate` 끝에서 호출)가 재전송. 이 펌프가 안 돌면 재전송이 안 됨.
- **`PlayerControl`은 스냅샷으로 전파**되어야 클라가 "이 clientId가 이 엔티티" 매핑을 안다. `RegisterSnapshotComponent<PlayerControl>()`를 서버·클라 양쪽 월드에 등록. 신규 플레이어 스폰 중간 합류의 엔티티 전파는 별도 과제(최소판은 초기 스냅샷에 담긴 엔티티로 테스트).
- **hold 대체의 행동입력 함정:** 결측 대체가 buttonMask 전체를 반복하면, 나중에 추가될 "발사" 같은 엣지 비트가 매 tick 재발사된다. 행동 비트 도입 시 대체에서 그 비트는 제거(이동/aim만 hold).
- **deque→slot 교체는 기존 입력 테스트를 깬다** — 의도된 교체(비결정론 API 제거). 놀라지 말고 슬롯/확정 기준으로 다시 쓸 것.
- **host/single vs dedicated:** 전자는 한 `GameEngine`·공유 tick·즉시 메모리 전달 → 같은 tick에 서버 확정→클라 적용까지 가능(월드 업데이트 순서: 서버 월드가 먼저 등록됨). 후자는 별도 클럭·RTT 지연.
- **클라 시뮬은 프레임 구동**(`clientSimTick`)이라 엔진 `FixedUpdate`의 tick 번호와 다를 수 있다 — 클라 이동을 자동 실행 `ISystem`으로 두지 말고 sync 시스템이 확정 프레임으로 명시 구동(순서 통제).

### 디싱크 복구 경로 — 실측과 알려진 결함

M8에서 디싱크를 일부러 주입해 복구 전 과정을 굴려 본 결과다. **아래 결함은 M8 범위 밖이라 고치지 않았다.** 여기 적힌 숫자는 무손실 `MemoryConnection`에 서버·클라 각각 `FixedUpdate` 한 번씩(클라 → 서버 순), 50Hz 기준 실측이다.

**측정값**

| 구간 | 실측 |
|---|---|
| 디싱크 발생 → `HasHashMismatch()` | **0 tick**(주입과 같은 tick). 서버가 그 tick의 `TickHash`를 이미 보내 둔 상태라 다음 클라 펌프에서 바로 잡힌다. |
| 디싱크 발생 → 해시 재일치 | **3 tick**(≈60ms). 단, **월드가 정지한 뒤에**. |
| 그 사이 클라가 적용한 `WorldSnapshot` | **3개** — 한 번이 아니다. 이유는 아래 (1). |

**(1) ⚠️ 스냅샷의 tick 라벨이 한 칸 앞서 있다 — resync마다 확정 입력 1프레임을 잃는다.**

`NetworkSyncSystem::UpdateServer`의 순서는 `수신 처리 → ConfirmInputs(tick) 적용 → TickHash(tick) 방송`이다. `ResyncRequest`는 **수신 처리 단계**에서 답하므로, `world.CreateSnapshot(tick)`이 담는 내용은 *tick의 입력을 적용하기 전* 상태 = 실제로는 `tick - 1`까지의 상태다. 그런데 라벨은 `tick`으로 붙는다.

클라는 그걸 받아 `m_clientSimTick = snapshot.tick`으로 놓고, 같은 배치에 온 `InputFrame(tick)`을 `frame.tick > m_clientSimTick` 조건에서 버린다. 귀결:

- **매 resync가 확정 입력 정확히 한 프레임을 조용히 삼킨다.**
- 클라의 `m_localHashByTick[tick]`은 서버의 `TickHash(tick)`보다 한 tick 낡아서 **복구 직후 다시 불일치**가 뜬다.
- 월드가 매 tick 계속 바뀌는 동안에는 이 과정이 끝나지 않는다 — **tick마다 `ResyncRequest` → tick마다 전체 스냅샷의 무한 루프**. 실측으로 확인했다: 플레이어가 계속 이동하는 상태에서 디싱크를 주입하고 200 tick(4초)을 굴리면 **스냅샷을 정확히 200개**(매 tick 하나) 받으면서도 불일치가 유지되고, 클라 위치는 서버보다 계속 2유닛 뒤처져 있었다. 이동을 멈추자(= 월드가 정지하자) 그제서야 3 tick 만에 수렴했다.
- 조인 핸드셰이크도 **같은 off-by-one을 밟는다.** 지금은 접속 순간에 아무도 움직이지 않아 삼켜진 프레임이 no-op이라 드러나지 않을 뿐이다.

고칠 방향(미착수): 스냅샷을 `ConfirmInputs` **적용 후**에 뜨거나, 라벨을 `tick - 1`로 정직하게 붙이고 클라가 `InputFrame(tick)`을 버리지 않게 한다. 어느 쪽이든 "스냅샷 라벨 T = T의 입력까지 적용된 상태"라는 정의를 코드 한 곳에 못 박고 양쪽이 그걸 따르게 해야 한다.

**(2) ⚠️ `ResyncRequest`에 rate limit이 없다.**

`VerifyPendingTickHashes`는 불일치하는 **pending tick마다** `ResyncRequest`를 보내고, 서버는 요청마다 **전체 월드 스냅샷**을 reliable로 답한다. "이미 요청했다" 플래그도, 백오프도 없다. RTT가 크거나 (1) 때문에 루프에 빠지면 **스냅샷 폭풍**이 된다 — 월드가 커질수록(공장 게임이면 커진다) 위험이 커진다. 최소한 "in-flight resync 하나만" 같은 억제가 필요하다.

**(3) 복구되는 동안 화면은 틀린 상태를 그리다 순간이동한다.**

예측·롤백·보간이 없으므로, 디싱크가 난 클라는 탐지 전까지 **틀린 월드를 그대로 렌더**하고, 스냅샷이 도착하면 그 프레임에 pop 한다. 위 실측(3 tick ≈ 60ms)이면 눈에 잘 안 띄지만, (1)의 루프에 빠지면 "매 프레임 순간이동하며 계속 뒤처지는" 화면이 된다. 렌더 보간(R7)은 이 pop을 부드럽게 할 뿐 원인을 없애지 못한다.

**(4) 해시를 직접 비교할 때의 함정.**

클라는 서버보다 항상 최소 한 tick 뒤처지므로, `clientWorld.ComputeHash() == serverWorld.ComputeHash()`는 **월드가 정지해 있을 때만** 참이다. 정상 동작을 확인하려면 raw 해시가 아니라 tick을 맞춰 비교하는 `HasHashMismatch()`를 봐야 한다. 새 테스트를 쓸 때 이걸 헷갈리면 멀쩡한 코드가 깨진 것처럼 보인다.

---

## 8. 플레이어 엔티티 vs 캐릭터 엔티티 (소유 모델)

**채택 방향(Controller/Pawn 분리):** 수명이 다른 셋을 **분리**한다. (핵심: 세션 ≠ 플레이어. 플레이어는 세션이 죽어도 남아야 한다 = 세이브.)

| 개념 | 수명 | 식별자 | 사는 곳 |
|---|---|---|---|
| **세션/연결** | 접속~끊김만 | `clientId`(접속마다 새 발급, ephemeral) | 네트워크 계층 `NetworkClientState` |
| **플레이어(세이브)** | **영속** — 끊겨도·서버 재시작해도 유지 | `accountId`/`playerId`(durable) | 게임 상태(영속) |
| **캐릭터/아바타** | 스폰~사망 | `EntityId`(로컬), owner는 stable id로 참조 | 시뮬 월드, `ControlledBy{clientId}` |

왜 좋은가: 생명주기 분리(캐릭터가 죽어도 플레이어 상태 유지, 플레이어가 끊겨도 세이브 유지), 빙의 간접화(리스폰=새 캐릭터·같은 플레이어, 관전, 다중 유닛), ECS 조합 자연스러움. 언리얼 `PlayerController`↔`Pawn`과 같은 정석.

**주의:** **플레이어 정체성을 `clientId`로 박지 말 것.** `clientId`는 세션용(접속마다 바뀜). 끊기면 **세션만 파기**, 플레이어 엔티티는 유지, 캐릭터는 despawn. 재접속 = 새 `clientId`를 durable id로 기존 플레이어에 재연결. (계정/영속이 아직 없는 현 단계에선 clientId로 가되, "clientId = 영구 플레이어"로 가정하는 코드를 만들지 말 것.)

### 세이브(영속)를 어디에 두나 — 결정론 영향 여부로 갈림
- **결정론 시뮬에 영향 O** (자원·점수가 능력/승패 좌우 등) → **시뮬 월드 안.** 전 peer가 결정론으로 같은 값을 계산, 스냅샷으로 복제. "매치 동안 영속."
  - 엔진의 `CreateSnapshot`/`ApplySnapshot`은 **이미 월드 직렬화기**다 → 시뮬-내부 상태의 세이브 = **스냅샷을 디스크로**(종료 시 저장/시작 시 로드)로 자연 확장.
- **영향 X** (계정 진행·언락·전적 등 메타) → **시뮬 밖 별도 영속 계층**(save/DB). 결정론 해시에 미포함.
- "끊겨도 남음"은 두 층: **매치 동안**(시뮬 월드, 전 peer 일치) vs **세션·재시작 넘어**(별도 세이브 계층). 게임에 따라 둘 다 가능.

### 끊김 ≠ 사망 — 캐릭터는 얼리고(despawn 아님), 데이터는 캐릭터에
- **끊김(캐릭터 생존):** despawn하지 말고 캐릭터에 `Dormant` 태그 → 이동/물리/AI 시스템이 스킵. 월드엔 남으므로 **스냅샷이 곧 세이브**(매치 범위). 재접속 = 태그 해제 + 새 세션(clientId) 재연결.
- **사망:** despawn(또는 ragdoll), 리스폰 = 새 캐릭터.
- **장기 정리:** dormant 타임아웃 → despawn + 캐릭터 레코드를 **영속 저장소(DB)로 직렬화**(accountId 키). 무한 누적 방지.
- **"저장"은 두 층:** ① 얼려서 월드에 남김(세션·매치, 전 peer가 들고 있고 해시 포함, `CreateSnapshot`이 직렬화) ② 저장소로 직렬화(재시작·계정 넘어, 라이브 시뮬에서 빼서 보관).
- **라이브 캐릭터 상태(Transform·Health…)는 캐릭터 엔티티에 둔다. 플레이어 엔티티로 복제하지 말 것**(활성 중 이중 동기화·authoritative 모호). 플레이어 엔티티엔 캐릭터보다 오래 사는 것만(계정/메타·소유 링크·팀/점수).

> ⚠️ **결정론:** `Dormant` on/off(및 spawn/despawn)는 **relayed 결정론 이벤트**여야 한다. 서버만 얼리고 클라에 안 알리면 디싱크→리싱크 폭풍. "player X 캐릭터가 tick T에 dormant"를 입력 릴레이 스트림(또는 별도 relayed 이벤트)에 실어 **전 peer가 같은 tick에** 적용. 상태를 바꾸는 모든 결정은 입력과 같은 원리로 서버가 확정·전파.

### 함정 — 네트워크를 넘는 엔티티 참조는 clientId로 (EntityId 금지)
- 스냅샷은 [`ComponentSnapshot.entities`](../../EngineCore/ECS/World.hpp)에 **`EntityIndex`만** 싣고 **`EntityId`의 version은 싣지 않는다.** 따라서 컴포넌트가 `EntityId`(index+version)로 다른 엔티티를 참조하면 클라 재조립 시 어긋날 수 있다.
- **규칙:** 복제되는 링크(플레이어↔캐릭터 등)는 **안정적 도메인 id = `clientId`** 로 건다. 서버 로컬 참조는 `EntityId`로 해도 되지만, 와이어/스냅샷을 넘는 것은 clientId(또는 향후 도입할 안정적 network-entity-id)로.

### 입력 적용에의 영향 (작음)
- `ApplyConfirmedInputs`는 **캐릭터 엔티티**(`Transform` + clientId 태그)에 직접 적용한다 — M1의 `View<Transform, PlayerControl>` 매칭 그대로. 이동 로직은 플레이어 엔티티를 거치지 않는다(입력→캐릭터를 clientId 한 번으로).
- 명명 권장: 캐릭터의 조종 태그는 `ControlledBy{clientId}`(또는 현행 `PlayerControl`을 캐릭터에 붙인 의미 그대로 유지)로 두어 "플레이어 엔티티"와 헷갈리지 않게.
- "입력→플레이어→캐릭터" 2단 간접이 필요해지면 그 링크도 clientId 기반. 결정론(clientId 정렬)·매핑 비용에 영향 없음.
- **스폰 복제 주의:** 중간 합류 클라에게 새 플레이어/캐릭터 엔티티가 생기는 것을 어떻게 전파할지는 별도 과제(스냅샷 재전송 또는 스폰 이벤트). 최소판은 초기 스냅샷에 담긴 엔티티로 테스트.
```
