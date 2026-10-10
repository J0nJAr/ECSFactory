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
2. **`explain-diff-html` 스킬로 그 변경의 설명 문서를 만든다** — 배경·직관·코드 워크스루·퀴즈가 나온다. 출력은 홈 아래 `~/Documents/explanations/`(저장소 밖)에 날짜 접두로 쌓인다. 학습용 프로젝트이므로 이게 결과물의 일부다.
3. 설계 판단이 바뀌었으면 해당 `docs/design/*.md`를 고친다. 근거를 대화에만 남기지 않는다.

마일스톤을 크게 묶으면 설명 문서가 소화 불가능한 크기가 된다. 그래서 크게 묶지 않는다.

### 문서 언어 규칙

소유자는 **한국어 사용자**다. 사람이 읽는 것은 전부 **한국어**로 쓴다: 코드 주석, `README.md`, `docs/design/*`, 커밋 메시지, 새로 만드는 모든 마크다운. 명시적으로 요청받지 않는 한 이 저장소에 영어 문서를 만들지 않는다.

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
cmake --preset default          # 구성 (x64-debug 상속). 그 외: x64-debug, x64-release, msvc-debug, msvc-release
cmake --build --preset default  # 전체 빌드 → out/build/default/bin
ctest --preset default          # 전체 테스트 (실패 시 출력 표시)
```

**기준 툴체인은 g++다.** Visual Studio 2022 MSVC용 `msvc-debug`/`msvc-release` 프리셋은 디버거를 쓰기 위한 **보조 경로이고 CI가 검증하지 않는다** — 쓰기 전에 `msvc-debug` 테스트를 직접 돌리고, 깨져 있으면 고치되 g++ 경로를 막는 결함으로 취급하지 않는다. 근거는 [development-tools.md](docs/reference/development-tools.md)의 "기준 툴체인은 g++, MSVC는 보조" 절. MSVC 프리셋과 함께 개발 런처(`Launch-ECSFactory.cmd`, `tools/Start-DevLauncher.ps1`), VS Code 태스크·디버그 구성도 있다. 프리셋 표와 준비물은 [docs/reference/development-tools.md](docs/reference/development-tools.md)가 정본이다.

> ⚠️ **MSVC와 g++는 같은 `Fixed64` 결과를 내야 한다.** 어느 컴파일러로 빌드해도 같은 시뮬레이션이어야 테스트·재현 결과를 서로 비교할 수 있다. 단, 서로 다른 컴파일러로 빌드한 서버·클라가 **한 세션에 섞이는 것은 보장하지 않는다** — 스냅샷이 trivial 컴포넌트의 메모리 배치를 그대로 싣고, 타입 이름 추출이 컴파일러별 경로(`__FUNCSIG__`/`__PRETTY_FUNCTION__`)라 두 컴파일러의 타입 해시가 같다는 보장이 없기 때문이다(실제로 다른지는 확인하지 않았다). 한 세션은 같은 프리셋으로 돌린다. [Math/Internal.hpp](EngineCore/Math/Internal.hpp)의 128비트 곱은 음수 소수부를 GCC의 산술 시프트처럼 **-∞ 방향으로 내리고**(0 방향 절삭이면 MSVC만 갈라진다), `_udiv128`에는 몫 overflow 예외를 피하려고 `high % divisor`를 넘긴다. 이 경로를 고칠 때는 두 컴파일러로 수학 테스트를 모두 돌린다.

실행 파일은 `out/build/<preset>/bin/`에 떨어진다(`EngineCoreTests.exe`, `GameClient.exe`, `GameServer.exe`).

빌드 타깃은 셋이다: `EngineCore`(로직) → `EngineRender`(창·렌더, `EngineCore`를 링크) → `GameClient`/`GameServer`. **`GameServer`는 `EngineRender`를 링크하지 않는다** — dedicated server에 DX11이 들어가면 안 된다. CI가 이걸 검사한다.

### CI와 버전

- **로컬 pre-push 훅** — 푸시 직전 빌드+테스트. 설치: `powershell -File tools/Install-GitHooks.ps1` (이 환경엔 `pwsh`가 없다. Windows PowerShell 5.1을 쓴다).
- **GitHub Actions** ([.github/workflows/ci.yml](.github/workflows/ci.yml)) — MSYS2 UCRT64에서 Debug/Release 둘 다 빌드·테스트. CI는 `ci-debug`/`ci-release` 프리셋(Ninja)을 쓴다. 로컬 프리셋은 컴파일러 절대경로를 박고 있어 러너에서 못 쓴다.
- **설계 규칙 검사** ([tools/ci/check-server-no-dx11.sh](tools/ci/check-server-no-dx11.sh)) — 문서에만 적힌 규칙은 결국 깨진다. **불변식은 가능하면 테스트나 CI로 못 박을 것.**
- **버전** — 마일스톤마다 git 태그 `milestone/<id>`, 트랙이 끝나면 `project(VERSION)` minor 상승. 코드에서는 `EF_VERSION_STRING`으로 보인다.

> ⚠️ 한국어가 든 `.ps1`은 **UTF-8 BOM으로 저장**해야 한다. Windows PowerShell 5.1은 BOM 없는 UTF-8을 ANSI로 읽어서 출력이 깨진다.

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
powershell -File tools/Create-CppArtifact.ps1 -Target EngineCore   -Kind pair -RelativePath ECS/MyThing.hpp
powershell -File tools/Create-CppArtifact.ps1 -Target EngineRender -Kind pair -RelativePath Render/Dx11/Foo.hpp
```

