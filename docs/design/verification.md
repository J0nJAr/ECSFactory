# 완료조건 (교차검증용)

> 이 파일은 **교차검증 도구에 넘기는 채점표**다. `/codex-eval <base> docs/design/verification.md`
> 형태로 쓰면 아래 `## 검증` 섹션만 추출되어 다른 벤더 모델에게 diff와 함께 전달된다.
>
> ⚠️ **설계 근거를 여기 쓰지 말 것.** 근거는 `roadmap.md`와 각 설계 문서에 있다.
> 검증자가 우리 논리에 설득되지 않게 하는 것이 이 분리의 목적이다.
> 여기에는 **참/거짓으로 판정 가능한 문장만** 쓴다.
>
> 마일스톤을 끝낼 때마다 해당 항목을 여기에 추가한다.

## 검증

### 전역 불변식 — 어느 마일스톤에서도 깨지면 안 됨

1. 시뮬레이션 경로(`FixedUpdate`, `ApplyConfirmedInputs`, 컴포넌트 상태 갱신)에 `float`/`double` 연산이 없다. `Fixed64::FromFloat` / `FromDouble` / `ToFloat` 호출은 초기화·렌더·디버그·테스트 경로에만 나타난다.
2. 월드 상태를 만들어내는 순회가 `std::unordered_map`의 반복 순서에 의존하지 않는다. 특히 `NetworkManager::Clients()`를 그대로 순회해 상태를 만들지 않는다(`clientId` 정렬 후 처리).
3. 입력은 확정된 tick에만 적용된다. "도착하는 대로" 적용하는 경로가 없다.
4. `GameServer` 실행 파일이 DX11/DXGI/d3dcompiler에 의존하지 않는다. `EngineRender`를 링크하는 대상은 `GameClient`뿐이다.
5. 렌더/UI 타입(`Float2`, `Float3`, `Color`, `Rect`, `Mat4`, 앵커 레이아웃)이 시뮬레이션 코드에서 쓰이지 않는다.
6. 사람이 읽는 새 문서·주석·커밋 메시지가 한국어다.
7. 클래스와 자명하지 않은 함수에 헤더 주석이 있고, 그 주석이 코드의 현재 동작과 일치한다. 동작을 바꾸면서 주석을 안 고친 곳이 없다.

### R0 — EngineRender 타깃 분리와 렌더 인터페이스 골격

- `EngineRender`가 `EngineCore`와 별도 CMake 타깃이고, 의존 방향이 `EngineRender → EngineCore` 한쪽뿐이다. `EngineCore`가 렌더 헤더를 include하지 않는다.
- `Dx11Renderer.hpp`를 include하는 번역 단위에 `windows.h` / `d3d11.h`가 노출되지 않는다.
- `DrawItem`의 위치가 3D 좌표(`Float3`)다.

### R1 — Win32Window

- `Win32Window`가 `IWindow`를 전부 구현한다.
- `Win32Window.hpp`를 include해도 `windows.h`가 노출되지 않는다.
- 요청한 width/height가 **클라이언트 영역** 크기가 된다(창 테두리 보정이 있다).
- `WndProc`이 전역 상태나 전역 맵이 아니라 창별 저장소로 인스턴스를 되찾는다.
- `Destroy()`가 멱등하다. 두 번 호출해도, 이미 닫힌 뒤 호출해도 안전하다.
- 메시지 펌프가 창 종료를 놓치지 않는다.
- 최소화(크기 0) 상황에서 0으로 나누거나 무의미한 재생성을 하지 않는다.

### N1 — 스냅샷에 EntityManager 상태 포함

- `WorldSnapshot`이 엔티티 할당 상태(version, free list, 다음 index)를 나른다.
- 스냅샷 왕복 후 서버와 클라이언트가 각각 `CreateEntity()`를 부르면 **index와 version이 모두 같은** `EntityId`가 나온다. 파괴된 엔티티가 있었던 경우에도 그렇다.
- 스냅샷 대상 컴포넌트를 하나도 갖지 않은 엔티티가 왕복 후에도 살아 있다.
- 왕복 후 양쪽이 같은 횟수만큼 같은 컴포넌트를 붙여 스폰하면 `ComputeHash()`가 일치한다.
- 모순된 엔티티 상태(범위 밖 free index, 중복 free index, 죽은 index에 붙은 컴포넌트)를 가진 스냅샷은 거부된다.
- 이 검증들이 구조체 복사가 아니라 `EncodeWorldSnapshot` / `DecodeWorldSnapshot` 왕복을 실제로 통과한다.

### U3 — 앵커 레이아웃 코어

- 레이아웃 계산이 순수 함수다. 위젯 트리나 즉시형 컨텍스트에 의존하지 않는다.
- 9개 점 앵커 프리셋이 부모가 원점이 아닐 때도 올바른 위치를 낸다.
- stretch 축에서 offset이 양쪽 여백으로 동작한다.
- 축이 독립적으로 판정된다(`StretchX`는 X만 늘어나고 Y는 점 앵커다).
- 화면 좌표계가 Y 아래 증가로 일관된다(`Top`이 작은 y).
- pivot이 기준점을 옮긴다.
- 중첩된 부모-자식-손자에서 위치가 누적되어 맞는다.
- float 비교에 허용오차를 쓴다.

### CI / 빌드

- `cmake --preset default` → `cmake --build --preset default` → `ctest --preset default`가 전부 성공하고, 테스트 실패가 0이다.
- 새로 추가된 `.cpp`/`.hpp`가 전부 해당 `CMakeLists.txt`에 등록되어 있다(등록 없이 존재하는 소스 파일이 없다).
- 새 `TEST_CASE`마다 `tests/CMakeLists.txt`에 대응하는 `add_test(...)` 줄이 있다.
- 새로 추가한 파일에서 `-Wall -Wextra` 경고가 발생하지 않는다.
