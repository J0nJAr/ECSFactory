# Repository Guidelines

## Project Structure & Module Organization

ECSFactory is a dependency-light C++20 game-engine core for deterministic, server-authoritative multiplayer. Shared engine code lives in `EngineCore/`, grouped by domain: `Base/`, `ECS/`, `Math/`, `Network/`, and `Components/`. `GameClient/` and `GameServer/` are thin executables over the shared `EngineCore` static library. Tests live in `tests/` and are built as `EngineCoreTests`. Design notes and longer explanations live in `docs/`; current planning notes are also in `README.md`. Generated build outputs belong under `out/` and should not be committed.

## Build, Test, and Development Commands

Use the provided CMake presets on Windows with MSYS2 MinGW g++:

```powershell
cmake --preset default
cmake --build --preset default
ctest --preset default
```

`cmake --preset default` configures the debug build in `out/build/default`. `cmake --build --preset default` builds all targets and places executables in `out/build/default/bin/`. `ctest --preset default` runs all registered tests with failure output enabled. For release builds, use `x64-release`.

To run one test:

```powershell
ctest --preset default -R Network.WorldSnapshotRoundTripCopiesTrivialComponents
.\out\build\default\bin\EngineCoreTests.exe Math.SqrtMatchesPerfectSquares
```

## Coding Style & Naming Conventions

프로젝트 전체의 한글 소스·문서·설정·로그는 **UTF-8**을 사용한다. 파일 입출력 시 인코딩을 명시하고, CP949/시스템 기본 인코딩에 의존하지 않는다. Windows PowerShell 5.1용 `.ps1`은 UTF-8 BOM으로 저장한다(UTF-8 판별용). 컴파일러의 소스 및 실행 문자열 인코딩도 UTF-8을 유지한다.

Use C++20, namespace `EF` for engine code and `EF::Network` for networking. Follow the existing style: four-space indentation, PascalCase type names, PascalCase public methods, and `k`-prefixed constants such as `kNullEntity`. Prefer deterministic simulation types (`Fixed64`, `FromInt`, `FromRaw`) in gameplay logic; keep floating-point conversions for initialization, rendering, or debugging. CMake source lists are explicit, so register new files in the relevant `CMakeLists.txt`.

Use the helper when adding artifacts:

```powershell
pwsh -File tools/Create-CppArtifact.ps1 -Target EngineCore -Kind pair -RelativePath ECS/MyThing
```

## Testing Guidelines

Tests use the custom header-only harness in `tests/TestHarness.hpp`, not GoogleTest or Catch. Add tests with `TEST_CASE(Suite, Name)`. If the case should be runnable through CTest by name, add a matching `add_test(...)` entry in `tests/CMakeLists.txt`. Keep deterministic math and networking tests focused on stable raw values, snapshots, hashes, and packet behavior.

## Commit & Pull Request Guidelines

Git history uses lightweight conventional prefixes with a spaced colon, for example `feat : ...`, `fix : ...`, `docs : ...`, and `chore : ...`. Keep commits scoped and describe the behavior changed. Pull requests should include a concise summary, linked issue or task when available, test commands run, and screenshots or logs only when UI, rendering, or runtime behavior needs visual proof.

## Agent-Specific Notes

Do not rewrite generated build directories or IDE metadata. Preserve Korean documentation where present, and keep new guidance concise unless the surrounding document is already detailed.