파일을 만들고 대상 타깃의 `CMakeLists.txt`에 등록까지 해준다. VS Code 태스크 **"CMake: Create C++ Artifact"**로도 같은 걸 할 수 있다. `-Kind`는 `pair`/`source`/`header`, `-Target`은 `EngineCore`/`EngineRender`/`GameClient`/`GameServer`/`tests`.

`-RelativePath`의 확장자는 있어도 없어도 되고(`ECS/MyThing` = `ECS/MyThing.hpp` = `ECS/MyThing.cpp`), 같은 명령을 두 번 돌려도 중복 등록되지 않는다. 소스 목록이 이미 정렬되어 있으면 정렬 위치에 끼워 넣는다.

**`-Target EngineRender`가 곧 "서버가 이걸 링크하지 않는다"는 선언이다.** `EngineRender`의 소스는 경로상 `EngineCore/` 아래에 있지만(`Render/`, `Platform/`, `UI/`) 등록은 `EngineCore/CMakeLists.txt`의 **두 번째** `target_sources` 블록으로 간다. 타깃을 잘못 고르면 빌드는 통과하고 설계 규칙만 조용히 깨진다 — 그래서 스크립트가 디렉터리로 추측하지 않고 `-Target`을 그대로 따른다.

안전장치: CMakeLists를 쓰기 전에 "원본의 모든 줄이 순서대로 남아 있고, 늘어난 줄은 방금 넣은 것뿐"을 검증하고, 어긋나면 **쓰지 않고 중단**한다. 예전에 이 파일을 통째로 비워버린 사고가 있었기 때문이다(그 사고 모드를 재현하는 시뮬레이션으로 차단을 확인했다).

> 예전에 여기 적혀 있던 결함 둘(`MyThing..hpp` 이중 점, `EngineRender` 타깃 부재)은 해결됐다.
> 함께 드러나 고친 것: 파일 전체를 문자열로 치환하던 탓에 `EngineCore`에 넣은 `.cpp`가 **`EngineRender` 블록에도 같이 등록**되던 문제, 헤더 등록 경로가 잘못된 정규식(`^\\s*\\)`)으로 항상 예외를 던지던 문제, CRLF 파일에서 중복 검사가 절대 맞지 않던 문제(.NET의 multiline `$`는 `\r`를 건너뛰지 않는다).

### clangd / IDE 참고

`.clangd`의 `CompilationDatabase`는 `out/build/x64-debug`를 가리킨다. 코드 인텔리전스가 동작하려면 그 프리셋을 최소 한 번 구성해서(`cmake --preset x64-debug`) 거기에 `compile_commands.json`이 있어야 한다 — `default` 프리셋은 다른 디렉터리에 빌드한다.

## 문서

`docs/`는 이 파일보다 자세하고, 코드와 함께 갱신되고 있다.

