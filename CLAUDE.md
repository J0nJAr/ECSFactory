# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

（이 파일은 에이전트용 지침서다. 이 저장소의 나머지 문서와 마찬가지로 한국어로 쓴다.）

## 이게 뭔가

ECSFactory는 **서버 권위형 멀티플레이 + 결정론적 시뮬레이션**을 목표로 하는, 외부 의존을 최소화한 C++20 게임 엔진 코어다. 같은 엔진 코드가 세 가지로 실행된다 — dedicated server / remote client / host·singleplayer. 그래서 로직 대부분은 공용 정적 라이브러리 `EngineCore`에 있고, `GameClient`와 `GameServer`는 그 위에 얹힌 얇은 실행 파일이다.

모든 것은 네임스페이스 `EF` 안에 있다(네트워크 코드는 `EF::Network`).

### 프로젝트 성격: 학습용 프로젝트다

이건 **공부용 사이드 프로젝트**지 상용 소프트웨어가 아니다. 두 목표가 나란히 가고, 둘 다 중요하다.

1. **엔진·시스템 개발을 직접 만들며 배운다** — 소유자는 AI를 협업자로 두고 만들면서 공부하는 중이다. *왜 이 설계를 골랐는가*를 이해하는 것이 돌아가는 코드만큼이나 결과물이다.
2. **공장(팩토리) 게임을 만든다** — 이 엔진 위에 올릴 자동화·생산 라인 장르. 2D인지 3D인지는 미정(현재 렌더링 방향은 `docs/design/rendering-dx11.md` 참조).

**작업 방식에 주는 의미:**

- **결과만 내지 말고 설명하라.** 뭔가를 구현했으면 어떤 대안이 있었고 왜 이걸 골랐는지 말한다. `docs/design/` 아래 설계 문서가 정확히 이 목적의 핸드오프 스펙이다 — 근거를 채팅 메시지에만 남기지 말고 문서를 늘려라.
- **영리한 길보다 배우기 좋은 길을 택한다.** 두 구현의 품질이 비슷하면 읽고 추론하기 쉬운 쪽을 고른다.
- **틀린 걸 알면서 얼버무린 설명을 주석에 남기지 마라.** 학습용 코드베이스에서 잘못된 주석은 없는 것보다 나쁘다 — 잘못된 걸 가르치게 된다. 확신이 없으면 주석에 그렇게 명시하거나 질문한다.

### 작업 리듬 — 마일스톤마다 설명 문서를 누적한다

마일스톤은 **작게**(하루치 정도) 자르고, 하나를 끝낼 때마다:

