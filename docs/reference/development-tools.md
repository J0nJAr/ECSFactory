# 개발 환경과 런처

빌드 설정은 `CMakePresets.json`에서 관리한다. VS Code, Visual Studio, 런처 모두 같은
프리셋으로 빌드하며 결과물은 `out/build/<프리셋>/bin/`에 놓인다.

| 프리셋 | 도구 | 구성 |
|---|---|---|
| `default` 또는 `x64-debug` | MSYS2 UCRT64 GCC | Debug |
| `x64-release` | MSYS2 UCRT64 GCC | Release |
| `msvc-debug` | Visual Studio 2022 MSVC x64 | Debug |
| `msvc-release` | Visual Studio 2022 MSVC x64 | Release |

## 기준 툴체인은 g++, MSVC는 보조

두 컴파일러를 다 지원하지만 **지위가 같지 않다.**

| | g++ (MSYS2 UCRT64) | MSVC (Visual Studio 2022) |
|---|---|---|
| 지위 | **기준.** 결정론 계산의 정답, CI, pre-push 훅 | **보조.** 주로 Visual Studio 디버거를 쓰기 위한 경로 |
| 검증 | GitHub Actions가 매 푸시 Debug/Release를 빌드·테스트 | **CI가 검증하지 않는다.** 쓰기 전에 `msvc-debug` 테스트를 직접 돌린다 |
| 깨졌을 때 | 막아야 하는 결함 | 고친다. 오래 방치되면 그때 지운다 |

- **왜 g++가 기준인가.** CI·훅·clangd가 이미 g++ 기준이고, `Fixed64`의 음수 곱셈 내림도
  MSVC 쪽을 GCC의 산술 시프트에 맞추는 방식으로 일치시켰다. 정답이 GCC 쪽에 있다.
  dedicated server를 나중에 Linux로 옮길 때도 g++ 경로가 그대로 이어진다.
- **왜 MSVC를 지우지 않는가.** 서로 다른 컴파일러로 빌드한 서버·클라를 한 세션에 섞지 않으므로
  (아래 참조) MSVC 경로가 깨져도 g++ 경로는 영향을 받지 않는다. 유지 비용은 작고,
  Visual Studio 디버거와 "두 컴파일러가 같은 숫자를 내는가"를 비교해 볼 수 있다는 학습 가치가 남는다.
- ⚠️ **예외 하나.** [Math/Internal.hpp](../../EngineCore/Math/Internal.hpp)의 `_MSC_VER` 분기는
  g++ 테스트로 잡히지 않고, 틀리면 MSVC 빌드에서만 숫자가 조용히 달라진다. 이 파일을 고치면
  `default`와 `msvc-debug` 양쪽에서 수학 테스트(`Math.Fixed64CompilerParityAtRoundingBoundaries` 포함)를 돌린다.

## 준비

- Windows, CMake 3.23 이상. `cmake`와 `ctest`를 PATH에 추가한다.
- MinGW: `C:/msys64/ucrt64/bin`에 GCC, mingw32-make, GDB가 필요하다.
  MSYS2 UCRT64 터미널에서 `pacman -S --needed mingw-w64-ucrt-x86_64-toolchain`으로 설치한다.
- MSVC: Visual Studio 2022의 **C++를 사용한 데스크톱 개발** 워크로드와 Windows SDK가 필요하다.
- 런처는 Windows 기본 PowerShell 5.1과 Windows Forms를 사용한다. 별도 패키지는 없다.

MinGW 경로는 기존 저장소 설정을 유지했다. 다른 위치에 설치했다면 `CMakeUserPresets.json`의
개인 프리셋으로 compiler/PATH를 재정의하고 VS Code의 GDB 경로도 맞춘다.
기본 런처는 위 표의 기본 프리셋만 지원한다.

## 한글 인코딩

소스·문서·설정·로그는 모두 UTF-8이다. `.editorconfig`와 VS Code 설정에 저장 인코딩을
명시했고, GCC의 입력/실행 문자셋과 MSVC의 `/utf-8`도 빌드에서 지정한다.
Windows PowerShell 5.1용 `.ps1`은 UTF-8 BOM으로 저장한다. 런처는 빌드 도구의 콘솔 출력과
로그 읽기도 UTF-8로 맞춘다. 기존 UTF-8 파일을 CP949로 다시 저장하지 않는다.

## 간이 런처

저장소 루트의 **`Launch-ECSFactory.cmd`를 더블클릭**한다.