- **[docs/design/roadmap.md](docs/design/roadmap.md) — 마일스톤 전체 계획.** 지금 무엇을 왜 그 순서로 만드는지가 여기 있다. "다음에 뭘 해야 하나"는 이 문서를 먼저 본다.
- **[docs/reference/testing-guide.md](docs/reference/testing-guide.md) — 테스트 가이드.** 무엇을 어떻게 확인하는가. 로컬·다중 기기 실행, 결정론 대조, 지연·손실·디싱크 주입, 새 테스트를 쓸 때의 규칙(빨간불 먼저).
- **[docs/reference/milestone-codes.md](docs/reference/milestone-codes.md) — 마일스톤 기호 사전.** `R2`·`N1`·`U8` 같은 기호가 무엇을 가리키고 지금 어떤 상태인지의 대조표. **이 기호는 저장소 안에서만 쓴다.** 외부 설명에는 뜻이 드러나는 말로 풀어 쓴다.
- `docs/design/*.md` — 다음 에이전트·개발자를 대상으로 한 **핸드오프 스펙**: 근거, 불변식, 마일스톤 상태, 함정. 입력·동기화를 건드리기 전에 [docs/design/deterministic-input-lockstep.md](docs/design/deterministic-input-lockstep.md)를, 렌더링을 건드리기 전에 [docs/design/rendering-dx11.md](docs/design/rendering-dx11.md)를, UI를 건드리기 전에 [docs/design/ui-layer.md](docs/design/ui-layer.md)를 읽는다. 뒤의 둘은 **설계 대비 구현이 앞부분만 들어와 있다** — 렌더링은 R0~R6(창·device·카메라·ECS 연결·실제 입력), UI는 U3(앵커 레이아웃)까지다. 나머지는 아직 문서뿐이므로, 각 문서의 마일스톤 표에서 지금 어디까지 왔는지를 먼저 확인할 것.
- 블로그 원고(`docs/blog/`), 스킨(`blog/`), 전용 문체 기준·검사 도구는 로컬 파일이며 `.gitignore`로 제외한다. 엔진 작업에 함께 커밋하지 않는다.
  - 티스토리(`machamy.tistory.com`) 발행 현황은 로컬 `docs/blog/README.md`에 적는다. 발행은 붙여넣기 수동 작업이라 로컬 원고와 발행본이 어긋날 수 있다. 확실하지 않으면 `https://machamy.tistory.com/rss`로 확인한다.

`README.md`의 "작업 스케치" 섹션이 살아 있는 TODO 목록이다.

## 아키텍처

### 애플리케이션 수명주기

앱은 `GameEngine` 하나를 소유하는 `IGameApp`(별칭 `GameAppBase`, [Base/IGameApp.hpp](EngineCore/Base/IGameApp.hpp))을 상속한다. `IGameApp::Run()`이 순서를 고정한다: `OnConfigure` → `OnInitialize`(여기서 월드·시스템·리소스 생성) → `engine.Initialize()` → `OnRun` → `OnShutdown` → `engine.Shutdown()`. [GameClient/main.cpp](GameClient/main.cpp), [GameServer/main.cpp](GameServer/main.cpp) 참조.

### GameEngine과 World

`GameEngine`([Base/GameEngine.cpp](EngineCore/Base/GameEngine.cpp))은 여러 `World`(각각 `WorldRole` — `ServerSimulation` / `ClientPresentation` — 로 태깅됨)와 고정 timestep 누산기 루프(기본 20ms / 50Hz)를 소유한다. 이 루프가 **결정론적인 `FixedUpdate(Fixed64 dt, tick)`**(누산기 기반이라 프레임당 0~n회 실행)와 가변 주기 `FrameUpdate(float dt)`를 구동한다. `dt`는 정수 `m_fixedTimeStepUs`에서 고정소수점으로 유도한다 — double에서 만들지 않는다. 테스트에서는 `ManualStepFixed` / `ManualStepFrame`으로 루프를 돌린다.

**핵심 설계 포인트:** 호스트와 싱글플레이는 별도 서버 프로세스를 띄우지 *않는다*. 클라이언트 엔진이 *같은 `GameEngine` 안에* `ServerSimulation` 월드를 추가하고 인메모리 연결로 대화한다 — 그래서 single / host / dedicated가 하나의 네트워크 코드 경로를 공유한다. 기억할 귀결 하나: 같은 엔진의 월드들은 **tick 클럭을 공유**하고, dedicated는 공유하지 않는다(아래 클럭 추정 참조).

