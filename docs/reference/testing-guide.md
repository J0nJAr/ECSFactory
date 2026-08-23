# 테스트 가이드 — 무엇을 어떻게 확인하나

> 대상: 이 저장소를 이어받는 사람(그리고 에이전트).
> 이 문서는 **손으로 무엇을 어떻게 돌려보는가**를 다룬다. 무엇을 왜 만드는지는
> [roadmap.md](../design/roadmap.md), 각 계층의 설계는 `docs/design/*.md` 를 본다.

이 엔진의 어려운 점은 **"돌아간다"와 "맞다"가 다르다**는 것이다. 창이 뜨고 캐릭터가
움직여도 두 클라이언트의 시뮬레이션이 갈라져 있을 수 있고, 그건 화면만 봐서는 모른다.
그래서 확인 수단이 층마다 다르다.

| 층 | 확인 수단 | 사람이 필요한가 |
|---|---|---|
| 순수 로직(ECS·수학·레이아웃·입력 매핑) | 단위 테스트 | 아니오 |
| 네트워크 프로토콜·lockstep·복구 | 인메모리 링크 위 통합 테스트 | 아니오 |
| 렌더 파이프라인(ECS → DrawItem) | 가짜 백엔드로 단위 테스트 | 아니오 |
| GPU가 실제로 픽셀을 냈는가 | 백버퍼 캡처(PPM) | 아니오 |
| 창·입력·다중 프로세스 | 실행해서 눈으로 | **예** |

---

## 1. 기본 — 빌드와 전체 테스트

```powershell
cmake --preset default          # 구성 (x64-debug 상속)
cmake --build --preset default  # 전체 빌드 → out/build/default/bin
ctest --preset default          # 전체 테스트
```

테스트 하나만:

```powershell
ctest --preset default -R Network.DesyncRecoversWhileWorldKeepsMoving
./out/build/default/bin/EngineCoreTests.exe Network.DesyncRecoversWhileWorldKeepsMoving
```

> ⚠️ **테스트 바이너리는 첫 실패에서 멈춘다.** 하네스의 실패 함수가 `[[noreturn]]` 이라
> 그렇다. "실패가 하나뿐"으로 읽지 말 것 — 고치고 다시 돌리면 다음 실패가 나온다.
> 여러 실패를 한 번에 보려면 `ctest` 로 돌린다(케이스마다 프로세스가 따로 뜬다).

설계 규칙 검사(서버 바이너리가 DX11을 링크하지 않는지):

```bash
bash tools/ci/check-server-no-dx11.sh out/build/default
```

---

## 2. 로컬 실행 — 한 대에서

### 2.1 싱글플레이

```powershell
./out/build/default/bin/GameClient.exe --single
```

같은 프로세스 안에 `ServerSimulation` 월드를 만들고 인메모리 연결로 대화한다.
**별도 서버 프로세스가 뜨지 않는다.** 그래도 네트워크 코드 경로는 dedicated 와 동일하다 —
그게 이 엔진의 설계 요점이다.

### 2.2 멀티플레이 (같은 PC, 창 두 개)

터미널 셋을 띄운다.

```powershell
# 1번 창
./out/build/default/bin/GameServer.exe

# 2번 창
./out/build/default/bin/GameClient.exe --join 127.0.0.1

# 3번 창
./out/build/default/bin/GameClient.exe --join 127.0.0.1
```

**조작:** WASD 또는 방향키. 카메라가 자기 플레이어를 따라간다.
플레이어 색은 `clientId` 로 정해지므로 창마다 다른 색이 자기 캐릭터다.

> ⚠️ **한 번에 한 창만 조작된다.** 현재 입력 계층은 `GetAsyncKeyState`(시스템 전역)를
> 읽으므로, 포커스 없는 창은 입력을 보내지 않도록 `IWindow::HasFocus()` 로 막아 둔다.
> 이걸 안 막으면 키 한 번에 두 캐릭터가 같이 움직인다. 근본 해결은 창 메시지 기반
> 입력으로 옮기는 것이고 트랙 U6에서 어차피 필요해진다.

### 2.3 실제 다중 기기

서버가 뜬 PC의 LAN 주소를 클라이언트에 준다.

```powershell
# 서버 PC
./out/build/default/bin/GameServer.exe          # UDP 6780 을 연다

# 다른 PC
./out/build/default/bin/GameClient.exe --join 192.168.0.42
```

확인할 것:

1. **방화벽.** Windows가 첫 실행에서 물어본다. 놓쳤으면 인바운드 UDP 6780 을 직접 허용한다.
   접속이 조용히 실패하면 여기가 첫 번째 용의자다.
2. **포트는 `6780` 고정이다.** 지금은 명령줄로 못 바꾼다.
3. 서버 로그(`game_server.log`)에 `Dedicated server listening on UDP port 6780.` 이 있어야 한다.

