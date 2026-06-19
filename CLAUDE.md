# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

ECSFactory is a dependency-light C++20 game-engine core aimed at **server-authoritative multiplayer with deterministic simulation**. The same engine code runs three ways — dedicated server, remote client, and host/singleplayer — so most logic lives in the shared `EngineCore` static library; `GameClient` and `GameServer` are thin executables on top of it.

Code comments and design docs (including `README.md`) are written in Korean. Everything lives in namespace `EF` (network code in `EF::Network`).

## Build, test, run

The toolchain is **MinGW g++ from MSYS2** (`C:/msys64/ucrt64/bin/g++.exe`), driven via CMake presets (`MinGW Makefiles` generator). Despite the MSVC branch in the root `CMakeLists.txt`, the default presets use g++, so `-Wall -Wextra` is the active warning set.

```powershell
cmake --preset default          # configure (inherits x64-debug); also: x64-debug, x64-release
cmake --build --preset default  # build all targets into out/build/default/bin
ctest --preset default          # run all tests (outputs on failure)
```

Executables land in `out/build/<preset>/bin/` (e.g. `EngineCoreTests.exe`, `GameClient.exe`, `GameServer.exe`).

### Running a single test

Tests use a custom header-only harness ([tests/TestHarness.hpp](tests/TestHarness.hpp)), **not** GoogleTest/Catch. Cases register via `TEST_CASE(Suite, Name)`. The test binary takes one optional `Suite.Name` filter argument; `ctest` also registers each case individually.

```powershell
ctest --preset default -R Network.WorldSnapshotRoundTripCopiesTrivialComponents   # via ctest
./out/build/default/bin/EngineCoreTests.exe Math.SqrtMatchesPerfectSquares        # direct, with filter
```

When you add a `TEST_CASE`, also add a matching `add_test(...)` line in [tests/CMakeLists.txt](tests/CMakeLists.txt) if you want it runnable as an isolated ctest entry.

## Adding source/header files (important)

CMake source lists are **explicit, not globbed**. A new `.cpp`/`.hpp` won't compile until it's listed in the right `CMakeLists.txt` (`target_sources` `PRIVATE` for `.cpp`, the `FILE_SET HEADERS` block for `.hpp` in `EngineCore`). Use the helper instead of editing by hand:

```powershell
pwsh -File tools/Create-CppArtifact.ps1 -Target EngineCore -Kind pair -RelativePath ECS/MyThing
```

It creates the file(s) and registers them in the target's `CMakeLists.txt`. The same is available as the VS Code task **"CMake: Create C++ Artifact"**. `-Kind` is `pair`/`source`/`header`; `-Target` is `EngineCore`/`GameClient`/`GameServer`/`tests`.

### clangd / IDE note

`.clangd` points `CompilationDatabase` at `out/build/x64-debug`. For working code intelligence, configure that preset (`cmake --preset x64-debug`) at least once so `compile_commands.json` exists there — the `default` preset builds to a different directory.

## Architecture

### Application lifecycle

Apps derive from `IGameApp` (alias `GameAppBase`, [Base/IGameApp.hpp](EngineCore/Base/IGameApp.hpp)) which owns one `GameEngine`. `IGameApp::Run()` fixes the order: `OnConfigure` → `OnInitialize` (create worlds/systems/resources here) → `engine.Initialize()` → `OnRun` → `OnShutdown` → `engine.Shutdown()`. See [GameClient/main.cpp](GameClient/main.cpp) and [GameServer/main.cpp](GameServer/main.cpp).

### GameEngine and Worlds

`GameEngine` ([Base/GameEngine.cpp](EngineCore/Base/GameEngine.cpp)) owns multiple `World`s, each tagged with a `WorldRole` (`ServerSimulation` / `ClientPresentation`), and a fixed-timestep accumulator loop (default 20ms / 50Hz). The loop drives **deterministic `FixedUpdate(Fixed64 dt, tick)`** (accumulator-based, may run 0..n times per frame) plus a variable-rate `FrameUpdate(float dt)`.

**Key design point:** host and singleplayer do *not* spawn a separate server process. The client engine adds a `ServerSimulation` world *into the same `GameEngine`* and talks to it over an in-memory connection — so single / host / dedicated all share one network code path.

### ECS layer ([EngineCore/ECS/](EngineCore/ECS/))