### ECS 계층 ([EngineCore/ECS/](EngineCore/ECS/))

- **`EntityId`** ([Core.hpp](EngineCore/ECS/Core.hpp)): 기본 64비트(`EF_ENTITY_ID_64`, index 32 : version 32). 해제된 index를 재사용하면 version이 올라간다. `GetEntityIndex` / `GetEntityVersion` / `CreateEntityId` 헬퍼를 쓰고, 센티널은 `kNullEntity`. `EntityManager::IsAlive`는 version뿐 아니라 **index별 생존 플래그**(free list에서 유도되는 캐시)까지 본다 — 회수됐지만 아직 재사용되지 않은 index로 만든 id도 죽은 것으로 판정된다. `ResolveEntityId(index)`는 그런 index에도 id를 돌려주므로, 생존은 항상 `IsAlive`로 물을 것.
- **`Registry`**: `EntityManager`와 타입별로 인덱싱된 `vector<unique_ptr<IPool>>`을 소유한다. 엔티티 생성·삭제, 컴포넌트 추가·제거, `CreateView<...>()`.
- **`ComponentPool<T>`**: `SparseSet<EntityIndex, T>`를 감싸고 `IPool` 인터페이스로 dense 바이트 span을 노출한다 — 스냅샷과 해시가 순회하는 게 이것이다.
- **`View<With<...>, Without<...>>`**: 쿼리에 맞는 엔티티를 순회한다. 효율을 위해 **필수 풀 중 가장 작은 것**부터 시작한다. `view.ForEach([](EntityId, A&, B&){...})`(엔티티 인자는 선택)과 range-`for` 둘 다 된다.
- **`World`** ([World.hpp](EngineCore/ECS/World.hpp)): `Registry` + `SystemRegistry` + 타입 키 **리소스 맵**의 조합. 리소스(`AddResource<T>` / `GetResource<T>`)는 월드별 싱글턴을 담는다 — `NetworkManager`가 이렇게 붙는다.

`EngineCore/Feature/`, `Systems/`는 아직 빈 자리표시 디렉터리다.

> ⚠️ **디렉터리 위치와 CMake 타깃이 일치하지 않는다.** `EngineCore/Render/`, `EngineCore/Platform/`, `EngineCore/UI/`는 경로만 `EngineCore` 아래일 뿐 **`EngineRender` 타깃에 등록되어 있다**(둘 다 [EngineCore/CMakeLists.txt](EngineCore/CMakeLists.txt)에 있고, 두 번째 `target_sources` 블록이 `EngineRender`다). 파일을 추가할 때 어느 블록에 넣는지가 곧 "서버가 이걸 링크하는가"를 정한다.

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

월드 리소스([NetworkManager.hpp](EngineCore/Network/NetworkManager.hpp))로, 연결 하나와 endpoint↔clientId 테이블, endpoint별 `ReliabilityChannel`을 소유한다. 패킷 타입([Packet.hpp](EngineCore/Network/Packet.hpp)): `Hello`, `TypeTable`, `WorldSnapshotRequest`, `WorldSnapshot`, `InputCommand`, `TickHash`, `ResyncRequest`, `Welcome`, `InputFrame`, `PartialSnapshot`. 모든 타입에 static `Encode*`/`Decode*`가 있다(`NetUtils.hpp`의 `PacketWriter`/`PacketReader`로 네트워크 바이트 오더 직렬화. `Fixed64`는 `Raw()`로 나간다).

> ⚠️ **와이어 버전에 관한 정확한 사실** (예전 문서에 틀린 내용이 있었다):
> - 매직 넘버 `EFSY`는 **코드에 존재하지 않는다.** 설계 초안에만 있던 것이다.
> - `Packet.hpp`의 `kProtocolVersion`은 **지금은** 선언만 되어 있고 아무도 읽지 않는다. 올려도 아무 효과가 없다 — 구버전 상대는 여전히 조용히 깨진 데이터를 읽는다.
>   (역사적 주의: 처음부터 죽은 상수는 아니었다. 신뢰성 계층 도입 전에는 `NetworkManager`가 프레임 첫 바이트에 이 값을 쓰고 수신 시 검사했다. 07에서 프레임 포맷이 신뢰성 데이터그램으로 바뀌면서 그 경로가 사라졌다. 옛 커밋을 읽을 때 헷갈리지 말 것.)
> - 실제로 와이어에 실리고 **검사까지 되는** 버전은 전송 계층의 `kReliabilityVersion`뿐이다(`ReliabilityChannel.cpp`. 데이터그램 헤더에 쓰고, 불일치하면 거부).
>
> 앱 계층 버전 검사가 필요해지면 `Hello`/`Welcome` handshake에서 비교하는 게 자연스럽다. 그때 `EF_VERSION_*`와 [serialization.md](docs/design/serialization.md) §5의 스키마 해시를 함께 실으면 된다.