- **빌드**: 선택한 프리셋을 구성하고 전체 빌드한다.
- **전체 테스트**: 구성 → 빌드 → CTest. 앞 단계가 실패하면 다음 단계는 실행하지 않는다.
- **싱글플레이 / 서버 + 클라 2개**: 빌드 성공 후 시작한다. 멀티플레이는 UDP 6780 점유를
  확인하고 서버의 준비 로그를 기다린 다음 두 클라이언트를 실행한다.
- **실행 모두 종료**: 이 런처에서 시작한 게임만 종료한다. 런처를 닫을 때도 정리한다.
  게임 실행 중에는 빌드를 시작할 수 없고 빌드/테스트 중에는 런처 닫기를 보류한다.
- **VS Code**: 저장소 폴더를 연다.
- **Visual Studio**: MSVC 프리셋을 구성한 뒤 생성된 `ECSFactory.sln`을 연다.
  MinGW가 선택되어 있으면 `msvc-debug`로 전환한다.
- **로그 폴더**: `out/launcher/<시작시각>/`. 빌드 로그와 세션별 `server`, `client-1`,
  `client-2` 폴더를 보관한다. 클라이언트마다 작업 폴더가 달라 로그가 덮어써지지 않는다.

게임 창은 직접 닫아도 된다. 서버는 숨겨진 콘솔로 실행되며 로그 폴더에서 출력을 확인한다.
강제 종료(작업 관리자 등)로 런처를 끄면 정상 닫기 정리 절차를 거치지 못할 수 있다.

## VS Code

저장소 폴더를 열고 권장 확장 **C/C++**, **CMake Tools**를 설치한다.

- `Ctrl+Shift+B`: MinGW Debug 전체 빌드.
- **Run Task**의 `ECSFactory: Test default` / `ECSFactory: Test msvc-debug`: 빌드 후 테스트.
- CMake Tools의 **Select Configure Preset**으로 Debug/Release와 컴파일러를 선택할 수 있다.
- F5 구성은 `default:`(GDB), `msvc-debug:`(Windows 디버거) 각각 Single, Join, Server, Tests를 제공한다.
  F5는 이름에 적힌 Debug 프리셋을 빌드하므로 CMake Tools의 선택과 독립적이다.
- `Join`은 `127.0.0.1:6780` 서버가 이미 실행 중이어야 한다. 런처로 서버와 클라를 켜고
  C/C++의 프로세스 연결 기능으로 디버깅할 수도 있다.
- **Run Task → ECSFactory: Launcher**로 런처를 열 수 있다.

## Visual Studio 2022

가장 간단한 진입점은 런처의 **Visual Studio** 버튼이다. 생성된 솔루션의 시작 프로젝트는
`GameClient`, F5 인자는 `--single`로 설정되어 있다. `GameServer` 또는 `EngineCoreTests`를
시작 프로젝트로 바꾸면 서버/테스트도 디버깅할 수 있다.

터미널에서도 같은 솔루션을 생성할 수 있다.

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
ctest --preset msvc-debug
# Visual Studio에서 out/build/msvc-debug/ECSFactory.sln 열기
```

Visual Studio의 **폴더 열기**로 저장소를 여는 경우에는 `msvc-debug` 또는 `msvc-release`
프리셋을 선택하고 `GameClient.exe`를 시작 항목으로 고른다. 실행 인자는 Visual Studio의
**디버그 및 시작 설정**에서 `--single` 또는 `--join 127.0.0.1`로 지정한다.
개인 `.vs/` 상태와 생성된 프로젝트 파일은 커밋하지 않는다.

터미널용 공통 진입점도 있다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/Invoke-Dev.ps1 -Action Test -Preset msvc-debug
```

MSVC 지원을 위해 고정소수점 제곱근/사인 계산의 128비트 연산을 공통 정수 헬퍼로 구현했고,
음수 곱셈의 내림도 GCC와 맞췄다. raw 값 회귀 테스트는 두 컴파일러에서 실행한다.
네트워크 스냅샷은 trivial component의 메모리 배치를 사용하므로 서로 다른 컴파일러로 만든
서버와 클라이언트의 혼용까지 보장하는 것은 아니다. 한 세션은 같은 프리셋으로 실행한다.

도구 참고: [VS Code의 MinGW 설정](https://code.visualstudio.com/docs/cpp/config-mingw),
[Visual Studio의 CMake 프리셋](https://learn.microsoft.com/en-us/cpp/build/cmake-presets-vs?view=msvc-170).