1. **빌드·테스트 green 확인.** 마일스톤마다 독립 커밋.
2. **`explain-diff-html` 스킬로 그 변경의 설명 문서를 만든다** — 배경·직관·코드 워크스루·퀴즈가 나온다. 출력은 `C:\Users\macham\Documents\explanations\`에 날짜 접두로 쌓인다. 학습용 프로젝트이므로 이게 결과물의 일부다.
3. 설계 판단이 바뀌었으면 해당 `docs/design/*.md`를 고친다. 근거를 대화에만 남기지 않는다.

마일스톤을 크게 묶으면 설명 문서가 소화 불가능한 크기가 된다. 그래서 크게 묶지 않는다.

### 문서 언어 규칙

소유자는 **한국어 사용자**다. 사람이 읽는 것은 전부 **한국어**로 쓴다: 코드 주석, `README.md`, `docs/design/*`, `docs/blog/*`, 커밋 메시지, 새로 만드는 모든 마크다운. 명시적으로 요청받지 않는 한 이 저장소에 영어 문서를 만들지 않는다.

### 주석 규약

여기의 주석 밀도는 **일반적인 상용 코드보다 의도적으로 높다.** 그리고 주석은 문법을 다시 읽어주는 게 아니라 *설계 의도*를 담는다.

- **클래스와 자명하지 않은 함수에는 헤더 주석을 반드시 단다.** 무엇을 위한 것인지, 왜 이 형태인지, 호출자가 지켜야 할 불변식은 무엇인지를 적는다. 목표 스타일은 [ReliabilityChannel.hpp](EngineCore/Network/ReliabilityChannel.hpp)와 [Gameplay/PlayerMovement.hpp](EngineCore/Gameplay/PlayerMovement.hpp)를 보면 된다 — 책임, 전달·결정론 보장, 그리고 **이 타입이 하지 않는 일**까지 명시한다.
- 비자명한 선택을 했으면 **버린 대안과 이유를 함께 적는다**(예: "결측 입력은 stall이 아니라 직전 입력 hold — stall이면 한 명 렉에 전원이 멈춘다").
- **매직 넘버에는 주석이나 이름 붙은 상수**로 그 값의 출처를 남긴다(`kInputDelayTicks`, `kFragmentSize` 등).
- **함정을 표시한다.** 나중에 읽는 사람을 물 만한 것 — 결정론 위험, 순서 요구사항, "매 tick 펌프를 돌려야 함" 같은 것 — 은 문서뿐 아니라 그 코드 자리에 적는다.
- 수정 후에도 주석이 참이어야 한다. 동작을 바꾸면서 위의 주석을 안 고치는 건 여기선 결함이다.

## 빌드, 테스트, 실행

툴체인은 **MSYS2의 MinGW g++**(`C:/msys64/ucrt64/bin/g++.exe`)이고, CMake 프리셋(`MinGW Makefiles` 제너레이터)으로 돌린다. 루트 `CMakeLists.txt`에 MSVC 분기가 있지만 기본 프리셋은 g++를 쓰므로, 실제로 켜져 있는 경고 세트는 `-Wall -Wextra`다.

```powershell
cmake --preset default          # 구성 (x64-debug 상속). 그 외: x64-debug, x64-release
cmake --build --preset default  # 전체 빌드 → out/build/default/bin
ctest --preset default          # 전체 테스트 (실패 시 출력 표시)
```

실행 파일은 `out/build/<preset>/bin/`에 떨어진다(`EngineCoreTests.exe`, `GameClient.exe`, `GameServer.exe`).

### 테스트 하나만 돌리기

테스트는 GoogleTest/Catch가 **아니라** 직접 만든 헤더 온리 하네스([tests/TestHarness.hpp](tests/TestHarness.hpp))를 쓴다. 케이스는 `TEST_CASE(Suite, Name)`으로 등록된다. 테스트 바이너리는 선택 인자로 `Suite.Name` 필터 하나를 받고, `ctest`에도 케이스가 개별 등록되어 있다.

```powershell
ctest --preset default -R Network.LockstepConvergesTwoClientsOverLossyReorderingLink   # ctest로
./out/build/default/bin/EngineCoreTests.exe Math.SqrtMatchesPerfectSquares             # 직접 실행 + 필터
```

`TEST_CASE`를 추가했으면, 개별 ctest 항목으로도 돌리고 싶을 때 [tests/CMakeLists.txt](tests/CMakeLists.txt)에 대응하는 `add_test(...)` 줄도 추가한다.

## 소스/헤더 파일 추가 (중요)

CMake 소스 목록은 **글롭이 아니라 명시적**이다. 새 `.cpp`/`.hpp`는 해당 `CMakeLists.txt`에 등록되기 전까지 컴파일되지 않는다(`.cpp`는 `target_sources PRIVATE`, `EngineCore`의 `.hpp`는 `FILE_SET HEADERS` 블록). 손으로 편집하지 말고 헬퍼를 쓴다:

```powershell
pwsh -File tools/Create-CppArtifact.ps1 -Target EngineCore -Kind pair -RelativePath ECS/MyThing
```

파일을 만들고 대상 타깃의 `CMakeLists.txt`에 등록까지 해준다. VS Code 태스크 **"CMake: Create C++ Artifact"**로도 같은 걸 할 수 있다. `-Kind`는 `pair`/`source`/`header`, `-Target`은 `EngineCore`/`GameClient`/`GameServer`/`tests`.

### clangd / IDE 참고

`.clangd`의 `CompilationDatabase`는 `out/build/x64-debug`를 가리킨다. 코드 인텔리전스가 동작하려면 그 프리셋을 최소 한 번 구성해서(`cmake --preset x64-debug`) 거기에 `compile_commands.json`이 있어야 한다 — `default` 프리셋은 다른 디렉터리에 빌드한다.

## 문서

`docs/`는 이 파일보다 자세하고, 코드와 함께 갱신되고 있다.

- **[docs/design/roadmap.md](docs/design/roadmap.md) — 마일스톤 전체 계획.** 지금 무엇을 왜 그 순서로 만드는지가 여기 있다. "다음에 뭘 해야 하나"는 이 문서를 먼저 본다.
- `docs/design/*.md` — 다음 에이전트·개발자를 대상으로 한 **핸드오프 스펙**: 근거, 불변식, 마일스톤 상태, 함정. 입력·동기화를 건드리기 전에 [docs/design/deterministic-input-lockstep.md](docs/design/deterministic-input-lockstep.md)를, 렌더링을 시작하기 전에 [docs/design/rendering-dx11.md](docs/design/rendering-dx11.md)를, UI를 건드리기 전에 [docs/design/ui-layer.md](docs/design/ui-layer.md)를 읽는다(뒤의 둘은 설계만 있고 코드는 아직 없다).
- `docs/blog/*.md` — 각 계층을 만든 순서대로 쓴 서술형 기록(05 패킷 → 06 동기화 상태머신 → 07 신뢰성 UDP → 08 lockstep).

`README.md`의 "작업 스케치" 섹션이 살아 있는 TODO 목록이다.

## 아키텍처

### 애플리케이션 수명주기

앱은 `GameEngine` 하나를 소유하는 `IGameApp`(별칭 `GameAppBase`, [Base/IGameApp.hpp](EngineCore/Base/IGameApp.hpp))을 상속한다. `IGameApp::Run()`이 순서를 고정한다: `OnConfigure` → `OnInitialize`(여기서 월드·시스템·리소스 생성) → `engine.Initialize()` → `OnRun` → `OnShutdown` → `engine.Shutdown()`. [GameClient/main.cpp](GameClient/main.cpp), [GameServer/main.cpp](GameServer/main.cpp) 참조.

### GameEngine과 World

`GameEngine`([Base/GameEngine.cpp](EngineCore/Base/GameEngine.cpp))은 여러 `World`(각각 `WorldRole` — `ServerSimulation` / `ClientPresentation` — 로 태깅됨)와 고정 timestep 누산기 루프(기본 20ms / 50Hz)를 소유한다. 이 루프가 **결정론적인 `FixedUpdate(Fixed64 dt, tick)`**(누산기 기반이라 프레임당 0~n회 실행)와 가변 주기 `FrameUpdate(float dt)`를 구동한다. `dt`는 정수 `m_fixedTimeStepUs`에서 고정소수점으로 유도한다 — double에서 만들지 않는다. 테스트에서는 `ManualStepFixed` / `ManualStepFrame`으로 루프를 돌린다.

**핵심 설계 포인트:** 호스트와 싱글플레이는 별도 서버 프로세스를 띄우지 *않는다*. 클라이언트 엔진이 *같은 `GameEngine` 안에* `ServerSimulation` 월드를 추가하고 인메모리 연결로 대화한다 — 그래서 single / host / dedicated가 하나의 네트워크 코드 경로를 공유한다. 기억할 귀결 하나: 같은 엔진의 월드들은 **tick 클럭을 공유**하고, dedicated는 공유하지 않는다(아래 클럭 추정 참조).

### ECS 계층 ([EngineCore/ECS/](EngineCore/ECS/))

- **`EntityId`** ([Core.hpp](EngineCore/ECS/Core.hpp)): 기본 64비트(`EF_ENTITY_ID_64`, index 32 : version 32). 해제된 index를 재사용하면 version이 올라간다. `GetEntityIndex` / `GetEntityVersion` / `CreateEntityId` 헬퍼를 쓰고, 센티널은 `kNullEntity`.
- **`Registry`**: `EntityManager`와 타입별로 인덱싱된 `vector<unique_ptr<IPool>>`을 소유한다. 엔티티 생성·삭제, 컴포넌트 추가·제거, `CreateView<...>()`.
- **`ComponentPool<T>`**: `SparseSet<EntityIndex, T>`를 감싸고 `IPool` 인터페이스로 dense 바이트 span을 노출한다 — 스냅샷과 해시가 순회하는 게 이것이다.
- **`View<With<...>, Without<...>>`**: 쿼리에 맞는 엔티티를 순회한다. 효율을 위해 **필수 풀 중 가장 작은 것**부터 시작한다. `view.ForEach([](EntityId, A&, B&){...})`(엔티티 인자는 선택)과 range-`for` 둘 다 된다.
- **`World`** ([World.hpp](EngineCore/ECS/World.hpp)): `Registry` + `SystemRegistry` + 타입 키 **리소스 맵**의 조합. 리소스(`AddResource<T>` / `GetResource<T>`)는 월드별 싱글턴을 담는다 — `NetworkManager`가 이렇게 붙는다.

`EngineCore/Render/`, `Feature/`, `Systems/`는 아직 빈 자리표시 디렉터리다.

### TypeInfo — index와 hash의 구분 ([TypeInfo.hpp](EngineCore/ECS/TypeInfo.hpp))

`GetTypeInfo<T>()`는 타입별 `TypeInfo { hash, index, name, size, alignment, isTrivial }`을 돌려준다.

- `hash`는 **타입 이름의 컴파일타임 FNV-1a**(이름은 `static constexpr T::TypeName`으로 덮어쓸 수 있다) — 프로세스가 달라도 안정적이다.
- `index`는 **처음 쓰일 때 할당되는 런타임 카운터** — 프로세스 로컬이고 순서 의존적이라 **클라이언트와 서버 사이에서 안정적이지 않다**.

귀결: 네트워크를 넘는 것(스냅샷, 타입 테이블)은 절대 `index`가 아니라 `hash`/`size`/`alignment`로 비교한다. 로컬 풀 저장은 `index`로 키를 잡는다.

### 결정론 수학 ([EngineCore/Math/](EngineCore/Math/))

`Fixed64`([Math/Fixed64.hpp](EngineCore/Math/Fixed64.hpp))가 시뮬레이션 수치 타입이다 — **기본 Q32.32**(`EF_FIXED64_FRACTIONAL_BITS = 32`, [Base/Defines.hpp](EngineCore/Base/Defines.hpp)). 포맷은 설정 가능하다: [Math/Internal.hpp](EngineCore/Math/Internal.hpp)의 곱셈·나눗셈 경로와 [Math/Math.cpp](EngineCore/Math/Math.cpp)의 각도 상수·사인 테이블이 모두 `Fixed64::Scale`/`FractionalBits`에서 유도되므로 매크로를 바꿔도 정합성이 유지된다.

주의: `Sin`/`Cos` 정밀도는 저장 포맷과 무관하게 **Q16 기준 테이블**(절대오차 ~1.5e-5)에 묶여 있다 — 회전이 많이 걸린 단언에서는 이 점을 감안한다(수학 테스트는 허용오차를 `Scale / 65536`으로 스케일한다). `Vector`/`Quaternion`은 `Fixed64` 위에 올라간다. 값 생성은 `FromInt`/`FromRaw`로 하고 `FromFloat`/`FromDouble`/`ToFloat`는 초기화·렌더·디버그 경로 전용으로 둔다. 직렬화되는 건 raw 접근(`Raw()`/`FromRaw()`)이다.

## 네트워킹 ([EngineCore/Network/](EngineCore/Network/))

세 계층이 쌓여 있다. 이 분리를 유지할 것 — 전송 계층은 패킷을 모르고, 동기화 시스템은 소켓을 만지지 않는다.

### 1. 전송 — `INetworkConnection` + `Endpoint`

데이터그램 단위이고 **주소 기반**이다: `SendTo(Endpoint, span<const uint8_t>)` / `ReceiveFrom() -> optional<Datagram{from, bytes}>`. 메시지 경계 보장은 각 구현체의 책임이다. `Send`/`Receive`는 `DefaultPeer()`를 대상으로 하는 point-to-point 편의 래퍼로 남아 있다. `Endpoint`([Endpoint.hpp](EngineCore/Network/Endpoint.hpp))는 해시 가능한 값 타입(`IPv4`는 네트워크 오더 주소·포트, `Memory`는 peer id)으로, 디먹스용 맵 키로 쓰인다.

- `MemoryConnection` — 같은 프로세스(host/singleplayer). `CreatePair()`가 양쪽 끝을 돌려준다.
- `UDPConnection` — 원격 클라이언트 ↔ dedicated server(포트 `6780`). `recvfrom`의 sender를 `Endpoint`로 노출한다.
- `SimulatedNetwork`([SimulatedNetwork.hpp](EngineCore/Network/SimulatedNetwork.hpp)) — **테스트 전용** 인메모리 허브. 시드 기반이라 재현 가능한 손실·중복·재정렬을 주입한다. 참가자마다 `CreateHost(self, defaultPeer)`. 수렴 테스트가 서버 + N 클라이언트를 손실 링크 위에서 돌리는 방식이 이것이다.

### 2. 신뢰성 — `ReliabilityChannel`

원격 endpoint마다 상태 머신 하나([ReliabilityChannel.hpp](EngineCore/Network/ReliabilityChannel.hpp)). 앱 레코드(`[type][payload...]`)를 데이터그램으로 포장하고 ack / 재전송 / 조각화 / 재조립 / 순서를 처리한다. 전달 등급 세 가지이고, 패킷 타입별 배정은 `NetworkManager::DeliveryClassFor`가 한다.

| 전달 등급 | 보장 | 쓰는 곳 |
|---|---|---|
| `Reliable` (0) | 순서 보장 + 재전송, MTU 초과 시 조각화 | 제어 패킷, `WorldSnapshot`, **`InputFrame`** |
| `Sequenced` (1) | 비신뢰, 최신만(구식은 폐기) | `TickHash` |
| `Unreliable` (2) | 비신뢰, 전부 전달 | `InputCommand`(클라→서버. 중복 제거는 앱 계층에서) |

와이어 상수는 `ReliabilityChannel.cpp` 상단에 있다: 조각 1024바이트, 데이터그램 페이로드 최대 1200바이트, 4 tick 후 재전송. 데이터그램 헤더는 `[version][누적 ack]` 뒤에 채널 메시지들이 붙는 형태다.

### 3. 프로토콜 — `NetworkManager`

월드 리소스([NetworkManager.hpp](EngineCore/Network/NetworkManager.hpp))로, 연결 하나와 endpoint↔clientId 테이블, endpoint별 `ReliabilityChannel`을 소유한다. 패킷 타입([Packet.hpp](EngineCore/Network/Packet.hpp), `kProtocolVersion`): `Hello`, `TypeTable`, `WorldSnapshotRequest`, `WorldSnapshot`, `InputCommand`, `TickHash`, `ResyncRequest`, `Welcome`, `InputFrame`. 모든 타입에 static `Encode*`/`Decode*`가 있다(`NetUtils.hpp`의 `PacketWriter`/`PacketReader`로 네트워크 바이트 오더 직렬화. `Fixed64`는 `Raw()`로 나간다).

서버 쪽 클라이언트별 상태는 `NetworkClientState`다: `endpoint`, 조종 엔티티, 타입테이블·스냅샷 플래그, **`inputByTick` 슬롯 맵**, 마지막 확정 입력. `ClientIdForEndpoint`가 스푸핑 방지 조회다 — 페이로드에 실린 clientId가 아니라 endpoint로 인가한다.

매 tick 둘 다 호출되어야 하는 것:

- `SendX(...)`는 즉시 flush하지만, **재전송과 누적 ack를 굴리는 건 `Update(tick)`**이다 — `NetworkSyncSystem::FixedUpdate` 끝에서 호출한다. 이 펌프가 안 돌면 신뢰성 트래픽이 손실에서 절대 복구되지 않는다.
- `TryReceive()`는 순서·재조립이 끝난 다음 패킷 하나를 보낸 사람 endpoint와 함께 돌려준다(`TryReceivePacket()`은 endpoint를 빼는 래퍼).

### 스냅샷 모델 (`World::CreateSnapshot` / `ApplySnapshot`)

- **trivially-copyable** 컴포넌트만 스냅샷 대상이고, raw 바이트로 복사된다. 비trivial 컴포넌트는 제외된다.
- 클라이언트는 타입마다 `World::RegisterSnapshotComponent<T>()`로 명시 등록해야 한다(trivially-copyable을 static_assert). `ApplySnapshot`은 레지스트리를 처음부터 재구성하고, `hash`/`size`/`alignment`/`isTrivial`/바이트 수가 안 맞는 타입은 **거부**한다.
- 스냅샷은 **조인과 resync 전용**이다 — 매 tick 상태 스트리밍이 아니다(아래 lockstep 참조).
- `ComponentSnapshot.entities`에는 `EntityIndex`만 실리고 **version은 실리지 않는다.** 따라서 컴포넌트가 `EntityId`로 다른 엔티티를 참조하면 왕복에서 살아남지 못한다. 복제되는 링크는 안정적인 도메인 id(현재는 `clientId`)로 걸어야 한다.
- `World::ComputeHash()`(trivial 풀에 대한 FNV-1a)가 결정론 체크섬이고, `TickHash` 비교 → `ResyncRequest`를 구동한다.

## 결정론 입력 lockstep

이 엔진은 상태가 아니라 **입력**을 중계한다. 서버와 모든 클라이언트가 같은 확정 입력셋으로 같은 시뮬레이션을 돌린다. `Fixed64`·`TickHash`·resync가 존재하는 이유가 바로 이것이다. 전체 스펙과 근거는 [docs/design/deterministic-input-lockstep.md](docs/design/deterministic-input-lockstep.md)에 있다.

tick당 흐름:

```
클라:   로컬 입력 수집 → applyTick = serverTickEstimate + 2 (kInputDelayTicks)
        InputCommand ──unreliable, 최대 3회 재전송(kInputRedundancySends)──▶ 서버
서버:   EnqueueInput()이 그 클라의 비어 있는 inputByTick 슬롯을 채움
        confirmed = NetworkManager::ConfirmInputs(tick)   // clientId 정렬. 결측은 직전 입력 hold
        ApplyConfirmedInputs(serverWorld, confirmed)      // 서버가 먼저 시뮬
        InputFrame(tick, confirmed) ──reliable──▶ 모든 클라
        TickHash(tick, world.ComputeHash()) ──sequenced──▶ 모든 클라
클라:   InputFrame을 버퍼링 → clientSimTick+1 프레임이 있으면 적용하고 ++clientSimTick
        TickHash 불일치 → ResyncRequest → 새 WorldSnapshot
```

`NetworkSyncSystem`([NetworkSyncSystem.hpp](EngineCore/Network/NetworkSyncSystem.hpp))이 양쪽 역할(`NetworkSyncMode::Client` / `Server`)의 이 상태 머신을 들고 있는 `ISystem`이고, `Hello → Welcome → TypeTable → WorldSnapshot` 핸드셰이크도 여기 있다. `ApplyConfirmedInputs`([Gameplay/PlayerMovement.hpp](EngineCore/Gameplay/PlayerMovement.hpp))는 각 입력의 `clientId`를 `PlayerControl` 엔티티에 매칭해서 그 `Transform`을 순수 `Fixed64`로 옮긴다.

### 불변식 — 이걸 깨지 말 것

1. **시뮬레이션 경로는 `Fixed64`만.** float/double 금지. `FromFloat`/`FromDouble`/`ToFloat`는 초기화·렌더·디버그 전용.
2. **상태를 만들어내는 순회는 결정론적 순서여야 한다.** `NetworkManager::Clients()`(`unordered_map`)를 그대로 순회해 상태를 만들지 말 것 — `clientId`로 정렬한 뒤 처리한다. 순서 무관한 용도(예: `TickHash` 브로드캐스트)는 괜찮다.
3. **입력은 확정된 tick에 적용한다.** "도착하는 대로" 적용 금지. 서버와 모든 클라가 *같은* 입력을 *같은* tick에 넣어야 한다.
4. **결측 입력은 서버가 대체해서 중계한다**(현재: 직전 입력 hold). 그래서 어떤 peer도 임의로 판단하지 않는다. 단서: hold는 이동·조준에는 맞지만 엣지 트리거 행동 비트에는 틀리다 — 그런 비트가 추가되면 대체값에서 제거해야 한다.
5. **`InputFrame`은 reliable-ordered.** 구멍이 난 클라이언트는 재전송으로 채워질 때까지 그 tick을 보류해야 한다.
6. **스냅샷은 조인·resync 전용.**
7. **클라 시뮬레이션은 확정 프레임(`clientSimTick`)이 구동한다.** 엔진 자체의 tick 번호가 아니다. 클라 쪽 이동을 자동 실행되는 `ISystem`으로 등록하지 말 것 — 순서 통제를 위해 sync 시스템이 명시적으로 구동한다.

### 세션 vs 플레이어 vs 캐릭터

`clientId`는 **세션**을 식별한다 — 접속할 때마다 새로 발급되므로 영속적인 플레이어 정체성으로 취급하면 안 된다. 의도된 모델(설계 문서 §8)은 세션 / 영속 플레이어 / 캐릭터를 분리하고, `PlayerControl`(= `ControlledBy`)은 *캐릭터* 엔티티에 붙인다. 끊김은 캐릭터를 파괴하는 게 아니라 얼려야 한다(`Dormant`) — 그리고 그런 상태 변화 자체도 중계되는 결정론 이벤트여서 모든 peer가 같은 tick에 적용해야 한다. 아니면 디싱크한다.

## 코드베이스 현재 상태

동작하고 테스트로 덮여 있는 것: ECS, 고정소수점 수학, 스냅샷·동기화, 다중 클라이언트 라우팅을 갖춘 신뢰성 UDP 전송, 결정론 입력 lockstep 루프(설계 문서의 M1~M7 완료. 시드 기반 손실·재정렬 링크에서의 수렴 테스트 포함).

아직 **미완성 / 스텁**인 것:

- 실제 키보드·마우스 입력(`Input`, [Base/InputManager.hpp](EngineCore/Base/InputManager.hpp))이 `InputCommand`로 변환되지 않는다. 테스트는 `NetworkSyncSystem::QueueLocalInput`을 직접 호출한다.
- 클라이언트 예측·롤백 없음 — 클라는 입력 지연 + RTT만큼 뒤처진다.
- **결정론 스폰/디스폰** ⚠️ — `ApplySnapshot`이 새 `Registry`를 만들면서 `EntityManager`의 version / free list / next index를 복원하지 않는다. 그래서 스냅샷 이후 서버와 클라의 엔티티 index 할당이 갈라지고, `ComputeHash()`가 dense entity index를 섞으므로 곧바로 해시 불일치가 된다. 지금은 시뮬 도중 스폰을 하지 않아서 드러나지 않을 뿐이다. 건설·스폰이 필요한 작업에 들어가기 전에 반드시 해결할 것 — [roadmap.md §6](docs/design/roadmap.md) 참조.
- 렌더링 계층 전체 — `docs/design/rendering-dx11.md`에 설계만 있고 구현은 없다.
- `SystemRegistry::SortSystems()`는 TODO. 시스템은 등록 순서대로 실행된다.

## 사용할 수 있는 스킬

- `explain-diff-html` — 현재 diff / 브랜치 / PR을 배경·직관·코드 워크스루·퀴즈 5문항으로 설명하는 자체완결형 HTML을 만든다. 출력은 `C:\Users\macham\Documents\explanations\YYYY-MM-DD-explanation-<slug>.html`. 학습용 프로젝트이므로 큰 변경을 끝낸 뒤 이걸로 복습 자료를 만들면 좋다.