서버 쪽 클라이언트별 상태는 `NetworkClientState`다: `endpoint`, 조종 엔티티, 타입테이블·스냅샷 플래그, **`inputByTick` 슬롯 맵**, 마지막 확정 입력. `ClientIdForEndpoint`가 스푸핑 방지 조회다 — 페이로드에 실린 clientId가 아니라 endpoint로 인가한다.

매 tick 둘 다 호출되어야 하는 것:

- `SendX(...)`는 즉시 flush하지만, **재전송과 누적 ack를 굴리는 건 `Update(tick)`**이다 — `NetworkSyncSystem::FixedUpdate` 끝에서 호출한다. 이 펌프가 안 돌면 신뢰성 트래픽이 손실에서 절대 복구되지 않는다.
- `TryReceive()`는 순서·재조립이 끝난 다음 패킷 하나를 보낸 사람 endpoint와 함께 돌려준다(`TryReceivePacket()`은 endpoint를 빼는 래퍼).

### 스냅샷 모델 (`World::CreateSnapshot` / `ApplySnapshot`)

- **trivially-copyable** 컴포넌트만 스냅샷 대상이고, raw 바이트로 복사된다. 비trivial 컴포넌트는 제외된다.
- 클라이언트는 타입마다 `World::RegisterSnapshotComponent<T>()`로 명시 등록해야 한다(trivially-copyable을 static_assert). `ApplySnapshot`은 레지스트리를 처음부터 재구성하고, `hash`/`size`/`alignment`/`isTrivial`/바이트 수가 안 맞는 타입은 **거부**한다.
- 스냅샷은 **조인과 resync 전용**이다 — 매 tick 상태 스트리밍이 아니다(아래 lockstep 참조). resync는 **부분 스냅샷이 먼저**다: 클라가 불일치를 본 tick의 해시 분해표를 보내면 서버가 어긋난 풀만 `PartialSnapshot`으로 보내고(`World::CreatePartialSnapshot` / `ApplyPartialSnapshot`, 전부 아니면 전무), 엔티티 할당이 갈라졌거나 부분 복구로 안 맞으면 전체 `WorldSnapshot`으로 폴백한다([partial-resync.md](docs/design/partial-resync.md)).
- `ComponentSnapshot.entities`에는 `EntityIndex`만 실리고 **version은 실리지 않는다.** 따라서 컴포넌트가 `EntityId`로 다른 엔티티를 참조하면 왕복에서 살아남지 못한다. 복제되는 링크는 안정적인 도메인 id(현재는 `clientId`)로 걸어야 한다.
- `World::ComputeHash()`가 결정론 체크섬이다. 실제 계산은 `ComputeHashBreakdown()`이 층별로 한다 — `EntityManager` 상태 해시, trivial 풀마다의 해시, 그리고 그 둘에서 **유도한** 월드 해시(계산 경로는 하나뿐이다. 부분 resync가 풀 단위로 비교하기 위한 구조, [partial-resync.md](docs/design/partial-resync.md)). 월드 해시가 `TickHash` 비교 → `ResyncRequest`를 구동한다. 풀 순회는 **`TypeInfo::hash` 오름차순**이다 — 프로세스 로컬인 `index` 순서로 돌면 타입 첫 사용 순서가 다른 두 프로세스가 같은 월드에서 다른 해시를 낸다. `RegisterPresentationComponent<T>()`로 등록된 클라 전용 표현 컴포넌트는 해시에서 빠진다. 엔티티 할당은 뺄 방법이 없으므로 **어떤 peer도 월드 엔티티를 확정 경로 밖에서 만들거나 지우면 안 된다**(N4.1).