접속이 성공하면 클라이언트 로그에 이 둘이 뜬다.

```
서버가 clientId 1를 배정했다.
첫 월드 스냅샷 수신 (tick 63). 이제부터 확정 입력으로 시뮬레이션한다.
```

이 로그가 있는 이유가 있다. **없으면 접속 실패가 완전히 조용하다** — 화면도 로그도
아무 말이 없어서 "서버가 안 떴나 / 패킷이 안 가나 / 스냅샷이 안 오나"를 구별할 수 없다.

---

## 3. 결정론 확인 — 두 시뮬레이션이 같은가

**이게 이 엔진에서 제일 중요한 확인이다.** 화면이 멀쩡해 보여도 갈라져 있을 수 있다.

### 3.1 창 제목을 본다

각 클라이언트 창 제목이 이렇게 나온다.

```
ECSFactory [join] tick=1523 hash=36F0DEF161A59F3F
```

- `tick` — 그 클라가 확정 입력으로 시뮬레이션한 tick(`clientSimTick`)
- `hash` — 그 시점 월드의 결정론 체크섬(`World::ComputeHash()`)

**두 창이 같은 `tick` 에서 같은 `hash` 를 보이면 일치하는 것이다.**
`[디싱크!]` 가 붙으면 `TickHash` 비교가 불일치를 잡은 것이다(그 뒤 resync가 돈다).

> ⚠️ **tick 이 다른 두 창의 hash 를 비교하면 안 된다.** 클라는 서버보다 항상 조금
> 뒤처지고 두 클라의 진행도 미세하게 다르다. 반드시 **같은 tick** 끼리 본다.
>
> ⚠️ 화면 안에 글자를 그리는 진짜 디버그 오버레이는 트랙 U의 U5다. 제목 표시줄은
> 폰트 계층(U1·U2) 없이 지금 쓸 수 있어서 임시로 쓰는 것이다.

### 3.2 자동으로 대조하기

제목을 주기적으로 읽어 같은 tick 끼리 묶으면 사람이 안 봐도 된다. 이 저장소의 검증에
쓴 방식이 그것이다 — Windows `EnumWindows` 로 `ECSFactory` 로 시작하는 창을 찾고
`tick=(\d+) hash=([0-9A-F]+)` 를 뽑아 tick 별로 모은 뒤 값이 하나인지 본다.

실측(2026-08-23): 깨끗한 회선에서 같은 tick 표본 **32/32 일치**.

### 3.3 픽셀로 대조하기

`--capture` 로 백버퍼를 PPM으로 뽑아 두 클라의 화면을 직접 비교할 수 있다.

```powershell
./out/build/default/bin/GameClient.exe --join 127.0.0.1 --frames 300 --capture c1.ppm
```

> ⚠️ **두 클라의 캡처 시점이 같아야 의미가 있다.** 프로세스를 다른 시각에 띄우면
> 서로 다른 tick 의 화면을 비교하게 되어 당연히 다르다. 프레임 수를 같게 하고
> 거의 동시에 띄울 것.
>
> PPM을 고른 이유: 헤더가 세 줄뿐이라 인코더도 파서도 필요 없다.
> `P6\n<width> <height>\n255\n` 뒤에 RGB 바이트가 그대로 이어진다.

실측: 동시 캡처한 두 화면이 921,600픽셀 중 **0개 차이**.

---

## 4. 지연·손실 테스트

복구 경로(재전송, `TickHash` 불일치 → resync)는 **정상 세션에서 한 번도 실행되지 않는다.**
실행되지 않는 코드는 조용히 썩고, 정작 필요한 날 처음 돌아간다.
그래서 실제 네트워크가 나빠지기를 기다리지 말고 **주입한다.**

```powershell
./out/build/default/bin/GameClient.exe --join 127.0.0.1 `
    --net-loss 30 --net-delay 1-4 --net-dup 5 --net-seed 20260823