- **`EntityId`** ([Core.hpp](EngineCore/ECS/Core.hpp)): 64-bit by default (`EF_ENTITY_ID_64`, 32 index : 32 version). Reusing a freed index bumps the version. Use `GetEntityIndex` / `GetEntityVersion` / `CreateEntityId` helpers; `kNullEntity` is the sentinel.
- **`Registry`**: owns the `EntityManager` and a `vector<unique_ptr<IPool>>` indexed by type. Entity create/destroy, component add/remove, and `CreateView<...>()`.
- **`ComponentPool<T>`** wraps a `SparseSet<EntityIndex, T>` and exposes raw dense byte spans via the `IPool` interface — this is what snapshotting and hashing iterate over.
- **`View<With<...>, Without<...>>`** iterates entities matching a component query; it starts from the **smallest** required pool for efficiency. Both `view.ForEach([](EntityId, A&, B&){...})` (entity optional) and range-`for` work.
- **`World`** ([World.hpp](EngineCore/ECS/World.hpp)) composes `Registry` + `SystemRegistry` + a type-keyed **resource map**. Resources (`AddResource<T>` / `GetResource<T>`) hold per-world singletons — `NetworkManager` is attached this way.

### TypeInfo and the index-vs-hash distinction ([TypeInfo.hpp](EngineCore/ECS/TypeInfo.hpp))

`GetTypeInfo<T>()` returns a per-type `TypeInfo { hash, index, name, size, alignment, isTrivial }`.
- `hash` is a **compile-time FNV-1a of the type name** (override the name via a `static constexpr T::TypeName`) — stable across processes.
- `index` is a **runtime counter assigned on first use** — process-local, ordering-dependent, **not stable between client and server**.

Consequence: anything crossing the network (snapshots, type tables) compares by `hash`/`size`/`alignment`, never by `index`. Local pool storage is keyed by `index`.

### Networking & sync ([EngineCore/Network/](EngineCore/Network/))

All transport goes through `INetworkConnection` (`Send`/`Receive`). Two implementations:
- `MemoryConnection` — same-process (host/singleplayer); `CreatePair()` returns the two ends.
- `UDPConnection` — remote client ↔ dedicated server (port `6780`).

`NetworkManager` ([Network/NetworkManager.hpp](EngineCore/Network/NetworkManager.hpp)) is a world resource that owns a connection and handles the packet protocol ([Packet.hpp](EngineCore/Network/Packet.hpp), magic `EFSY`): `Hello`, `TypeTable`, `WorldSnapshotRequest`, `WorldSnapshot`, `InputCommand`, `TickHash`, `ResyncRequest`. On the server it tracks per-client `NetworkClientState` (a per-client pending-input deque). Serialization helpers (`PacketWriter`/`PacketReader`, network byte order) live in `NetUtils.hpp`.

**Snapshot model** (`World::CreateSnapshot` / `ApplySnapshot`):
- Only **trivially-copyable** components are snapshotted, copied as raw bytes. Non-trivial components are excluded.
- A client must opt each type in via `World::RegisterSnapshotComponent<T>()` (static-asserts trivially-copyable). `ApplySnapshot` rebuilds the registry from scratch and **rejects** any type whose `hash`/`size`/`alignment`/`isTrivial`/byte-count doesn't match.
- `World::ComputeHash()` (FNV-1a over trivial pools) gives a determinism checksum; mismatch is meant to trigger a `ResyncRequest` → fresh snapshot.

### Deterministic math ([EngineCore/Math/](EngineCore/Math/))

`Fixed64` ([Math/Fixed64.hpp](EngineCore/Math/Fixed64.hpp)) is the simulation number type — **Q32.32 by default** (`EF_FIXED64_FRACTIONAL_BITS = 32` in [Base/Defines.hpp](EngineCore/Base/Defines.hpp)). The format is configurable: the multiply/divide paths in [Math/Internal.hpp](EngineCore/Math/Internal.hpp) and the angle constants + sine table in [Math/Math.cpp](EngineCore/Math/Math.cpp) all derive from `Fixed64::Scale`/`FractionalBits`, so changing the macro stays correct. Note `Sin`/`Cos` precision is bounded by a **Q16 reference table** (~1.5e-5 absolute), independent of the storage format — keep that in mind for rotation-heavy assertions (the math tests scale their tolerances by `Scale / 65536`). `Vector`/`Quaternion` build on `Fixed64`. For determinism, construct from `FromInt`/`FromRaw` and keep `FromFloat`/`FromDouble`/`ToFloat` to init/render/debug paths only; raw access (`Raw()`/`FromRaw()`) is what gets serialized.

## State of the codebase

The engine core (ECS, fixed-point math, snapshot/sync) works and is test-covered. Notably **incomplete / stubbed**: wiring real keyboard/mouse input (`Input` in [Base/InputManager.hpp](EngineCore/Base/InputManager.hpp)) into `InputCommand`, UDP handshake, server tick input consumption, prediction/rollback, periodic `TickHash` exchange, and the entire rendering layer. `SystemRegistry::SortSystems()` is a TODO (systems currently run in registration order). The `README.md` "작업 스케치" section is the live TODO list.