## 결정론 입력 lockstep

이 엔진은 상태가 아니라 **입력**을 중계한다. 서버와 모든 클라이언트가 같은 확정 입력셋으로 같은 시뮬레이션을 돌린다. `Fixed64`·`TickHash`·resync가 존재하는 이유가 바로 이것이다. 전체 스펙과 근거는 [docs/design/deterministic-input-lockstep.md](docs/design/deterministic-input-lockstep.md)에 있다.

tick당 흐름:

```
클라:   로컬 입력 수집 → applyTick = serverTickEstimate + 2 (kInputDelayTicks)
        InputCommand ──unreliable, 최대 3회 재전송(kInputRedundancySends)──▶ 서버
서버:   EnqueueInput()이 그 클라의 비어 있는 inputByTick 슬롯을 채움
        events = NetworkManager::ConfirmSessionEvents(tick) // 예약된 Join/Leave. clientId 정렬
        ApplySessionEvents(serverWorld, events)             // 스폰·디스폰 (입력보다 먼저)
        confirmed = NetworkManager::ConfirmInputs(tick)   // clientId 정렬. 결측은 직전 입력 hold
        ApplyConfirmedInputs(serverWorld, confirmed)      // 서버가 먼저 시뮬
        InputFrame(tick, events, confirmed) ──reliable──▶ 모든 클라
        TickHash(tick, world.ComputeHash()) ──sequenced──▶ 모든 클라
클라:   InputFrame을 버퍼링 → clientSimTick+1 프레임이 있으면 events → inputs 순으로 적용하고 ++clientSimTick
        TickHash 불일치 → ResyncRequest(그 tick의 해시 분해표)
        서버: 어긋난 풀만 PartialSnapshot(X = tick-1)   또는   전체 WorldSnapshot(폴백)
        클라: clientSimTick == X에서 풀을 갈아끼우고 즉시 worldHash 검증 → 실패하면 전체 요청
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

> 이 절은 **2026-08-23 기준**이다. 에이전트 브랜치를 병합할 때 옛 문구가 다시 섞여 들어온 적이 있다
> (고친 버그가 여기 "아직 있다"로 남아 있었다). 여기 적힌 결함을 고치기 전에 코드와 테스트로 먼저 확인할 것.

동작하고 테스트로 덮여 있는 것: ECS, 고정소수점 수학, 스냅샷·동기화, 다중 클라이언트 라우팅을 갖춘 신뢰성 UDP 전송, 결정론 입력 lockstep 루프(M1~M7. 시드 기반 손실·재정렬 링크에서의 수렴 테스트 포함), **결정론 스폰·디스폰**(N1·N2. 접속·해제가 확정 이벤트로 흐른다), 타임아웃 기반 접속 해제, 디싱크 탐지·복구(주입 테스트 포함), 직교 카메라(R4), `RenderSystem`(R5. 가짜 백엔드로 GPU 없이 테스트), 입력 매핑과 조준의 결정론 경계(I1), UI 앵커 레이아웃(U3).

동작하지만 **자동 테스트가 없는** 것: DX11 백엔드(R2·R3), 창(R1), 실제 키보드 배선과 카메라 추적(R6). device 생성과 창이 GPU·데스크톱 세션을 요구해서 하네스로 감싸지 않았다. 대신 실행해서 확인한다 — `--capture` 로 백버퍼를 PPM으로 뽑고, 창 제목의 `tick`·`hash` 를 두 클라에서 대조한다. 방법은 [testing-guide.md](docs/reference/testing-guide.md). 기준 이미지 비교는 아직 없다.

### 지켜야 하는 계약 (어기면 조용히 깨진다)

- 📌 **스냅샷 tick 라벨은 `SimulatedThroughTick(tick)` = `tick - 1`이다.** 서버의 한 tick은 `수신 처리 → 입력 확정·적용 → TickHash 방송` 순이고 스냅샷은 첫 단계에서 만들어지므로, 그 월드는 아직 `tick`의 입력을 적용하지 않은 상태다. `tick`으로 붙이면 클라가 `InputFrame{tick}`을 "이미 지난 것"으로 버린다 — resync마다 확정 입력을 한 프레임 삼키던 버그였다. 정지한 월드에서는 삼킨 프레임이 no-op이라 우연히 복구되어 오래 숨어 있었다. `Network.DesyncRecoversWhileWorldKeepsMoving`이 지킨다.
- 📌 **월드를 바꾸는 사건은 확정 경로로만.** 접속·해제는 서버가 `QueueSessionEvent(tick + 1, …)`로 예약하고 `InputFrame.events`에 실려 모든 peer가 같은 tick에 적용한다. `Hello`나 타임아웃 처리 자리에서 `CreateEntity`/`DestroyEntity`를 직접 부르면 그 순간 `EntityManager`가 갈라진다. 반면 clientId 발급, endpoint 등록, 신뢰성 채널 같은 **서버 장부**는 월드 상태가 아니라 거기서 다뤄도 된다. `Network.SecondClientJoinDoesNotResyncTheFirst`가 지킨다.
- 📌 **적용 순서는 `events → inputs`, 이벤트 정렬은 `ConfirmSessionEvents` 한 곳에서.** 같은 tick에 접속과 입력이 같이 오면 엔티티가 먼저 있어야 한다. 정렬 순서가 곧 `EntityId`를 정하므로 적용하는 쪽에서 다시 정렬하지 않는다.
- 📌 **클라 전용 표현 컴포넌트는 `RegisterPresentationComponent<T>()`로 해시에서 뺀다.** 서버엔 그 풀이 없어서, 빼지 않으면 매 tick 불일치 → 무한 resync가 된다. 기본값은 "해시에 포함"이다(깜빡 잊으면 시끄럽게 실패하는 쪽).
- 📌 **`View::ForEach`는 `EntityId`가 아니라 `EntityIndex`를 준다.** `EntityId`로 받아도 암묵 변환으로 컴파일되지만 version이 0인 가짜 id가 된다. id가 필요하면 range-for를 쓴다.
- 📌 **클라 생존 판정은 전송 계층(`PumpConnection`)에서, 등록 시점에 반드시 현재 tick을 찍는다.** 조용한 클라는 ack만 보내는데 ack만 든 데이터그램은 `TryReceive`로 올라오지 않는다. 그리고 등록 시 0으로 두면 오래 떠 있던 서버가 신규 접속을 즉시 쫓아낸다(`Network.ClientCanJoinLongRunningServer`). 타임아웃은 250 tick(5초).
- 📌 **부분 스냅샷은 `clientSimTick == X`(= 스냅샷의 tick)에서만 끼운다.** 풀은 서버 tick X 시점의 값이라, 클라가 뒤처진 상태에서 끼우면 서로 다른 시점의 풀이 한 월드에 섞인다. 그래서 보류했다가 확정 프레임으로 X까지 따라온 순간, 다음 프레임 적용 **전에** 끼운다. 그리고 끼운 직후 서버가 실어 보낸 X 시점 `worldHash`와 비교한다 — 어긋남은 번지므로 서버가 고른 풀만으로 부족할 수 있다. `Network.InsufficientPartialSnapshotFallsBackToFull`이 지킨다.
- 📌 **`kReliabilityVersion`은 3이다.** 2: `InputFrame`에 세션 이벤트가 붙어 앱 레코드 배치가 바뀌었다. 3: `ResyncRequest`에 해시 분해표, `PartialSnapshot` 패킷 추가(부분 resync). 층 구분상 앱 계층 버전(Hello/Welcome)이 맞는 자리지만 그게 없어서 여기서 막는다.

아직 **미완성 / 스텁**인 것:

- 클라이언트 예측·롤백 없음 — 클라는 입력 지연 + RTT만큼 뒤처진다.
- **보간(R7) 없음** — 50Hz 시뮬을 그대로 그리므로 빠르게 움직이면 끊겨 보인다. 텍스처·깊이 버퍼·컬링도 없다(컬러 쿼드만).
- **UI는 U3까지만.** 그릴 수단(U1 드로우 목록, U2 폰트)이 없어서 화면에 UI가 안 나온다. 디버그 표시는 창 제목으로 대신한다.
- **마우스 조준은 변환 계층만 있다.** `InputMapping`이 픽셀 → `Fixed64` 경계를 처리하지만 시뮬레이션(`ApplyConfirmedInputs`)이 아직 `aimYaw`/`aimPitch`를 소비하지 않는다. 소비를 시작할 때 서버 쪽 pitch 재클램프가 필요하다.
- **입력이 시스템 전역 키 상태(`GetAsyncKeyState`)를 읽는다.** 창이 여럿이면 `IWindow::HasFocus()`로 걸러야 한다. 근본 해결은 창 메시지 기반 입력(U6와 함께).
- **해제는 파괴다.** 나갔다 들어오면 새 `clientId` + 새 캐릭터가 된다. 설계 의도는 `Dormant`(얼리기)이고 세션/영속 플레이어/캐릭터 분리(§8)와 함께 한다.
- **`ResyncRequest`에 백오프가 없다.** in-flight 하나로 제한해서 폭주는 막혔지만, 복구가 반복 실패하는 상황에서 간격을 늘리지는 않는다.
- **앱 계층 버전 검사 없음.** `Packet.hpp`의 `kProtocolVersion`은 여전히 아무도 읽지 않는다.
- `SystemRegistry::SortSystems()`는 TODO. 시스템은 등록 순서대로 실행된다 — 클라이언트는 `LossyLinkPumpSystem → NetworkSyncSystem`, `PlayerVisualSystem → RenderSystem` 순서에 실제로 의존한다.

## 실행해서 눈으로 확인하기

이제 엔진이 화면에 뜬다. 렌더·입력·동기화를 건드렸으면 테스트만 보지 말고 실제로 띄워 볼 것.

```powershell
# 싱글 (같은 프로세스에 서버 월드를 두고 인메모리 연결)
./out/build/default/bin/GameClient.exe --single