```

| 인자 | 뜻 |
|---|---|
| `--net-loss 30` | 데이터그램 30%를 버린다 |
| `--net-delay 1-4` | 1~4틱 지연(지터). 고정 timestep 20ms 기준 20~80ms |
| `--net-delay 3` | 고정 3틱 지연 |
| `--net-dup 5` | 5%를 한 번 더 보낸다 |
| `--net-seed N` | **재현의 핵심.** 같은 시드 + 같은 트래픽 = 같은 손실 패턴 |

동작 방식과 근거는 `EngineCore/Network/LossyConnection.hpp` 와
[network-sync-system.md](../design/network-sync-system.md) 에 있다. 요점만:

- **송신 경로에만** 주입한다. 양방향을 열화시키려면 양 끝을 각각 감싸야 하고,
  그래서 비대칭 회선(업만 나쁜 링크)도 재현된다.
- 지연은 시간이 아니라 **tick** 단위다. wall-clock 을 쓰면 테스트가 진짜로 sleep해야 하고
  스케줄러에 따라 결과가 흔들려서, 재현성을 위해 만든 물건이 재현성을 깨게 된다.
- **지터가 재정렬을 겸한다** — 먼저 보낸 것이 더 큰 지연을 뽑을 수 있다.
- 파라미터가 0이면 난수도 큐도 건드리지 않고 그대로 통과한다. 기본값이 무해해야
  실수로 켜둔 채 넘어가지 않는다.

**무엇을 기대하나:** 손실이 있어도 **해시는 계속 일치해야 한다.** 신뢰성 계층이
재전송으로 복구하기 때문이다. 실측: 한쪽에 손실 30% + 지터 + 중복을 넣고
같은 tick 표본 **31/31 일치**, `[디싱크!]` 표시 없음.

> ⚠️ 지금은 **클라이언트에만** 붙는다. 서버→클라 방향 열화는 아직 CLI로 못 켠다.
> `LossyConnection` 은 양 끝을 감싸는 구조를 지원하므로 필요해지면 `GameServer` 에
> 같은 플래그를 붙이면 된다.

---

## 5. 디싱크 테스트

### 5.1 코드에서 (권장)

`NetworkSyncSystem::DebugSkipNextConfirmedInputFrameOnce()` 가 **다음 확정 `InputFrame`
하나를 받았지만 적용하지 않는다.** tick 번호는 정상 진행시킨다 — 여기서 멈추면 디싱크가
아니라 그냥 뒤처진 클라이언트가 되어 재현하려는 상황이 아니게 된다.

```cpp
clientSync.DebugSkipNextConfirmedInputFrameOnce();
tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
EXPECT_TRUE(clientSync.HasHashMismatch());   // 탐지
```

**왜 이 형태인가:** 현실의 lockstep 디싱크는 대부분 "한쪽이 확정 입력을 놓쳤거나 다르게
적용했다"이다. 이 주입이 만드는 클라 월드는 여전히 *시뮬레이션이 만들어낼 수 있는* 상태이면서
서버와 한 스텝 어긋나 있다. 컴포넌트 값을 아무 숫자로 덮는 것보다 실제 버그에 가깝다.

기본 꺼짐이고, 한 번 발동하면 스스로 꺼진다(1회성). 켜 둔 걸 잊어 세션이 망가지는 사고가 없다.

> ⚠️ **주입한 프레임이 월드를 실제로 바꾸지 않으면 디싱크가 안 생긴다**(예: `buttonMask` 가
> 전부 0). 이걸 쓰는 테스트는 주입 직후 해시가 실제로 갈라졌는지를 **함께 단언해야 한다.**
> 아니면 "아무 일도 안 일어나서" 통과한다.

관련 테스트: `Network.SkippedConfirmedInputFrameIsDetectedByTickHash`(탐지),
`Network.DesyncedClientRecoversThroughResyncSnapshot`(복구),
`Network.DesyncRecoversWhileWorldKeepsMoving`(**움직이는 동안** 복구),
`Network.ResyncRequestDoesNotStormOnSingleDesync`(요청 폭주 없음),
`Network.NoInjectionKeepsCleanSessionFreeOfHashMismatch`(대조군).

### 5.2 ⚠️ 정지한 월드로 검증하면 안 된다

이 저장소가 실제로 데인 곳이다. **월드가 정지해 있으면 복구가 되는 것처럼 보인다.**

스냅샷 라벨 off-by-one 때문에 resync 가 확정 입력을 한 프레임씩 삼키던 버그가 있었는데,
멈춘 월드에서는 삼킨 프레임이 no-op 이라 우연히 복구됐다. 그래서 기존 테스트가 전부
통과하면서 결함이 오래 숨어 있었다. 계속 움직이는 상태에서는 200 tick / 스냅샷 200개를
받고도 복구되지 않았다.

**디싱크 테스트는 반드시 입력을 계속 넣으면서 복구를 요구할 것.**

### 5.3 해시 직접 비교의 함정

```cpp
// ❌ 이렇게 쓰면 멀쩡한 코드가 깨진 것처럼 보인다
EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
```

클라는 서버보다 항상 최소 한 tick 뒤처지므로 이 비교는 **월드가 정지해 있을 때만** 참이다.
정상 동작을 확인하려면 tick 을 맞춰 비교하는 `HasHashMismatch()` 를 본다.

---

## 6. 사람 없이 돌리기

창 포커스에 묶이지 않는 검증이 필요할 때(CI, 자동 스크립트, 원격) 쓰는 인자들.

| 인자 | 뜻 |
|---|---|
| `--frames N` | N프레임만 돌고 스스로 종료 |
| `--capture x.ppm` | 마지막 프레임 백버퍼를 PPM으로 저장 |
| `--input-script "20:W,80:"` | **가짜 키보드.** tick 20에 전진, tick 80에 정지 |

입력 스크립트 형식은 `tick:키들,tick:키들,...` 이고 키는 `WASD` 다.
빈 키 목록(`80:`)은 정지를 뜻한다. 항목이 없는 tick 에서는 **직전 마스크를 유지**하므로
"20틱에 누르고 80틱에 뗀다"를 두 항목으로 적을 수 있다.

**왜 있나:** 실제 키보드 검증은 창 포커스에 묶여 있다. 창이 둘이면 어느 쪽에 키를 줄지
사람이 정해야 하고, 자동화에서 `SetForegroundWindow` 는 신뢰할 수 없다.
스크립트는 **tick 기준**이라 프레임률과 무관하게 매번 같은 결과가 나오고,
실제 키보드와 **같은** `QueueLocalInput` 경로를 탄다 — 검증하는 길이 실제 길과 다르지 않다.

전체 조합 예시:

```powershell
# 서버 + 클라 둘. 한쪽은 회선이 나쁘고, 각자 다른 스크립트로 움직인다.
./out/build/default/bin/GameServer.exe

./out/build/default/bin/GameClient.exe --join 127.0.0.1 --frames 400 `
    --input-script "100:W,120:" --capture c1.ppm

./out/build/default/bin/GameClient.exe --join 127.0.0.1 --frames 400 `
    --input-script "150:D,170:" --capture c2.ppm `
    --net-loss 30 --net-delay 1-4 --net-seed 20260823
```

---

## 7. 새 테스트를 쓸 때

이 저장소의 규칙 하나만 기억하면 된다.

> **빨간불을 먼저 확인한다.**

수정을 넣기 전(또는 수정을 잠깐 되돌리고) 테스트가 **실제로 실패하는 것을 보고**,
실패 양상이 예측과 맞는지 확인한다. 이걸 안 하면 "테스트가 통과했다"와
"테스트가 아무것도 안 본다"가 구별되지 않는다.

실제로 이 저장소에서 그렇게 잡은 것들:

- 스냅샷에 `EntityManager` 상태를 넣는 수정 — `RestoreEntities` 호출을 빼니 네 개가 전부
  실패했고, 클라가 5를 뽑을 때 서버는 `4294967299`(index 3, version 1)를 뽑는 것까지 예측과 맞았다.
- resync 라벨 off-by-one 수정 — `SimulatedThroughTick` 을 되돌리니
  `DesyncRecoversWhileWorldKeepsMoving` 이 `recovered` 에서 실패했다.

그 외:

- 케이스는 `TEST_CASE(Suite, Name)` 으로 등록하고, `tests/CMakeLists.txt` 에 대응하는
  `add_test(...)` 줄도 추가한다(개별 ctest 항목으로 돌리기 위해).
- 와이어를 타는 것은 **구조체를 직접 넘기지 말고 인코딩·디코딩 왕복을 태운다.**
  구조체만 복사하면 인코더가 새 필드를 통째로 빠뜨려도 테스트가 통과한다.
- 렌더 계층은 `IRenderer`/`IWindow` 가 인터페이스라 **가짜 구현으로 GPU 없이** 테스트된다
  (`tests/RenderTests.cpp` 의 `RecordingRenderer`/`FixedSizeWindow`).
  단 `IWindow` 에 순수 가상 함수를 추가하면 이 가짜도 따라가야 한다 — 컴파일러가 잡아 준다.

---

## 8. 아직 못 하는 것

정직하게 적어 둔다. 여기 있는 것을 "됐다"고 오해하지 않기 위해서다.

- **캡처 이미지의 자동 비교가 없다.** `--capture` 는 사람이 열어 보거나 스크립트가
  픽셀을 세는 용도이고, 기준 이미지를 저장소에 두고 회귀를 잡는 장치는 없다.
- **창·입력 자체의 단위 테스트가 없다.** 실제 `HWND` 와 데스크톱 세션이 필요해서,
  헤드리스 CI에서 만들면 환경에 따라 조용히 실패하거나 걸린다.
- **서버 쪽 링크 열화를 CLI로 못 켠다**(§4).
- **접속 해제 경로가 없다.** 나간 클라이언트의 플레이어 엔티티가 영원히 남고,
  재접속하면 새 `clientId` + 새 스폰이 되어 유령이 쌓인다. 장시간 테스트에서 이걸 감안할 것.
- **부하 테스트가 없다.** 클라이언트 수, 엔티티 수를 늘렸을 때의 한계를 재본 적이 없다.