# 멀티 (dedicated server + 클라 둘). 창마다 자기 플레이어를 따라가는 카메라가 붙는다.
./out/build/default/bin/GameServer.exe
./out/build/default/bin/GameClient.exe --join 127.0.0.1
./out/build/default/bin/GameClient.exe --join 127.0.0.1
```

**창 제목이 디버그 오버레이 역할을 한다** — `tick=N hash=XXXX`. 두 창에서 같은 시각에
같은 tick의 해시가 같으면 두 시뮬레이션이 일치하는 것이다. 화면 안에 글자를 그리는
진짜 오버레이는 U5이고, 그전까지는 이걸 쓴다.

### 사람 없이 돌리는 검증 인자

복구 경로(재전송, `TickHash` 불일치 → resync)는 정상 세션에서 안 밟혀서 조용히 썩는다.
그래서 **주입해서 재현한다.**

> 📘 **[docs/reference/testing-guide.md](docs/reference/testing-guide.md) 가 이 주제의 정본이다.**
> 로컬 실행, 다중 기기, 지연·손실 주입, 디싱크 주입, 사람 없이 돌리는 법, 그리고
> **하면 안 되는 것**(정지한 월드로 디싱크를 검증하는 실수 등)이 거기 있다.

| 인자 | 뜻 |
|---|---|
| `--frames N` | N프레임만 돌고 종료 |
| `--capture x.ppm` | 마지막 프레임 백버퍼를 PPM으로 저장(헤더 세 줄이라 파서가 필요 없다) |
| `--input-script "20:W,80:"` | 가짜 키보드. tick 20에 전진, tick 80에 정지 |
| `--net-loss 30` / `--net-delay 1-4` / `--net-dup 5` / `--net-seed N` | 링크 열화 주입 |

`--input-script` 가 있는 이유: 실제 키보드 검증은 **창 포커스에 묶여 있다.** 창이 둘이면
`Input` 이 `GetAsyncKeyState`(시스템 전역)를 읽으므로 양쪽이 같이 반응하고,
자동화에서 `SetForegroundWindow` 는 신뢰할 수 없다. 스크립트는 tick 기준이라
프레임률과 무관하게 매번 같은 결과를 내고, 실제 키보드와 **같은** `QueueLocalInput` 경로를 탄다.

## 사용할 수 있는 스킬

- `explain-diff-html` — 현재 diff / 브랜치 / PR을 배경·직관·코드 워크스루·퀴즈 5문항으로 설명하는 자체완결형 HTML을 만든다. 출력은 `~/Documents/explanations/YYYY-MM-DD-explanation-<slug>.html`(저장소 밖). 학습용 프로젝트이므로 큰 변경을 끝낸 뒤 이걸로 복습 자료를 만들면 좋다.
