#include "TestHarness.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "Components/PlayerControl.hpp"
#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "ECS/World.hpp"
#include "Gameplay/PlayerMovement.hpp"
#include "Network/Endpoint.hpp"
#include "Network/LossyConnection.hpp"
#include "Network/MemoryConnection.hpp"
#include "Network/NetworkManager.hpp"
#include "Network/NetworkSyncSystem.hpp"
#include "Network/ReliabilityChannel.hpp"
#include "Network/SimulatedNetwork.hpp"

namespace {
    EF::World CreateAuthoritativeWorld() {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::Velocity>();

        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(1, 2, 3),
            .rotation = EF::Quaternion::Identity()
        });
        world.AddComponent<EF::Velocity>(entity, EF::Velocity{
            .linear = EF::Vector3(4, 5, 6),
            .angular = EF::Vector3(7, 8, 9)
        });

        return world;
    }

    bool ReceiveSnapshotInto(EF::NetworkManager& clientNetwork, EF::World& clientWorld) {
        auto snapshotPacket = clientNetwork.TryReceivePacket();
        if (!snapshotPacket.has_value() ||
            snapshotPacket->type != EF::Network::PacketType::WorldSnapshot) {
            return false;
        }

        EF::WorldSnapshot snapshot;
        return EF::NetworkManager::DecodeWorldSnapshot(snapshotPacket->payload, snapshot) &&
               clientWorld.ApplySnapshot(snapshot);
    }

    // 스냅샷 컴포넌트 등록 + 서버 authoritative 월드를 entityCount만큼 채운다.
    void PopulateAuthoritativeWorld(EF::World& world, int entityCount) {
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::Velocity>();
        for (int i = 0; i < entityCount; ++i) {
            const EF::EntityId entity = world.CreateEntity();
            world.AddComponent<EF::Transform>(entity, EF::Transform{
                .position = EF::Vector3(i + 1, i + 2, i + 3),
                .rotation = EF::Quaternion::Identity()
            });
            world.AddComponent<EF::Velocity>(entity, EF::Velocity{
                .linear = EF::Vector3(i + 4, i + 5, i + 6),
                .angular = EF::Vector3(i + 7, i + 8, i + 9)
            });
        }
    }

    // 클라이언트 월드에 스냅샷 컴포넌트 등록 + NetworkManager + Client 동기화 시스템.
    EF::NetworkSyncSystem& AttachClientWorld(EF::World& world,
                                             std::shared_ptr<EF::Network::INetworkConnection> connection) {
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::Velocity>();
        // 서버는 Hello를 받을 때 플레이어 엔티티에 PlayerControl을 붙인다.
        // 클라이언트가 이 타입을 등록하지 않으면 스냅샷에서 조용히 버려져서
        // ComputeHash()가 영원히 어긋난다.
        world.RegisterSnapshotComponent<EF::PlayerControl>();
        world.AddResource<EF::NetworkManager>(std::move(connection));
        return world.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);
    }

    EF::Fixed64 FixedTickDelta() {
        return EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    }

    // lockstep 테스트용 서버 월드.
    //
    // 플레이어 엔티티를 여기서 미리 만들지 않는 것이 핵심이다. 서버가 Hello를 받을 때
    // Transform + PlayerControl{clientId}를 붙여서 직접 스폰하므로(NetworkSyncSystem),
    // 테스트가 손으로 붙이면 같은 clientId를 가진 엔티티가 둘이 되어 실제 동작을 가린다.
    EF::World CreateLockstepServerWorld() {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::PlayerControl>();
        return world;
    }

    // 월드 안에서 특정 clientId가 조종하는 플레이어를 찾아 온다.
    //
    // ApplyConfirmedInputs가 쓰는 것과 **같은 질의**(View<Transform, PlayerControl>)를 쓴다.
    // 그래야 "찾았다"가 곧 "확정 입력이 이 엔티티에 적용된다"와 같은 뜻이 된다.
    // matchCount를 함께 세는 이유: 같은 clientId를 가진 엔티티가 둘 이상이면 입력이
    // 여러 엔티티에 동시에 먹는다는 뜻이라 그 자체로 결함이다.
    struct PlayerProbe {
        bool found = false;
        EF::EntityIndex entityIndex = 0;
        EF::Vector3 position;
        std::size_t matchCount = 0;
    };

    PlayerProbe ProbePlayer(EF::World& world, uint32_t clientId) {
        PlayerProbe probe;
        auto view = world.CreateView<EF::Transform, EF::PlayerControl>();
        view.ForEach([&](EF::EntityIndex entityIndex, EF::Transform& transform, EF::PlayerControl& control) {
            if (control.clientId != clientId) {
                return;
            }
            probe.found = true;
            probe.entityIndex = entityIndex;
            probe.position = transform.position;
            ++probe.matchCount;
        });
        return probe;
    }

    std::vector<EF::InputCommand> DrainInputCommands(EF::NetworkManager& network) {
        std::vector<EF::InputCommand> commands;
        while (auto packet = network.TryReceivePacket()) {
            if (packet->type != EF::Network::PacketType::InputCommand) {
                continue;
            }

            EF::InputCommand command;
            if (EF::NetworkManager::DecodeInputCommand(packet->payload, command)) {
                commands.push_back(command);
            }
        }
        return commands;
    }
}

TEST_CASE(Network, MemoryConnectionTransfersBytesBetweenPeers) {
    auto [client, server] = EF::Network::MemoryConnection::CreatePair();

    const std::vector<uint8_t> sent{ 1, 2, 3, 4 };
    EXPECT_TRUE(client->Send(sent));

    auto received = server->Receive();
    EXPECT_TRUE(received.has_value());
    EXPECT_EQ(received->size(), static_cast<std::size_t>(4));
    EXPECT_EQ((*received)[0], static_cast<uint8_t>(1));
    EXPECT_EQ((*received)[1], static_cast<uint8_t>(2));
    EXPECT_EQ((*received)[2], static_cast<uint8_t>(3));
    EXPECT_EQ((*received)[3], static_cast<uint8_t>(4));
}

TEST_CASE(Network, HostModeSelfConnectionSynchronizesClientFromLocalServerWorld) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();
    EF::NetworkManager clientNetwork(clientConnection);
    EF::NetworkManager localServerNetwork(serverConnection);

    EF::World localServerWorld = CreateAuthoritativeWorld();
    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();

    EXPECT_TRUE(clientNetwork.SendHello());
    auto helloPacket = localServerNetwork.TryReceivePacket();
    EXPECT_TRUE(helloPacket.has_value());
    EXPECT_EQ(static_cast<int>(helloPacket->type), static_cast<int>(EF::Network::PacketType::Hello));

    EXPECT_TRUE(localServerNetwork.SendTypeTable(localServerWorld.BuildTypeTable()));
    auto typeTablePacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(typeTablePacket.has_value());
    EXPECT_EQ(static_cast<int>(typeTablePacket->type), static_cast<int>(EF::Network::PacketType::TypeTable));

    EXPECT_TRUE(clientNetwork.SendWorldSnapshotRequest(0));
    auto requestPacket = localServerNetwork.TryReceivePacket();
    EXPECT_TRUE(requestPacket.has_value());
    EXPECT_EQ(static_cast<int>(requestPacket->type), static_cast<int>(EF::Network::PacketType::WorldSnapshotRequest));

    EXPECT_TRUE(localServerNetwork.SendWorldSnapshot(localServerWorld.CreateSnapshot(100)));
    EXPECT_TRUE(ReceiveSnapshotInto(clientNetwork, clientWorld));
    EXPECT_EQ(clientWorld.ComputeHash(), localServerWorld.ComputeHash());
}

TEST_CASE(Network, SyncSystemCompletesInitialSnapshotHandshake) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateAuthoritativeWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    EF::NetworkSyncSystem& serverSync = serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    clientWorld.FixedUpdate(dt, 1); // client sends Hello
    serverWorld.FixedUpdate(dt, 1); // server replies TypeTable
    clientWorld.FixedUpdate(dt, 2); // client requests snapshot
    serverWorld.FixedUpdate(dt, 2); // server sends snapshot
    clientWorld.FixedUpdate(dt, 3); // client applies snapshot

    EXPECT_TRUE(clientSync.HasTypeTable());
    EXPECT_TRUE(clientSync.HasWelcome());
    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_EQ(clientWorld.GetResource<EF::NetworkManager>().LocalClientId(), static_cast<uint32_t>(1));
    // ⚠️ 서버 tick 2에 만든 스냅샷의 라벨은 **1**이다. 2가 아니다.
    //
    // 서버의 한 tick은 `수신 처리 → 입력 확정·적용 → TickHash 방송` 순이고,
    // 스냅샷은 첫 단계(수신 처리)에서 만들어진다. 그 시점의 월드는 tick 2의 입력을
    // 아직 적용하지 않았으므로 **tick 1까지 시뮬레이션한 상태**다.
    //
    // 라벨이 곧 "클라가 어디까지 시뮬레이션했다고 볼 것인가"(clientSimTick)이므로,
    // 2로 붙이면 클라가 InputFrame{2}를 "이미 지난 것"으로 버린다. 그게 resync마다
    // 확정 입력 한 프레임을 삼키던 버그였다. 이 단언이 그 계약을 못 박는다.
    EXPECT_EQ(clientSync.LastSnapshotTick(), static_cast<uint64_t>(1));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    const EF::NetworkClientState* serverClient = serverWorld.GetResource<EF::NetworkManager>().FindClient(1);
    EXPECT_TRUE(serverClient != nullptr);
    EXPECT_TRUE(serverClient->hasControlledEntity);
    EXPECT_TRUE(serverClient->controlledEntity != EF::kNullEntity);
    EXPECT_TRUE(serverClient->inputByTick.empty());

    static_cast<void>(serverSync);
}

TEST_CASE(Network, SyncSystemSendsQueuedInputToServer) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateAuthoritativeWorld();
    EF::NetworkManager& serverNetwork = serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    clientWorld.FixedUpdate(dt, 1);
    serverWorld.FixedUpdate(dt, 1);
    clientWorld.FixedUpdate(dt, 2);
    serverWorld.FixedUpdate(dt, 2);
    clientWorld.FixedUpdate(dt, 3);

    clientSync.QueueLocalInput(EF::InputCommand{
        .buttonMask = 0b101,
        .aimYaw = EF::Fixed64::FromInt(10),
        .aimPitch = EF::Fixed64::FromInt(20)
    });

    clientWorld.FixedUpdate(dt, 4);
    serverWorld.FixedUpdate(dt, 4);

    EXPECT_EQ(clientWorld.GetResource<EF::NetworkManager>().LocalClientId(), static_cast<uint32_t>(1));

    const EF::NetworkClientState* serverClient = serverNetwork.FindClient(1);
    EXPECT_TRUE(serverClient != nullptr);
    EXPECT_TRUE(serverClient->hasConfirmed);
    EXPECT_EQ(serverClient->lastConfirmed.clientId, static_cast<uint32_t>(1));
    EXPECT_EQ(serverClient->lastConfirmed.tick, static_cast<uint64_t>(4));
    EXPECT_EQ(serverClient->lastConfirmed.sequence, static_cast<uint32_t>(0));
    EXPECT_EQ(serverClient->lastConfirmed.buttonMask, static_cast<uint64_t>(0));
    EXPECT_TRUE(serverClient->inputByTick.find(5) != serverClient->inputByTick.end());
    EXPECT_EQ(serverClient->inputByTick.at(5).sequence, static_cast<uint32_t>(1));
    EXPECT_EQ(serverClient->inputByTick.at(5).buttonMask, static_cast<uint64_t>(0b101));

    EF::NetworkManager& clientNetwork = clientWorld.GetResource<EF::NetworkManager>();
    auto framePacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(framePacket.has_value());
    EXPECT_EQ(static_cast<int>(framePacket->type), static_cast<int>(EF::Network::PacketType::InputFrame));
    EF::InputFrame frame;
    EXPECT_TRUE(EF::NetworkManager::DecodeInputFrame(framePacket->payload, frame));
    EXPECT_EQ(frame.tick, static_cast<uint64_t>(4));
    EXPECT_EQ(frame.inputs.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(frame.inputs[0].clientId, static_cast<uint32_t>(1));
    EXPECT_EQ(frame.inputs[0].buttonMask, static_cast<uint64_t>(0));
}

TEST_CASE(Network, ConfirmInputsReturnsSortedInputsAndHoldsMissing) {
    EF::NetworkManager serverNetwork(EF::Network::MemoryConnection::CreatePair().second);

    serverNetwork.RegisterClient(5);
    serverNetwork.RegisterClient(2);

    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 10, .sequence = 1, .buttonMask = 5 }));
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 2, .tick = 10, .sequence = 1, .buttonMask = 2 }));

    std::vector<EF::InputCommand> confirmed = serverNetwork.ConfirmInputs(10);
    EXPECT_EQ(confirmed.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(confirmed[0].clientId, static_cast<uint32_t>(2));
    EXPECT_EQ(confirmed[0].buttonMask, static_cast<uint64_t>(2));
    EXPECT_EQ(confirmed[1].clientId, static_cast<uint32_t>(5));
    EXPECT_EQ(confirmed[1].buttonMask, static_cast<uint64_t>(5));

    std::vector<EF::InputCommand> held = serverNetwork.ConfirmInputs(11);
    EXPECT_EQ(held.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(held[0].tick, static_cast<uint64_t>(11));
    EXPECT_EQ(held[0].buttonMask, static_cast<uint64_t>(2));
    EXPECT_EQ(held[1].tick, static_cast<uint64_t>(11));
    EXPECT_EQ(held[1].buttonMask, static_cast<uint64_t>(5));

    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 10, .sequence = 2 }));
}

TEST_CASE(Network, ClientAppliesRelayedInputFramesInOrder) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = FixedTickDelta();
    clientWorld.FixedUpdate(dt, 1);
    serverWorld.FixedUpdate(dt, 1);
    clientWorld.FixedUpdate(dt, 2);
    serverWorld.FixedUpdate(dt, 2);
    clientWorld.FixedUpdate(dt, 3);

    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_EQ(clientSync.ClientSimTick(), static_cast<uint64_t>(2));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    serverWorld.FixedUpdate(dt, 3);
    clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
    clientWorld.FixedUpdate(dt, 4);
    EXPECT_EQ(clientSync.ClientSimTick(), static_cast<uint64_t>(3));

    serverWorld.FixedUpdate(dt, 4);
    clientWorld.FixedUpdate(dt, 5);
    EXPECT_EQ(clientSync.ClientSimTick(), static_cast<uint64_t>(4));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    serverWorld.FixedUpdate(dt, 5);
    EXPECT_FALSE(clientWorld.ComputeHash() == serverWorld.ComputeHash());

    clientWorld.FixedUpdate(dt, 6);
    EXPECT_EQ(clientSync.ClientSimTick(), static_cast<uint64_t>(5));
    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, ClientQueuesInputForEstimatedServerTickPlusDelay) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateLockstepServerWorld();
    EF::NetworkManager& serverNetwork = serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = FixedTickDelta();
    clientWorld.FixedUpdate(dt, 1);
    serverWorld.FixedUpdate(dt, 1);
    clientWorld.FixedUpdate(dt, 2);
    serverWorld.FixedUpdate(dt, 2);
    clientWorld.FixedUpdate(dt, 3);
    serverWorld.FixedUpdate(dt, 3);

    clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
    clientWorld.FixedUpdate(dt, 4);
    EXPECT_EQ(clientSync.ServerTickEstimate(), static_cast<uint64_t>(3));

    serverWorld.FixedUpdate(dt, 4);
    const EF::NetworkClientState* clientState = serverNetwork.FindClient(1);
    EXPECT_TRUE(clientState != nullptr);
    EXPECT_TRUE(clientState->inputByTick.find(5) != clientState->inputByTick.end());
    EXPECT_EQ(clientState->inputByTick.at(5).buttonMask, static_cast<uint64_t>(EF::kInputMoveRight));
}

TEST_CASE(Network, ClientResendsQueuedInputForRedundancyWindow) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World clientWorld;
    EF::NetworkManager& clientNetwork = clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    clientNetwork.SetLocalClientId(9);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    EF::NetworkManager serverNetwork(serverConnection);
    const EF::Fixed64 dt = FixedTickDelta();

    clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveForward });

    clientWorld.FixedUpdate(dt, 10);
    std::vector<EF::InputCommand> first = DrainInputCommands(serverNetwork);
    EXPECT_EQ(first.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(first[0].clientId, static_cast<uint32_t>(9));
    EXPECT_EQ(first[0].tick, static_cast<uint64_t>(12));
    EXPECT_EQ(first[0].sequence, static_cast<uint32_t>(1));

    clientWorld.FixedUpdate(dt, 11);
    std::vector<EF::InputCommand> second = DrainInputCommands(serverNetwork);
    EXPECT_EQ(second.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(second[0].tick, first[0].tick);
    EXPECT_EQ(second[0].sequence, first[0].sequence);

    clientWorld.FixedUpdate(dt, 12);
    std::vector<EF::InputCommand> third = DrainInputCommands(serverNetwork);
    EXPECT_EQ(third.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(third[0].tick, first[0].tick);
    EXPECT_EQ(third[0].sequence, first[0].sequence);

    clientWorld.FixedUpdate(dt, 13);
    EXPECT_TRUE(DrainInputCommands(serverNetwork).empty());
}

TEST_CASE(Network, SyncSystemRequestsAndAppliesResyncOnHashMismatch) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateAuthoritativeWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    clientWorld.FixedUpdate(dt, 1);
    serverWorld.FixedUpdate(dt, 1);
    clientWorld.FixedUpdate(dt, 2);
    serverWorld.FixedUpdate(dt, 2);
    clientWorld.FixedUpdate(dt, 3);
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    auto view = clientWorld.CreateView<EF::Transform>();
    view.ForEach([](EF::EntityIndex, EF::Transform& transform) {
        transform.position = EF::Vector3(99, 99, 99);
    });
    EXPECT_FALSE(clientWorld.ComputeHash() == serverWorld.ComputeHash());

    serverWorld.FixedUpdate(dt, 3); // server sends frame/hash for the next simulated tick
    clientWorld.FixedUpdate(dt, 4); // client applies frame 3, detects mismatch, sends ResyncRequest
    EXPECT_TRUE(clientSync.HasHashMismatch());

    serverWorld.FixedUpdate(dt, 4); // server sends fresh snapshot
    clientWorld.FixedUpdate(dt, 5); // client applies snapshot

    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, ServerClientConnectionTransfersInputAndSnapshot) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();
    EF::NetworkManager clientNetwork(clientConnection);
    EF::NetworkManager serverNetwork(serverConnection);

    EF::World serverWorld = CreateAuthoritativeWorld();
    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();

    const EF::InputCommand command{
        .clientId = 7,
        .tick = 120,
        .sequence = 3,
        .buttonMask = 0b101,
        .aimYaw = EF::Fixed64::FromInt(10),
        .aimPitch = EF::Fixed64::FromInt(20)
    };
    EXPECT_TRUE(clientNetwork.SendInputCommand(command));

    auto inputPacket = serverNetwork.TryReceivePacket();
    EXPECT_TRUE(inputPacket.has_value());
    EXPECT_EQ(static_cast<int>(inputPacket->type), static_cast<int>(EF::Network::PacketType::InputCommand));

    EF::InputCommand decodedCommand;
    EXPECT_TRUE(EF::NetworkManager::DecodeInputCommand(inputPacket->payload, decodedCommand));
    EXPECT_EQ(decodedCommand.sequence, command.sequence);
    EXPECT_TRUE(decodedCommand.aimYaw == command.aimYaw);
    EXPECT_TRUE(decodedCommand.aimPitch == command.aimPitch);
    serverNetwork.EnqueueInput(decodedCommand);
    std::vector<EF::InputCommand> confirmed = serverNetwork.ConfirmInputs(command.tick);
    EXPECT_EQ(confirmed.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(confirmed[0].clientId, command.clientId);
    EXPECT_EQ(confirmed[0].buttonMask, command.buttonMask);

    EXPECT_TRUE(serverNetwork.SendInputFrame(EF::InputFrame{ .tick = command.tick, .events = {}, .inputs = confirmed }));
    auto framePacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(framePacket.has_value());
    EXPECT_EQ(static_cast<int>(framePacket->type), static_cast<int>(EF::Network::PacketType::InputFrame));
    EF::InputFrame decodedFrame;
    EXPECT_TRUE(EF::NetworkManager::DecodeInputFrame(framePacket->payload, decodedFrame));
    EXPECT_EQ(decodedFrame.tick, command.tick);
    EXPECT_EQ(decodedFrame.inputs.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(decodedFrame.inputs[0].buttonMask, command.buttonMask);

    EXPECT_TRUE(serverNetwork.SendWorldSnapshot(serverWorld.CreateSnapshot(command.tick)));
    EXPECT_TRUE(ReceiveSnapshotInto(clientNetwork, clientWorld));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, ServerSynchronizesMultipleMemoryClientsWithDistinctAssignedIds) {
    auto [clientOneConnection, serverOneConnection] = EF::Network::MemoryConnection::CreatePair();
    auto [clientTwoConnection, serverTwoConnection] = EF::Network::MemoryConnection::CreatePair();
    EF::NetworkManager clientOneNetwork(clientOneConnection);
    EF::NetworkManager clientTwoNetwork(clientTwoConnection);
    EF::NetworkManager serverClientOneNetwork(serverOneConnection);
    EF::NetworkManager serverClientTwoNetwork(serverTwoConnection);

    EF::World serverWorld = CreateAuthoritativeWorld();
    EF::World clientOneWorld;
    EF::World clientTwoWorld;
    clientOneWorld.RegisterSnapshotComponent<EF::Transform>();
    clientOneWorld.RegisterSnapshotComponent<EF::Velocity>();
    clientTwoWorld.RegisterSnapshotComponent<EF::Transform>();
    clientTwoWorld.RegisterSnapshotComponent<EF::Velocity>();

    uint32_t nextClientId = 1;

    EXPECT_TRUE(clientOneNetwork.SendHello());
    EXPECT_TRUE(clientTwoNetwork.SendHello());

    auto clientOneHello = serverClientOneNetwork.TryReceivePacket();
    auto clientTwoHello = serverClientTwoNetwork.TryReceivePacket();
    EXPECT_TRUE(clientOneHello.has_value());
    EXPECT_TRUE(clientTwoHello.has_value());
    EXPECT_EQ(static_cast<int>(clientOneHello->type), static_cast<int>(EF::Network::PacketType::Hello));
    EXPECT_EQ(static_cast<int>(clientTwoHello->type), static_cast<int>(EF::Network::PacketType::Hello));
    EXPECT_TRUE(EF::NetworkManager::DecodeHello(clientOneHello->payload));
    EXPECT_TRUE(EF::NetworkManager::DecodeHello(clientTwoHello->payload));

    const uint32_t clientOneId = nextClientId++;
    const uint32_t clientTwoId = nextClientId++;
    const EF::EntityId clientOneEntity = serverWorld.CreateEntity();
    const EF::EntityId clientTwoEntity = serverWorld.CreateEntity();
    serverClientOneNetwork.RegisterClient(clientOneId, clientOneEntity);
    serverClientTwoNetwork.RegisterClient(clientTwoId, clientTwoEntity);
    EXPECT_TRUE(serverClientOneNetwork.SendWelcome(clientOneId));
    EXPECT_TRUE(serverClientTwoNetwork.SendWelcome(clientTwoId));
    EXPECT_TRUE(serverClientOneNetwork.SendTypeTable(serverWorld.BuildTypeTable()));
    EXPECT_TRUE(serverClientTwoNetwork.SendTypeTable(serverWorld.BuildTypeTable()));

    auto clientOneWelcome = clientOneNetwork.TryReceivePacket();
    auto clientTwoWelcome = clientTwoNetwork.TryReceivePacket();
    EXPECT_TRUE(clientOneWelcome.has_value());
    EXPECT_TRUE(clientTwoWelcome.has_value());
    EXPECT_EQ(static_cast<int>(clientOneWelcome->type), static_cast<int>(EF::Network::PacketType::Welcome));
    EXPECT_EQ(static_cast<int>(clientTwoWelcome->type), static_cast<int>(EF::Network::PacketType::Welcome));
    uint32_t assignedClientOneId = 0;
    uint32_t assignedClientTwoId = 0;
    EXPECT_TRUE(EF::NetworkManager::DecodeWelcome(clientOneWelcome->payload, assignedClientOneId));
    EXPECT_TRUE(EF::NetworkManager::DecodeWelcome(clientTwoWelcome->payload, assignedClientTwoId));
    clientOneNetwork.SetLocalClientId(assignedClientOneId);
    clientTwoNetwork.SetLocalClientId(assignedClientTwoId);
    EXPECT_EQ(clientOneNetwork.LocalClientId(), static_cast<uint32_t>(1));
    EXPECT_EQ(clientTwoNetwork.LocalClientId(), static_cast<uint32_t>(2));

    auto clientOneTypeTable = clientOneNetwork.TryReceivePacket();
    auto clientTwoTypeTable = clientTwoNetwork.TryReceivePacket();
    EXPECT_TRUE(clientOneTypeTable.has_value());
    EXPECT_TRUE(clientTwoTypeTable.has_value());
    EXPECT_EQ(static_cast<int>(clientOneTypeTable->type), static_cast<int>(EF::Network::PacketType::TypeTable));
    EXPECT_EQ(static_cast<int>(clientTwoTypeTable->type), static_cast<int>(EF::Network::PacketType::TypeTable));

    EXPECT_TRUE(clientOneNetwork.SendWorldSnapshotRequest(0));
    EXPECT_TRUE(clientTwoNetwork.SendWorldSnapshotRequest(0));
    auto clientOneSnapshotRequest = serverClientOneNetwork.TryReceivePacket();
    auto clientTwoSnapshotRequest = serverClientTwoNetwork.TryReceivePacket();
    EXPECT_TRUE(clientOneSnapshotRequest.has_value());
    EXPECT_TRUE(clientTwoSnapshotRequest.has_value());
    EXPECT_EQ(static_cast<int>(clientOneSnapshotRequest->type), static_cast<int>(EF::Network::PacketType::WorldSnapshotRequest));
    EXPECT_EQ(static_cast<int>(clientTwoSnapshotRequest->type), static_cast<int>(EF::Network::PacketType::WorldSnapshotRequest));

    const EF::WorldSnapshot snapshot = serverWorld.CreateSnapshot(10);
    EXPECT_TRUE(serverClientOneNetwork.SendWorldSnapshot(snapshot));
    EXPECT_TRUE(serverClientTwoNetwork.SendWorldSnapshot(snapshot));
    EXPECT_TRUE(ReceiveSnapshotInto(clientOneNetwork, clientOneWorld));
    EXPECT_TRUE(ReceiveSnapshotInto(clientTwoNetwork, clientTwoWorld));
    EXPECT_EQ(clientOneWorld.ComputeHash(), serverWorld.ComputeHash());
    EXPECT_EQ(clientTwoWorld.ComputeHash(), serverWorld.ComputeHash());

    EXPECT_TRUE(clientOneNetwork.SendInputCommand(EF::InputCommand{ .clientId = clientOneNetwork.LocalClientId(), .tick = 10, .sequence = 1, .buttonMask = 1 }));
    EXPECT_TRUE(clientTwoNetwork.SendInputCommand(EF::InputCommand{ .clientId = clientTwoNetwork.LocalClientId(), .tick = 10, .sequence = 1, .buttonMask = 2 }));

    auto clientOneInputPacket = serverClientOneNetwork.TryReceivePacket();
    auto clientTwoInputPacket = serverClientTwoNetwork.TryReceivePacket();
    EXPECT_TRUE(clientOneInputPacket.has_value());
    EXPECT_TRUE(clientTwoInputPacket.has_value());

    EF::InputCommand clientOneInput;
    EF::InputCommand clientTwoInput;
    EXPECT_TRUE(EF::NetworkManager::DecodeInputCommand(clientOneInputPacket->payload, clientOneInput));
    EXPECT_TRUE(EF::NetworkManager::DecodeInputCommand(clientTwoInputPacket->payload, clientTwoInput));
    serverClientOneNetwork.EnqueueInput(clientOneInput);
    serverClientTwoNetwork.EnqueueInput(clientTwoInput);

    std::vector<EF::InputCommand> confirmedOne = serverClientOneNetwork.ConfirmInputs(10);
    std::vector<EF::InputCommand> confirmedTwo = serverClientTwoNetwork.ConfirmInputs(10);
    EXPECT_EQ(confirmedOne.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(confirmedTwo.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(confirmedOne[0].buttonMask, uint64_t{ 1 });
    EXPECT_EQ(confirmedTwo[0].buttonMask, uint64_t{ 2 });

    const EF::NetworkClientState* serverClientOne = serverClientOneNetwork.FindClient(1);
    const EF::NetworkClientState* serverClientTwo = serverClientTwoNetwork.FindClient(2);
    EXPECT_TRUE(serverClientOne != nullptr);
    EXPECT_TRUE(serverClientTwo != nullptr);
    EXPECT_TRUE(serverClientOne->hasControlledEntity);
    EXPECT_TRUE(serverClientTwo->hasControlledEntity);
    EXPECT_TRUE(serverClientOne->controlledEntity != serverClientTwo->controlledEntity);
}

TEST_CASE(Network, WorldSnapshotRoundTripCopiesTrivialComponents) {
    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();
    serverWorld.RegisterSnapshotComponent<EF::Velocity>();

    const EF::EntityId entity = serverWorld.CreateEntity();
    serverWorld.AddComponent<EF::Transform>(entity, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });
    serverWorld.AddComponent<EF::Velocity>(entity, EF::Velocity{
        .linear = EF::Vector3(4, 5, 6),
        .angular = EF::Vector3(7, 8, 9)
    });

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::Velocity>();

    const EF::WorldSnapshot snapshot = serverWorld.CreateSnapshot(42);
    EXPECT_TRUE(clientWorld.ApplySnapshot(snapshot));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, WorldSnapshotRejectsMismatchedComponentShape) {
    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();

    const EF::EntityId entity = serverWorld.CreateEntity();
    serverWorld.AddComponent<EF::Transform>(entity, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });

    EF::WorldSnapshot snapshot = serverWorld.CreateSnapshot(1);
    EXPECT_TRUE(!snapshot.components.empty());
    snapshot.components[0].type.size += 1;

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    EXPECT_FALSE(clientWorld.ApplySnapshot(snapshot));
}

namespace {
    // 스냅샷 왕복 테스트용 서버 월드: 엔티티 5개를 만들고 index 1과 3을 파괴한다.
    //
    // 이 모양을 고른 이유: 파괴가 남긴 흔적 세 가지 — 0이 아닌 version, 비어 있지 않은
    // free list, 그리고 free list의 **LIFO 순서**(3 → 1) — 를 한 번에 걸 수 있다.
    EF::World CreateWorldWithHoles(std::vector<EF::EntityId>& outEntities) {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();

        for (int i = 0; i < 5; ++i) {
            const EF::EntityId entity = world.CreateEntity();
            world.AddComponent<EF::Transform>(entity, EF::Transform{
                .position = EF::Vector3(i, i, i),
                .rotation = EF::Quaternion::Identity()
            });
            outEntities.push_back(entity);
        }

        world.DestroyEntity(outEntities[1]);
        world.DestroyEntity(outEntities[3]);
        return world;
    }

    // 서버 월드를 실제 와이어 포맷으로 인코딩했다가 클라 월드에 적용한다.
    // Encode/Decode를 거치는 이유: 새 EntitySnapshot 필드가 와이어에 실제로 실리는지까지
    // 같이 검증하기 위해서다(구조체 복사만 하면 인코더 누락을 못 잡는다).
    bool RoundTripSnapshotThroughWire(const EF::World& serverWorld, EF::World& clientWorld, uint64_t tick) {
        const std::vector<uint8_t> payload =
            EF::NetworkManager::EncodeWorldSnapshot(serverWorld.CreateSnapshot(tick));

        EF::WorldSnapshot decoded;
        return EF::NetworkManager::DecodeWorldSnapshot(payload, decoded) && clientWorld.ApplySnapshot(decoded);
    }
}

TEST_CASE(Network, SnapshotRestoresEntityAllocationStateAcrossDestroys) {
    std::vector<EF::EntityId> entities;
    EF::World serverWorld = CreateWorldWithHoles(entities);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    EXPECT_TRUE(RoundTripSnapshotThroughWire(serverWorld, clientWorld, 7));

    // free list는 LIFO라 3 → 1 순으로 회수되고, 그 뒤에야 새 index 5가 나온다.
    // 회수된 index의 version은 파괴 때 1 올라가 있어야 한다.
    const EF::EntityId serverFirst = serverWorld.CreateEntity();
    const EF::EntityId clientFirst = clientWorld.CreateEntity();
    EXPECT_EQ(EF::GetEntityIndex(serverFirst), static_cast<EF::EntityIndex>(3));
    EXPECT_EQ(EF::GetEntityVersion(serverFirst), static_cast<EF::EntityVersion>(1));
    EXPECT_EQ(clientFirst, serverFirst);

    const EF::EntityId serverSecond = serverWorld.CreateEntity();
    const EF::EntityId clientSecond = clientWorld.CreateEntity();
    EXPECT_EQ(EF::GetEntityIndex(serverSecond), static_cast<EF::EntityIndex>(1));
    EXPECT_EQ(EF::GetEntityVersion(serverSecond), static_cast<EF::EntityVersion>(1));
    EXPECT_EQ(clientSecond, serverSecond);

    // free list가 마르면 양쪽 모두 새 index로 넘어간다.
    const EF::EntityId serverThird = serverWorld.CreateEntity();
    const EF::EntityId clientThird = clientWorld.CreateEntity();
    EXPECT_EQ(EF::GetEntityIndex(serverThird), static_cast<EF::EntityIndex>(5));
    EXPECT_EQ(EF::GetEntityVersion(serverThird), static_cast<EF::EntityVersion>(0));
    EXPECT_EQ(clientThird, serverThird);
}

TEST_CASE(Network, WorldHashStaysEqualWhenBothSidesSpawnAfterSnapshot) {
    std::vector<EF::EntityId> entities;
    EF::World serverWorld = CreateWorldWithHoles(entities);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    EXPECT_TRUE(RoundTripSnapshotThroughWire(serverWorld, clientWorld, 11));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    // 확정 입력을 양쪽이 똑같이 적용한 상황을 흉내낸다 — 같은 횟수, 같은 컴포넌트.
    // 스폰을 복제하지 않고도 해시가 유지되는 것이 결정론 lockstep의 전제다.
    for (int i = 0; i < 4; ++i) {
        const EF::Transform spawned{
            .position = EF::Vector3(100 + i, 200 + i, 300 + i),
            .rotation = EF::Quaternion::Identity()
        };
        serverWorld.AddComponent<EF::Transform>(serverWorld.CreateEntity(), spawned);
        clientWorld.AddComponent<EF::Transform>(clientWorld.CreateEntity(), spawned);
    }

    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, SnapshotKeepsEntityWithoutSnapshotComponents) {
    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();

    const EF::EntityId withComponent = serverWorld.CreateEntity();
    serverWorld.AddComponent<EF::Transform>(withComponent, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });

    // 컴포넌트를 하나도 붙이지 않는다. 컴포넌트 데이터에는 흔적이 남지 않으므로
    // 엔티티 상태를 따로 싣지 않으면 이 엔티티는 스냅샷을 넘어가며 사라진다.
    // (서버가 Hello를 받고 만드는 controlledEntity가 정확히 이 모양이다.)
    const EF::EntityId bare = serverWorld.CreateEntity();

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    EXPECT_TRUE(RoundTripSnapshotThroughWire(serverWorld, clientWorld, 3));

    EXPECT_TRUE(clientWorld.IsAlive(withComponent));
    EXPECT_TRUE(clientWorld.IsAlive(bare));

    // 살아남았다면 다음 할당도 그 뒤에서 시작해야 한다.
    EXPECT_EQ(clientWorld.CreateEntity(), serverWorld.CreateEntity());
}

TEST_CASE(Network, SnapshotRejectsInconsistentEntityState) {
    std::vector<EF::EntityId> entities;
    EF::World serverWorld = CreateWorldWithHoles(entities);
    const EF::WorldSnapshot valid = serverWorld.CreateSnapshot(1);

    // 변형한 스냅샷을 실제 와이어 왕복(Encode → Decode)까지 태운 뒤 적용한다.
    // 구조체를 곧장 ApplySnapshot에 넘기면 인코더나 디코더가 필드를 통째로 빠뜨려도
    // 이 테스트가 통과해 버린다 — 검증하려는 대상이 와이어를 건넌 스냅샷이므로
    // 왕복을 거치는 것이 맞다.
    auto applyMutated = [&valid](auto&& mutate) {
        EF::WorldSnapshot snapshot = valid;
        mutate(snapshot);

        const std::vector<uint8_t> payload = EF::NetworkManager::EncodeWorldSnapshot(snapshot);
        EF::WorldSnapshot decoded;
        if (!EF::NetworkManager::DecodeWorldSnapshot(payload, decoded)) {
            // 디코더가 먼저 걸러낸 것도 "거부"다.
            return false;
        }

        EF::World clientWorld;
        clientWorld.RegisterSnapshotComponent<EF::Transform>();
        return clientWorld.ApplySnapshot(decoded);
    };

    // 온전한 스냅샷은 통과한다(아래 거부들이 다른 이유로 실패한 게 아님을 보장).
    EXPECT_TRUE(applyMutated([](EF::WorldSnapshot&) {}));

    // nextEntityIndex와 versions 길이가 어긋나면 거부.
    EXPECT_FALSE(applyMutated([](EF::WorldSnapshot& snapshot) {
        snapshot.entities.nextEntityIndex += 1;
    }));

    // 범위 밖 free index는 거부.
    EXPECT_FALSE(applyMutated([](EF::WorldSnapshot& snapshot) {
        snapshot.entities.freeIndices.push_back(snapshot.entities.nextEntityIndex);
    }));

    // 중복 free index는 거부 — 같은 index가 두 번 살아나면 엔티티가 겹친다.
    EXPECT_FALSE(applyMutated([](EF::WorldSnapshot& snapshot) {
        snapshot.entities.freeIndices.push_back(snapshot.entities.freeIndices.front());
    }));

    // 죽은 index에 컴포넌트가 붙어 있으면 거부. 그냥 받으면 EnsureEntityExists가
    // 복원된 free list를 갉아먹어서 다음 할당이 서버와 갈라진다.
    EXPECT_FALSE(applyMutated([](EF::WorldSnapshot& snapshot) {
        snapshot.components[0].entities[0] = snapshot.entities.freeIndices.front();
    }));

    // 클라가 등록하지 않은 타입에 실린 잘못된 index도 거부해야 한다.
    // 미등록 타입을 먼저 건너뛰면 이 검사가 통째로 빠져서, 자기모순인 스냅샷이
    // 통과한다. 여기서는 타입 해시만 바꿔 "이 클라가 모르는 타입"으로 만든다.
    EXPECT_FALSE(applyMutated([](EF::WorldSnapshot& snapshot) {
        snapshot.components[0].entities[0] = snapshot.entities.freeIndices.front();
        snapshot.components[0].type.hash ^= 0xA5A5A5A5A5A5A5A5ull;
    }));
}

TEST_CASE(Network, WorldHashChangesWhenComponentDataChanges) {
    EF::World world;
    world.RegisterSnapshotComponent<EF::Transform>();

    const EF::EntityId entity = world.CreateEntity();
    world.AddComponent<EF::Transform>(entity, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });

    const uint64_t firstHash = world.ComputeHash();
    auto view = world.CreateView<EF::Transform>();
    view.ForEach([](EF::EntityIndex, EF::Transform& transform) {
        transform.position = EF::Vector3(9, 8, 7);
    });

    EXPECT_FALSE(firstHash == world.ComputeHash());
}

TEST_CASE(Network, MemoryConnectionCarriesHelloTypeTableAndSnapshotPackets) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();
    EF::NetworkManager clientNetwork(clientConnection);
    EF::NetworkManager serverNetwork(serverConnection);

    EXPECT_TRUE(clientNetwork.SendHello());
    auto helloPacket = serverNetwork.TryReceivePacket();
    EXPECT_TRUE(helloPacket.has_value());
    EXPECT_EQ(static_cast<int>(helloPacket->type), static_cast<int>(EF::Network::PacketType::Hello));

    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();
    const EF::EntityId entity = serverWorld.CreateEntity();
    serverWorld.AddComponent<EF::Transform>(entity, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });

    EXPECT_TRUE(serverNetwork.SendTypeTable(serverWorld.BuildTypeTable()));
    auto typeTablePacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(typeTablePacket.has_value());
    EXPECT_EQ(static_cast<int>(typeTablePacket->type), static_cast<int>(EF::Network::PacketType::TypeTable));

    std::vector<EF::ComponentTypeDescriptor> decodedTypes;
    EXPECT_TRUE(EF::NetworkManager::DecodeTypeTable(typeTablePacket->payload, decodedTypes));
    EXPECT_EQ(decodedTypes.size(), serverWorld.BuildTypeTable().size());

    EXPECT_TRUE(clientNetwork.SendWorldSnapshotRequest(0));
    auto requestPacket = serverNetwork.TryReceivePacket();
    EXPECT_TRUE(requestPacket.has_value());
    EXPECT_EQ(static_cast<int>(requestPacket->type), static_cast<int>(EF::Network::PacketType::WorldSnapshotRequest));

    EXPECT_TRUE(serverNetwork.SendWorldSnapshot(serverWorld.CreateSnapshot(10)));
    auto snapshotPacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(snapshotPacket.has_value());
    EXPECT_EQ(static_cast<int>(snapshotPacket->type), static_cast<int>(EF::Network::PacketType::WorldSnapshot));

    EF::WorldSnapshot decodedSnapshot;
    EXPECT_TRUE(EF::NetworkManager::DecodeWorldSnapshot(snapshotPacket->payload, decodedSnapshot));

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    EXPECT_TRUE(clientWorld.ApplySnapshot(decodedSnapshot));
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, ResyncRequestCanTriggerSnapshotReapply) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();
    EF::NetworkManager clientNetwork(clientConnection);
    EF::NetworkManager serverNetwork(serverConnection);

    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();
    const EF::EntityId serverEntity = serverWorld.CreateEntity();
    serverWorld.AddComponent<EF::Transform>(serverEntity, EF::Transform{
        .position = EF::Vector3(1, 2, 3),
        .rotation = EF::Quaternion::Identity()
    });

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    const EF::EntityId clientEntity = clientWorld.CreateEntity();
    clientWorld.AddComponent<EF::Transform>(clientEntity, EF::Transform{
        .position = EF::Vector3(9, 8, 7),
        .rotation = EF::Quaternion::Identity()
    });

    const uint64_t serverHash = serverWorld.ComputeHash();
    const uint64_t clientHash = clientWorld.ComputeHash();
    EXPECT_FALSE(serverHash == clientHash);

    EXPECT_TRUE(clientNetwork.SendResyncRequest(EF::ResyncRequest{
        .tick = 50,
        .remoteHash = serverHash,
        .localBreakdown = clientWorld.ComputeHashBreakdown()
    }));
    auto resyncPacket = serverNetwork.TryReceivePacket();
    EXPECT_TRUE(resyncPacket.has_value());
    EXPECT_EQ(static_cast<int>(resyncPacket->type), static_cast<int>(EF::Network::PacketType::ResyncRequest));

    EXPECT_TRUE(serverNetwork.SendWorldSnapshot(serverWorld.CreateSnapshot(50)));
    auto snapshotPacket = clientNetwork.TryReceivePacket();
    EXPECT_TRUE(snapshotPacket.has_value());

    EF::WorldSnapshot snapshot;
    EXPECT_TRUE(EF::NetworkManager::DecodeWorldSnapshot(snapshotPacket->payload, snapshot));
    EXPECT_TRUE(clientWorld.ApplySnapshot(snapshot));
    EXPECT_EQ(clientWorld.ComputeHash(), serverHash);
}

TEST_CASE(Network, ServerDeduplicatesInputByTickAndRejectsConfirmedPast) {
    EF::NetworkManager serverNetwork(EF::Network::MemoryConnection::CreatePair().second);

    // 다른 tick 슬롯이면 수락된다.
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 1 }));
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 2, .sequence = 2 }));

    // 같은 tick 슬롯은 중복으로 폐기된다.
    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 2, .sequence = 2 }));
    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 1 }));

    std::vector<EF::InputCommand> first = serverNetwork.ConfirmInputs(1);
    EXPECT_EQ(first.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(first[0].sequence, static_cast<uint32_t>(1));
    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 100 }));

    // 아직 확정되지 않은 미래 tick은 계속 수락된다.
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 3, .sequence = 3 }));

    std::vector<EF::InputCommand> second = serverNetwork.ConfirmInputs(2);
    EXPECT_EQ(second.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(second[0].sequence, static_cast<uint32_t>(2));

    const EF::NetworkClientState* state = serverNetwork.FindClient(5);
    EXPECT_TRUE(state != nullptr);
    EXPECT_EQ(state->inputByTick.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(state->inputByTick.find(3) != state->inputByTick.end());
}

TEST_CASE(Network, ReliabilityChannelDeliversReliableInOrderDespiteReorder) {
    using EF::Network::ReliabilityChannel;
    const auto record = [](uint8_t value) { return std::vector<uint8_t>{ value }; };

    ReliabilityChannel sender;
    ReliabilityChannel receiver;

    // 하나씩 넣고 flush해서 각각 별도 데이터그램으로 만든다.
    sender.Enqueue(ReliabilityChannel::Delivery::Reliable, record(10));
    std::vector<std::vector<uint8_t>> first = sender.Flush(0);
    sender.Enqueue(ReliabilityChannel::Delivery::Reliable, record(20));
    std::vector<std::vector<uint8_t>> second = sender.Flush(0);
    sender.Enqueue(ReliabilityChannel::Delivery::Reliable, record(30));
    std::vector<std::vector<uint8_t>> third = sender.Flush(0);

    EXPECT_EQ(first.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(second.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(third.size(), static_cast<std::size_t>(1));

    // 역순으로 도착시켜도 원래 순서(10,20,30)로 전달되어야 한다.
    std::vector<std::vector<uint8_t>> delivered;
    receiver.OnReceive(third[0], delivered);
    receiver.OnReceive(second[0], delivered);
    receiver.OnReceive(first[0], delivered);

    EXPECT_EQ(delivered.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(delivered[0][0], static_cast<uint8_t>(10));
    EXPECT_EQ(delivered[1][0], static_cast<uint8_t>(20));
    EXPECT_EQ(delivered[2][0], static_cast<uint8_t>(30));
}

TEST_CASE(Network, ReliabilityChannelKeepsOnlyNewestSequenced) {
    using EF::Network::ReliabilityChannel;
    const auto record = [](uint8_t value) { return std::vector<uint8_t>{ value }; };

    ReliabilityChannel sender;
    ReliabilityChannel receiver;

    sender.Enqueue(ReliabilityChannel::Delivery::Sequenced, record(1));
    std::vector<std::vector<uint8_t>> older = sender.Flush(0);
    sender.Enqueue(ReliabilityChannel::Delivery::Sequenced, record(2));
    std::vector<std::vector<uint8_t>> newer = sender.Flush(0);

    // 최신(2)을 먼저 받고, 구식(1)이 뒤늦게 오면 버려야 한다.
    std::vector<std::vector<uint8_t>> delivered;
    receiver.OnReceive(newer[0], delivered);
    receiver.OnReceive(older[0], delivered);

    EXPECT_EQ(delivered.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(delivered[0][0], static_cast<uint8_t>(2));
}

TEST_CASE(Network, ReliabilityChannelReassemblesFragmentedRecord) {
    using EF::Network::ReliabilityChannel;

    // 조각화 임계(1024)를 넘는 큰 레코드.
    std::vector<uint8_t> big(3000);
    for (std::size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    ReliabilityChannel sender;
    ReliabilityChannel receiver;
    sender.Enqueue(ReliabilityChannel::Delivery::Reliable, big);
    std::vector<std::vector<uint8_t>> datagrams = sender.Flush(0);

    // 여러 조각으로 나뉘어야 한다.
    EXPECT_TRUE(datagrams.size() > static_cast<std::size_t>(1));

    // 조각을 역순으로 넣어도 재조립되어야 한다.
    std::vector<std::vector<uint8_t>> delivered;
    for (auto it = datagrams.rbegin(); it != datagrams.rend(); ++it) {
        receiver.OnReceive(*it, delivered);
    }

    EXPECT_EQ(delivered.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(delivered[0] == big);
}

TEST_CASE(Network, ServerRoutesToMultipleClientsThroughSingleNetworkManager) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(12345);
    const Endpoint serverEp = Endpoint::Memory(1000);
    const Endpoint clientOneEp = Endpoint::Memory(1);
    const Endpoint clientTwoEp = Endpoint::Memory(2);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld;
    PopulateAuthoritativeWorld(serverWorld, 1);
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);
    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);
    static_cast<void>(clientTwoSync);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 40; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    EF::NetworkManager& serverNet = serverWorld.GetResource<EF::NetworkManager>();
    EF::NetworkManager& clientOneNet = clientOneWorld.GetResource<EF::NetworkManager>();
    EF::NetworkManager& clientTwoNet = clientTwoWorld.GetResource<EF::NetworkManager>();

    // 서버가 서로 다른 id를 발급하고, 두 클라이언트를 개별 추적한다.
    EXPECT_TRUE(clientOneNet.LocalClientId() != static_cast<uint32_t>(0));
    EXPECT_TRUE(clientTwoNet.LocalClientId() != static_cast<uint32_t>(0));
    EXPECT_TRUE(clientOneNet.LocalClientId() != clientTwoNet.LocalClientId());
    EXPECT_EQ(serverNet.Clients().size(), static_cast<std::size_t>(2));

    // 두 클라이언트 모두 각자 endpoint로 스냅샷을 받아 서버와 동기화된다.
    // (라우팅이 깨졌다면 한쪽은 수렴하지 못한다.)
    EXPECT_EQ(clientOneWorld.ComputeHash(), serverWorld.ComputeHash());
    EXPECT_EQ(clientTwoWorld.ComputeHash(), serverWorld.ComputeHash());

    // 입력이 보낸 클라이언트의 서버 큐로만 라우팅되는지 확인.
    clientOneSync.QueueLocalInput(EF::InputCommand{ .buttonMask = 0b111 });
    for (uint64_t tick = 41; tick <= 46; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    const uint32_t idOne = clientOneNet.LocalClientId();
    const uint32_t idTwo = clientTwoNet.LocalClientId();
    const EF::NetworkClientState* serverClientOne = serverNet.FindClient(idOne);
    const EF::NetworkClientState* serverClientTwo = serverNet.FindClient(idTwo);
    EXPECT_TRUE(serverClientOne != nullptr);
    EXPECT_TRUE(serverClientTwo != nullptr);
    EXPECT_TRUE(serverClientOne->hasConfirmed);
    EXPECT_TRUE(serverClientTwo->hasConfirmed);
    EXPECT_EQ(serverClientOne->lastConfirmed.buttonMask, static_cast<uint64_t>(0b111));
    EXPECT_EQ(serverClientOne->lastConfirmed.clientId, idOne);
    EXPECT_EQ(serverClientTwo->lastConfirmed.buttonMask, static_cast<uint64_t>(0));
}

TEST_CASE(Network, SyncSystemConvergesOverLossyReorderingLink) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(99);
    net.SetLoss(0.2f);
    net.SetDuplicate(0.1f);
    net.SetReorder(0.3f, 4);

    const Endpoint serverEp = Endpoint::Memory(500);
    const Endpoint clientEp = Endpoint::Memory(1);
    auto serverHost = net.CreateHost(serverEp);
    auto clientHost = net.CreateHost(clientEp, serverEp);

    EF::World serverWorld;
    PopulateAuthoritativeWorld(serverWorld, 3);
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = AttachClientWorld(clientWorld, clientHost);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 300; ++tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    // 손실/중복/재정렬에도 신뢰 채널이 결국 상태를 맞춘다.
    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, LockstepConvergesTwoClientsOverLossyReorderingLink) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260709);
    net.SetLoss(0.08f);
    net.SetDuplicate(0.05f);
    net.SetReorder(0.2f, 4);

    const Endpoint serverEp = Endpoint::Memory(900);
    const Endpoint clientOneEp = Endpoint::Memory(901);
    const Endpoint clientTwoEp = Endpoint::Memory(902);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);

    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 700; ++tick) {
        if (clientOneSync.HasSnapshot()) {
            if (tick >= 120 && tick < 128) {
                clientOneSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
            } else if (tick >= 128 && tick < 160) {
                clientOneSync.QueueLocalInput(EF::InputCommand{});
            }
        }

        if (clientTwoSync.HasSnapshot()) {
            if (tick >= 130 && tick < 138) {
                clientTwoSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveForward });
            } else if (tick >= 138 && tick < 170) {
                clientTwoSync.QueueLocalInput(EF::InputCommand{});
            }
        }

        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    EXPECT_TRUE(clientOneSync.HasSnapshot());
    EXPECT_TRUE(clientTwoSync.HasSnapshot());
    EXPECT_FALSE(clientOneSync.HasHashMismatch());
    EXPECT_FALSE(clientTwoSync.HasHashMismatch());
    EXPECT_EQ(clientOneWorld.ComputeHash(), serverWorld.ComputeHash());
    EXPECT_EQ(clientTwoWorld.ComputeHash(), serverWorld.ComputeHash());
}

TEST_CASE(Network, LargeSnapshotReassemblesOverLossyLink) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(7);
    net.SetLoss(0.15f);

    const Endpoint serverEp = Endpoint::Memory(800);
    const Endpoint clientEp = Endpoint::Memory(1);
    auto serverHost = net.CreateHost(serverEp);
    auto clientHost = net.CreateHost(clientEp, serverEp);

    EF::World serverWorld;
    PopulateAuthoritativeWorld(serverWorld, 60); // 스냅샷이 조각화 임계를 크게 넘도록
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = AttachClientWorld(clientWorld, clientHost);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 400; ++tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    // 큰 스냅샷이 여러 조각으로 나뉘고 손실을 넘어 재조립되어야 한다.
    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

// 회귀 테스트: 여러 InputFrame이 한 FixedUpdate에 몰아서 적용될 때
// 과거 tick의 원격 해시를 "현재" 월드 해시와 비교하면 안 된다.
//
// 재현 조건: 클라가 잠시 펌프하지 못하는 사이 서버가 여러 tick을 진행하면,
// 다음 펌프에서 프레임 여러 개가 한꺼번에 적용되어 clientSimTick이 점프한다.
// 그 사이 도착해 있던 tick T의 TickHash를 tick T+n 상태와 비교하면
// 정상인데도 불일치로 판정되어 resync 폭풍이 난다.
TEST_CASE(Network, BatchedInputFramesDoNotCauseFalseHashMismatch) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    const EF::Fixed64 dt = FixedTickDelta();

    // 핸드셰이크
    uint64_t tick = 1;
    for (; tick <= 3; ++tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }
    EXPECT_TRUE(clientSync.HasSnapshot());

    // 월드가 tick마다 실제로 바뀌도록 이동 입력을 넣는다.
    // (상태가 안 변하면 해시가 같아 이 버그가 드러나지 않는다.)
    clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
    clientWorld.FixedUpdate(dt, tick);
    serverWorld.FixedUpdate(dt, tick);
    ++tick;

    // 클라를 멈춘 채 서버만 여러 tick 진행 → 프레임과 TickHash가 쌓인다.
    const uint64_t serverOnlyTicks = 5;
    for (uint64_t i = 0; i < serverOnlyTicks; ++i, ++tick) {
        serverWorld.FixedUpdate(dt, tick);
    }

    // 이제 클라가 한 번에 따라잡는다. 여기서 허위 불일치가 나면 안 된다.
    clientWorld.FixedUpdate(dt, tick);

    EXPECT_FALSE(clientSync.HasHashMismatch());
}

// 서버가 Hello를 받으면 그 클라이언트가 "조종할 수 있고 그려질 수 있는" 엔티티를 만든다.
//
// 회귀 방지 대상: 예전에는 world.CreateEntity()만 하고 컴포넌트를 하나도 붙이지 않아서,
// 접속한 클라이언트에게 조종할 대상도 그릴 위치도 없었다. 테스트가 손으로 컴포넌트를
// 붙이고 있었기 때문에 이 구멍이 드러나지 않았다.
TEST_CASE(Network, ServerSpawnsControllablePlayerEntityOnHello) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld = CreateLockstepServerWorld();
    EF::NetworkManager& serverNetwork = serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    clientWorld.RegisterSnapshotComponent<EF::Transform>();
    clientWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    clientWorld.AddResource<EF::NetworkManager>(clientConnection);
    EF::NetworkSyncSystem& clientSync = clientWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);

    // 출발점: 서버 월드에 플레이어는 없다. 스폰은 오직 Hello 처리에서만 일어난다.
    EXPECT_FALSE(ProbePlayer(serverWorld, 1).found);

    const EF::Fixed64 dt = FixedTickDelta();
    clientWorld.FixedUpdate(dt, 1); // 클라가 Hello를 보낸다
    serverWorld.FixedUpdate(dt, 1); // 서버가 플레이어를 스폰하고 Welcome + TypeTable로 답한다
    clientWorld.FixedUpdate(dt, 2); // 클라가 스냅샷을 요청한다
    serverWorld.FixedUpdate(dt, 2); // 서버가 스냅샷을 보낸다
    clientWorld.FixedUpdate(dt, 3); // 클라가 스냅샷을 적용한다

    EXPECT_TRUE(clientSync.HasSnapshot());

    const EF::NetworkClientState* serverClient = serverNetwork.FindClient(1);
    EXPECT_TRUE(serverClient != nullptr);
    EXPECT_TRUE(serverClient->hasControlledEntity);

    // 서버: Transform + PlayerControl을 갖춘 엔티티가 정확히 하나 생겼고,
    // 그것이 NetworkClientState가 가리키는 조종 엔티티다.
    const PlayerProbe serverPlayer = ProbePlayer(serverWorld, 1);
    EXPECT_TRUE(serverPlayer.found);
    EXPECT_EQ(serverPlayer.matchCount, static_cast<std::size_t>(1));
    EXPECT_EQ(serverPlayer.entityIndex, EF::GetEntityIndex(serverClient->controlledEntity));

    // 첫 클라이언트(clientId == 1)의 스폰 슬롯은 0이므로 원점이다.
    EXPECT_TRUE(serverPlayer.position == EF::Vector3(0, 0, 0));

    // 클라이언트도 스냅샷으로 같은 엔티티를 받아야 한다.
    // 못 받으면 클라 쪽 View<Transform, PlayerControl>이 비어서 확정 입력이
    // 아무 데도 적용되지 않고, 서버만 움직이는 디싱크가 된다.
    const PlayerProbe clientPlayer = ProbePlayer(clientWorld, 1);
    EXPECT_TRUE(clientPlayer.found);
    EXPECT_EQ(clientPlayer.entityIndex, serverPlayer.entityIndex);
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

// 확정 입력은 그 입력을 보낸 클라이언트의 엔티티 **하나만** 움직여야 한다.
// PlayerControl.clientId 매칭이 깨지면(예: 스폰이 clientId를 안 넣으면)
// 아무도 안 움직이거나 전원이 같이 움직인다.
TEST_CASE(Network, ConfirmedInputMovesOnlyTheOwningPlayerEntity) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260823);
    const Endpoint serverEp = Endpoint::Memory(2000);
    const Endpoint clientOneEp = Endpoint::Memory(2001);
    const Endpoint clientTwoEp = Endpoint::Memory(2002);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);
    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 20; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }
    EXPECT_TRUE(clientOneSync.HasSnapshot());
    EXPECT_TRUE(clientTwoSync.HasSnapshot());

    const uint32_t idOne = clientOneWorld.GetResource<EF::NetworkManager>().LocalClientId();
    const uint32_t idTwo = clientTwoWorld.GetResource<EF::NetworkManager>().LocalClientId();
    EXPECT_TRUE(idOne != idTwo);

    const PlayerProbe beforeOne = ProbePlayer(serverWorld, idOne);
    const PlayerProbe beforeTwo = ProbePlayer(serverWorld, idTwo);
    EXPECT_TRUE(beforeOne.found);
    EXPECT_TRUE(beforeTwo.found);

    // 확정 입력을 서버 월드에 직접 적용한다. 릴레이 경로를 태우지 않는 이유는 결과가
    // "확정 입력이 몇 tick 동안 hold되는가"에 흔들리지 않게 하려는 것이다.
    // (릴레이까지 포함한 수렴은 LockstepConverges... 테스트들이 이미 덮는다.)
    const std::array<EF::InputCommand, 1> confirmed{
        EF::InputCommand{ .clientId = idOne, .buttonMask = EF::kInputMoveRight }
    };
    EF::ApplyConfirmedInputs(serverWorld, confirmed);

    const PlayerProbe afterOne = ProbePlayer(serverWorld, idOne);
    const PlayerProbe afterTwo = ProbePlayer(serverWorld, idTwo);

    // 입력을 보낸 쪽만 +X로 1유닛(tick당 고정 스텝) 움직인다.
    EXPECT_TRUE(afterOne.position == beforeOne.position + EF::Vector3(1, 0, 0));
    EXPECT_TRUE(afterTwo.position == beforeTwo.position);
}

// 두 클라이언트의 스폰 위치가 서로 달라야 한다.
// 전원이 원점에 겹쳐 나오면 화면에서 누가 누군지 구별할 수 없다.
TEST_CASE(Network, PlayerSpawnPositionsDifferPerClient) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260824);
    const Endpoint serverEp = Endpoint::Memory(2100);
    const Endpoint clientOneEp = Endpoint::Memory(2101);
    const Endpoint clientTwoEp = Endpoint::Memory(2102);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);
    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);

    const EF::Fixed64 dt = FixedTickDelta();
    for (uint64_t tick = 1; tick <= 20; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }
    EXPECT_TRUE(clientOneSync.HasSnapshot());
    EXPECT_TRUE(clientTwoSync.HasSnapshot());

    const uint32_t idOne = clientOneWorld.GetResource<EF::NetworkManager>().LocalClientId();
    const uint32_t idTwo = clientTwoWorld.GetResource<EF::NetworkManager>().LocalClientId();
    EXPECT_TRUE(idOne != idTwo);

    const PlayerProbe playerOne = ProbePlayer(serverWorld, idOne);
    const PlayerProbe playerTwo = ProbePlayer(serverWorld, idTwo);
    EXPECT_TRUE(playerOne.found);
    EXPECT_TRUE(playerTwo.found);
    EXPECT_FALSE(playerOne.position == playerTwo.position);

    // 스폰 규칙: x = (clientId - 1) * 간격, y = z = 0. clientId에서만 유도하므로
    // 스냅샷을 받은 클라이언트도 같은 값을 그대로 재현한다.
    //
    // ⚠️ 여기 6은 NetworkSyncSystem.cpp의 kPlayerSpawnSpacingUnits를 옮겨 적은 값이다
    //    (그 상수는 구현 세부라 헤더로 내보내지 않았다). 간격을 바꾸면 이 테스트가
    //    깨지면서 "스폰 규칙이 바뀌었다"고 알려 준다 — 그게 의도다.
    constexpr int32_t kSpawnSpacingUnits = 6;
    EXPECT_TRUE(playerOne.position ==
                EF::Vector3(static_cast<int32_t>(idOne - 1) * kSpawnSpacingUnits, 0, 0));
    EXPECT_TRUE(playerTwo.position ==
                EF::Vector3(static_cast<int32_t>(idTwo - 1) * kSpawnSpacingUnits, 0, 0));

    // 클라이언트 쪽에도 두 플레이어가 모두 와 있어야 한다(= 남의 캐릭터도 그릴 수 있다).
    EXPECT_TRUE(ProbePlayer(clientOneWorld, idTwo).found);
    EXPECT_TRUE(ProbePlayer(clientTwoWorld, idOne).found);
}

// ---------------------------------------------------------------------------
// LossyConnection — 실제 전송 경로 위에 손실/중복/지연을 주입하는 데코레이터.
//
// SimulatedNetwork 계열 테스트와 다른 점: 여기서는 "허브"가 아니라 이미 존재하는
// MemoryConnection을 한 겹 감싼다. 같은 방식으로 UDPConnection도 감쌀 수 있다는 것이
// 이 클래스의 요점이라, 테스트도 데코레이터 형태 그대로 쓴다.
// ---------------------------------------------------------------------------
namespace {
    // 내용으로 "어떤 패킷이 살아남았는지"를 식별하려고 첫 바이트에 일련번호를 박는다.
    std::vector<uint8_t> NumberedDatagram(uint8_t id) {
        return std::vector<uint8_t>{ id, static_cast<uint8_t>(0xA0 + id) };
    }

    // 신뢰 채널용 레코드. 600바이트로 부풀리는 이유가 있다 — ReliabilityChannel::Flush는
    // 대기 중인 레코드를 한 데이터그램(최대 1200바이트)에 몰아 담는다. 2바이트짜리를 8개
    // 넣으면 데이터그램 **하나**로 끝나서, 그 하나가 운 좋게 살아남으면 손실률을 아무리
    // 올려도 재전송이 한 번도 안 걸린다(실제로 처음엔 그렇게 짜서 테스트가 무의미했다).
    // 600바이트면 데이터그램당 2개씩 나뉘어 매 flush가 여러 데이터그램을 낳는다.
    // 조각화 임계(1024)보다는 작게 둬서, 여기서는 재전송만 검증하고 재조립은
    // ReliabilityChannelReassemblesFragmentedRecord에 맡긴다.
    std::vector<uint8_t> PaddedRecord(uint8_t id) {
        std::vector<uint8_t> record(600);
        for (std::size_t i = 0; i < record.size(); ++i) {
            record[i] = static_cast<uint8_t>(id * 31 + i * 7);
        }
        record[0] = id;
        return record;
    }

    // lossy 링크에 count개를 밀어넣고 반대편에 도착한 일련번호 목록을 돌려준다.
    std::vector<uint8_t> RunLossPattern(uint64_t seed, float lossRate, uint8_t count) {
        auto pair = EF::Network::MemoryConnection::CreatePair();

        EF::Network::LossyLinkSettings settings;
        settings.lossRate = lossRate;
        settings.seed = seed;

        EF::Network::LossyConnection lossy(pair.first, settings);
        for (uint8_t i = 0; i < count; ++i) {
            lossy.SendTo(lossy.DefaultPeer(), NumberedDatagram(i));
        }

        std::vector<uint8_t> arrived;
        while (auto datagram = pair.second->ReceiveFrom()) {
            arrived.push_back(datagram->bytes[0]);
        }
        return arrived;
    }
}

TEST_CASE(Network, LossyConnectionPassesEverythingThroughWhenDisabled) {
    auto pair = EF::Network::MemoryConnection::CreatePair();

    // 기본 생성 = 주입 없음. 실수로 켜둔 채 배포되는 사고를 막는 무해한 기본값.
    EF::Network::LossyConnection lossy(pair.first, EF::Network::LossyLinkSettings{});

    constexpr uint8_t kCount = 32;
    for (uint8_t i = 0; i < kCount; ++i) {
        EXPECT_TRUE(lossy.SendTo(lossy.DefaultPeer(), NumberedDatagram(i)));
    }

    // 지연 큐를 아예 쓰지 않아야 한다 — 기본값에서는 Advance를 잊어도 멈추지 않는다.
    EXPECT_EQ(lossy.PendingDelayedCount(), static_cast<std::size_t>(0));
    EXPECT_EQ(lossy.DroppedCount(), static_cast<uint64_t>(0));
    EXPECT_EQ(lossy.ForwardedCount(), static_cast<uint64_t>(kCount));

    std::vector<uint8_t> arrived;
    while (auto datagram = pair.second->ReceiveFrom()) {
        arrived.push_back(datagram->bytes[0]);
    }

    EXPECT_EQ(arrived.size(), static_cast<std::size_t>(kCount));
    for (uint8_t i = 0; i < kCount; ++i) {
        EXPECT_EQ(arrived[i], i);
    }
}

TEST_CASE(Network, LossyConnectionRepeatsTheSameDropPatternForTheSameSeed) {
    // 재현성이 이 클래스의 존재 이유다. 같은 시드 + 같은 송신 순서 = 같은 손실 패턴.
    const std::vector<uint8_t> first = RunLossPattern(20260823, 0.5f, 64);
    const std::vector<uint8_t> second = RunLossPattern(20260823, 0.5f, 64);
    EXPECT_TRUE(first == second);

    // 절반쯤은 살아남고 절반쯤은 죽어야 한다(전부 통과/전부 손실이면 위 비교가 무의미해진다).
    EXPECT_TRUE(!first.empty());
    EXPECT_TRUE(first.size() < static_cast<std::size_t>(64));

    // 시드가 다르면 패턴도 달라야 한다. 64번 시행에서 두 패턴이 우연히 같을 확률은
    // 사실상 0이라 flaky 걱정 없이 단언할 수 있다.
    const std::vector<uint8_t> other = RunLossPattern(1, 0.5f, 64);
    EXPECT_TRUE(first != other);
}

TEST_CASE(Network, LossyConnectionDropsEverythingAtFullLossRate) {
    auto pair = EF::Network::MemoryConnection::CreatePair();

    EF::Network::LossyLinkSettings settings;
    settings.lossRate = 1.0f;
    settings.seed = 7;

    EF::Network::LossyConnection lossy(pair.first, settings);
    for (uint8_t i = 0; i < 16; ++i) {
        // 버려도 SendTo는 true다 — 소켓 송신은 성공했고 패킷은 회선이 먹은 것이다.
        // 여기서 false를 내면 상위 계층이 송신 실패로 오해해 재시도가 이중으로 걸린다.
        EXPECT_TRUE(lossy.SendTo(lossy.DefaultPeer(), NumberedDatagram(i)));
    }

    EXPECT_EQ(lossy.DroppedCount(), static_cast<uint64_t>(16));
    EXPECT_EQ(lossy.ForwardedCount(), static_cast<uint64_t>(0));
    EXPECT_FALSE(pair.second->ReceiveFrom().has_value());
}

TEST_CASE(Network, LossyConnectionHoldsDelayedDatagramsUntilTheReleaseTick) {
    auto pair = EF::Network::MemoryConnection::CreatePair();

    EF::Network::LossyLinkSettings settings;
    settings.minDelayTicks = 3;
    settings.maxDelayTicks = 3; // 고정 지연 — 지터 없음
    settings.seed = 11;

    EF::Network::LossyConnection lossy(pair.first, settings);
    lossy.SendTo(lossy.DefaultPeer(), NumberedDatagram(1));

    // 아직 회선 위에 떠 있다.
    EXPECT_EQ(lossy.PendingDelayedCount(), static_cast<std::size_t>(1));
    EXPECT_FALSE(pair.second->ReceiveFrom().has_value());

    lossy.Advance(1);
    lossy.Advance(2);
    EXPECT_EQ(lossy.PendingDelayedCount(), static_cast<std::size_t>(1));
    EXPECT_FALSE(pair.second->ReceiveFrom().has_value());

    // 지연은 사라지는 게 아니라 미뤄지는 것이다 — 결국 도착한다.
    lossy.Advance(3);
    EXPECT_EQ(lossy.PendingDelayedCount(), static_cast<std::size_t>(0));

    auto datagram = pair.second->ReceiveFrom();
    EXPECT_TRUE(datagram.has_value());
    EXPECT_EQ(datagram->bytes[0], static_cast<uint8_t>(1));
}

TEST_CASE(Network, LossyConnectionDuplicatesDatagramsAtFullDuplicateRate) {
    auto pair = EF::Network::MemoryConnection::CreatePair();

    EF::Network::LossyLinkSettings settings;
    settings.duplicateRate = 1.0f;
    settings.seed = 5;

    EF::Network::LossyConnection lossy(pair.first, settings);
    lossy.SendTo(lossy.DefaultPeer(), NumberedDatagram(9));

    EXPECT_EQ(lossy.DuplicatedCount(), static_cast<uint64_t>(1));

    std::vector<uint8_t> arrived;
    while (auto datagram = pair.second->ReceiveFrom()) {
        arrived.push_back(datagram->bytes[0]);
    }
    EXPECT_EQ(arrived.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(arrived[0], static_cast<uint8_t>(9));
    EXPECT_EQ(arrived[1], static_cast<uint8_t>(9));
}

TEST_CASE(Network, ReliabilityChannelRecoversFromLossyConnectionByResending) {
    using EF::Network::ReliabilityChannel;

    // 이 파일에서 제일 값어치 있는 테스트: "손실이 있어도 신뢰 채널은 결국 전부 전달한다"를
    // 시뮬레이터가 아니라 **실제 전송 구현 위에 씌운 데코레이터**로 확인한다.
    // 양쪽 끝을 각각 감싸서 데이터 방향과 ack 방향 둘 다 열화시킨다(ack도 잃는다).
    auto pair = EF::Network::MemoryConnection::CreatePair();

    EF::Network::LossyLinkSettings settings;
    settings.lossRate = 0.4f;
    settings.minDelayTicks = 0;
    settings.maxDelayTicks = 2; // 지터 → 재정렬까지 저절로 섞인다
    settings.seed = 424242;
    auto senderLink = std::make_shared<EF::Network::LossyConnection>(pair.first, settings);

    settings.seed = 777777; // 방향마다 다른 수열을 써서 손실이 대칭으로 겹치지 않게 한다
    auto receiverLink = std::make_shared<EF::Network::LossyConnection>(pair.second, settings);

    ReliabilityChannel senderChannel;
    ReliabilityChannel receiverChannel;

    constexpr uint8_t kRecordCount = 8;
    for (uint8_t i = 0; i < kRecordCount; ++i) {
        senderChannel.Enqueue(ReliabilityChannel::Delivery::Reliable, PaddedRecord(i));
    }

    std::vector<std::vector<uint8_t>> delivered;
    constexpr uint64_t kMaxTicks = 400; // 재전송 간격 4틱 * 여유. 무한 루프 방지용 상한.
    uint64_t usedTicks = 0;

    for (uint64_t tick = 0; tick < kMaxTicks; ++tick) {
        usedTicks = tick;

        // ⚠️ 지연 큐를 굴리는 펌프. NetworkManager::Update(tick) 옆에서 같이 불러야 한다.
        senderLink->Advance(tick);
        receiverLink->Advance(tick);

        for (const std::vector<uint8_t>& datagram : senderChannel.Flush(tick)) {
            senderLink->SendTo(senderLink->DefaultPeer(), datagram);
        }
        while (auto received = receiverLink->ReceiveFrom()) {
            receiverChannel.OnReceive(received->bytes, delivered);
        }

        // 반대 방향(ack)도 같은 열화를 겪는다.
        for (const std::vector<uint8_t>& datagram : receiverChannel.Flush(tick)) {
            receiverLink->SendTo(receiverLink->DefaultPeer(), datagram);
        }
        while (auto received = senderLink->ReceiveFrom()) {
            std::vector<std::vector<uint8_t>> ignored;
            senderChannel.OnReceive(received->bytes, ignored);
        }

        if (delivered.size() == static_cast<std::size_t>(kRecordCount)) {
            break;
        }
    }

    // 전부, 그리고 보낸 순서대로 도착해야 한다.
    EXPECT_EQ(delivered.size(), static_cast<std::size_t>(kRecordCount));
    for (uint8_t i = 0; i < kRecordCount; ++i) {
        EXPECT_TRUE(delivered[i] == PaddedRecord(i));
    }

    // 링크가 실제로 나빴는지 확인한다. 손실이 0이었다면 위 단언은 아무것도 증명하지 못한다.
    EXPECT_TRUE(senderLink->DroppedCount() > static_cast<uint64_t>(0));
    EXPECT_TRUE(receiverLink->DroppedCount() > static_cast<uint64_t>(0));

    // 그리고 재전송으로 "결국" 이겼다는 것 — 상한에 걸려 끝난 게 아니다.
    EXPECT_TRUE(usedTicks < kMaxTicks - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 디싱크 탐지·복구 경로 (TickHash 불일치 → ResyncRequest → WorldSnapshot)
//
// 이 경로는 정상 세션에서 **한 번도 실행되지 않는다.** 실행되지 않는 코드는 조용히
// 썩고, 정작 필요한 날(실전 디싱크)에 처음 돌아간다. 그래서 디싱크를 일부러 만들어
// 놓고 전 과정을 확인한다.
//
// 주입 수단은 NetworkSyncSystem::DebugSkipNextConfirmedInputFrameOnce()다 —
// "확정 InputFrame 하나를 받았지만 적용하지 않는다". 고른 이유와 버린 대안은 그
// 함수의 헤더 주석에 있다.
//
// 허위 불일치(거짓 양성) 쪽은 이미 다른 테스트가 덮고 있어 여기서 반복하지 않는다:
//  - Network.BatchedInputFramesDoNotCauseFalseHashMismatch
//      → 프레임이 몰아서 적용되어 clientSimTick이 점프해도 허위 불일치가 없다.
//  - Network.LockstepConvergesTwoClientsOverLossyReorderingLink
//      → 손실·중복·재정렬 링크에서 700 tick을 돌려도 불일치가 뜨지 않는다.
// 아래의 NoInjectionKeepsCleanSession은 그 둘의 반복이 아니라 **이 파일의 시나리오
// 자체가 멀쩡하다는 대조군**이다(= 위 테스트들의 불일치는 주입 때문이지 배선 때문이 아니다).
// ─────────────────────────────────────────────────────────────────────────────
namespace {
    // 서버·클라 월드를 lockstep 한 쌍으로 배선하고 클라 쪽 동기화 시스템을 돌려준다.
    //
    // 왜 SimulatedNetwork(손실 링크)가 아니라 무손실 MemoryConnection인가:
    // 여기서 재려는 것은 "복구 경로가 도는가"와 "복구까지 몇 tick인가"다. 손실 링크를
    // 깔면 그 숫자가 손실 패턴에 좌우되어 회귀를 재는 자가 되지 못한다. 손실 환경에서의
    // 수렴은 Network.LockstepConvergesTwoClientsOverLossyReorderingLink가 이미 덮는다.
    EF::NetworkSyncSystem& WireLockstepPair(EF::World& serverWorld, EF::World& clientWorld) {
        auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

        // 플레이어 엔티티는 여기서 만들지 않는다 — 서버가 Hello를 받을 때 스폰한다
        // (CreateLockstepServerWorld의 주석과 같은 이유).
        serverWorld.RegisterSnapshotComponent<EF::Transform>();
        serverWorld.RegisterSnapshotComponent<EF::PlayerControl>();
        serverWorld.AddResource<EF::NetworkManager>(serverConnection);
        serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

        return AttachClientWorld(clientWorld, clientConnection);
    }

    // tick 하나를 굴린다. 클라 → 서버 순서는 기존 lockstep 테스트와 같게 유지한다.
    // 이 순서가 바뀌면 같은 시나리오라도 왕복 지연이 한 칸 달라져 측정값이 흔들린다.
    void StepLockstepPair(EF::World& clientWorld, EF::World& serverWorld, EF::Fixed64 dt, uint64_t tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    // startTick부터 tickCount만큼 굴리고, 다음에 쓸 tick 번호를 돌려준다.
    uint64_t AdvanceLockstepPair(EF::World& clientWorld, EF::World& serverWorld, EF::Fixed64 dt,
                                 uint64_t startTick, uint64_t tickCount) {
        for (uint64_t i = 0; i < tickCount; ++i) {
            StepLockstepPair(clientWorld, serverWorld, dt, startTick + i);
        }
        return startTick + tickCount;
    }

    // Hello → Welcome/TypeTable → WorldSnapshot 핸드셰이크에 필요한 tick 수.
    // 무손실 MemoryConnection에서 클라 → 서버 순으로 굴릴 때의 실측값이다.
    constexpr uint64_t kHandshakeTicks = 3;

    // 이동 입력을 넣고 확정 프레임이 실제로 월드를 바꾸는 상태가 될 때까지 기다리는 tick 수.
    // 입력 지연 2 tick + 왕복 1 tick보다 넉넉하게 잡았다.
    constexpr uint64_t kInputSettleTicks = 6;

    // 디싱크 주입 전까지의 공통 각본: 핸드셰이크 → 이동 입력 hold.
    //
    // 이동 입력을 **한 번만** 넣는 것이 핵심이다. 서버는 결측 tick을 "직전 입력 hold"로
    // 채우므로(NetworkManager::ConfirmInputs), 그 뒤 모든 확정 프레임이 매 tick 월드를
    // 바꾼다. 이 상태가 아니면 프레임 하나를 건너뛰어도 아무 일도 일어나지 않는다
    // (건너뛴 프레임이 no-op이면 디싱크가 생기지 않는다).
    uint64_t DriveToMovingLockstep(EF::World& clientWorld, EF::World& serverWorld,
                                   EF::NetworkSyncSystem& clientSync, EF::Fixed64 dt,
                                   uint32_t& outClientId) {
        uint64_t tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, 1, kHandshakeTicks);
        outClientId = clientWorld.GetResource<EF::NetworkManager>().LocalClientId();

        clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
        return AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, kInputSettleTicks);
    }

    struct RecoveryReport {
        uint64_t nextTick = 0;          // 이어서 쓸 tick 번호
        uint64_t ticksUntilRecovered = 0; // 0이면 예산 안에 복구되지 않았다는 뜻
        std::size_t appliedSnapshots = 0; // 복구되기까지 클라가 적용한 WorldSnapshot 개수
    };

    // 이동을 멈춘 뒤 서버와 다시 일치할 때까지 굴린다.
    //
    // ⚠️ **왜 굳이 이동을 멈추고 재는가.** 지금 구현은 월드가 매 tick 계속 바뀌는 동안에는
    //    복구가 끝나지 않는다 — 서버가 ResyncRequest에 답하는 스냅샷이 항상 한 tick 낡아서
    //    resync가 무한히 반복된다. 상세는 docs/design/deterministic-input-lockstep.md §7의
    //    "스냅샷 tick 라벨 off-by-one" 항목에 적어 두었다. 여기서는 그 결함을 우회해
    //    "복구 경로 자체는 돈다"를 재는 것이고, 결함은 문서로 남긴다.
    //
    // 해시를 서로 직접 비교해도 되는 이유도 같다: 클라는 서버보다 항상 한 tick 뒤처지므로,
    // 월드가 정지해 있을 때만 두 해시가 같아진다.
    RecoveryReport DriveUntilRecovered(EF::World& clientWorld, EF::World& serverWorld,
                                       EF::NetworkSyncSystem& clientSync, EF::Fixed64 dt,
                                       uint64_t startTick, uint64_t budgetTicks) {
        clientSync.QueueLocalInput(EF::InputCommand{}); // 이동 정지(빈 buttonMask)

        RecoveryReport report;
        uint64_t lastSnapshotTick = clientSync.LastSnapshotTick();

        uint64_t tick = startTick;
        for (uint64_t elapsed = 1; elapsed <= budgetTicks; ++elapsed, ++tick) {
            StepLockstepPair(clientWorld, serverWorld, dt, tick);

            if (clientSync.LastSnapshotTick() != lastSnapshotTick) {
                lastSnapshotTick = clientSync.LastSnapshotTick();
                ++report.appliedSnapshots;
            }

            if (!clientSync.HasHashMismatch() && clientWorld.ComputeHash() == serverWorld.ComputeHash()) {
                report.ticksUntilRecovered = elapsed;
                ++tick;
                break;
            }
        }

        report.nextTick = tick;
        return report;
    }
}

// (1) 탐지 — 확정 입력 하나를 놓치면 TickHash 비교가 그걸 잡아낸다.
TEST_CASE(Network, SkippedConfirmedInputFrameIsDetectedByTickHash) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);

    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_TRUE(clientId != 0);

    const PlayerProbe before = ProbePlayer(clientWorld, clientId);
    EXPECT_TRUE(before.found);
    const uint64_t simTickBefore = clientSync.ClientSimTick();

    // 여기서 확정 프레임 하나를 통째로 놓친다(디버그 주입).
    clientSync.DebugSkipNextConfirmedInputFrameOnce();
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);

    const PlayerProbe after = ProbePlayer(clientWorld, clientId);
    EXPECT_TRUE(after.found);

    // (a) 주입이 실제로 월드를 갈라놓았는가. 이 단언이 없으면 "건너뛴 프레임이 no-op이라
    //     아무 일도 안 일어났는데 통과"가 가능해진다 — 그러면 아래 (c)는 아무것도 증명하지 못한다.
    EXPECT_TRUE(after.position == before.position);

    // (b) 멈춘 게 아니라 **어긋난** 것이다. tick은 정상 진행해야 한다.
    //     (여기서 tick까지 멈추면 그냥 뒤처진 클라이언트라 디싱크 시나리오가 아니다.)
    EXPECT_EQ(clientSync.ClientSimTick(), simTickBefore + 1);

    // (c) 그리고 TickHash 비교가 그 사실을 잡아냈다.
    //     실측: 서버가 그 tick의 TickHash를 이미 보내 둔 상태라 **주입과 같은 tick에** 탐지된다.
    EXPECT_TRUE(clientSync.HasHashMismatch());
}

// (2) 복구 — 불일치를 본 클라가 새 스냅샷을 받아 서버와 다시 일치한다.
TEST_CASE(Network, DesyncedClientRecoversThroughResyncSnapshot) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);

    clientSync.DebugSkipNextConfirmedInputFrameOnce();
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
    EXPECT_TRUE(clientSync.HasHashMismatch());
    const uint64_t snapshotTickBeforeRecovery = clientSync.LastSnapshotTick();

    // 예산 30 tick(50Hz에서 0.6초). 실측은 3 tick / 스냅샷 3개다 —
    // 예산을 실측에 딱 맞추면 무관한 지연에도 깨지므로 한 자리 여유를 뒀다.
    constexpr uint64_t kRecoveryBudgetTicks = 30;
    const RecoveryReport report =
        DriveUntilRecovered(clientWorld, serverWorld, clientSync, dt, tick, kRecoveryBudgetTicks);

    EXPECT_TRUE(report.ticksUntilRecovered != 0);
    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    // 복구가 "새 스냅샷을 받아서" 일어났는지 확인한다. 이게 없으면 우연히 상태가
    // 다시 맞아떨어진 경우와 구분되지 않는다.
    EXPECT_TRUE(report.appliedSnapshots >= 1);
    EXPECT_TRUE(clientSync.LastSnapshotTick() > snapshotTickBeforeRecovery);

    // 위치까지 같아야 한다(해시는 같은데 위치가 다르면 해시 쪽을 의심해야 한다).
    const PlayerProbe serverPlayer = ProbePlayer(serverWorld, clientId);
    const PlayerProbe clientPlayer = ProbePlayer(clientWorld, clientId);
    EXPECT_TRUE(serverPlayer.found);
    EXPECT_TRUE(clientPlayer.found);
    EXPECT_TRUE(serverPlayer.position == clientPlayer.position);
}

// (3) 복구 이후 — 다시 같은 확정 입력으로 진행해도 계속 일치한다.
//
// 이걸 따로 확인하는 이유: 스냅샷을 덮어쓰는 과정에서 클라의 tick 기준선
// (clientSimTick / m_localHashByTick)이 어긋난 채 남으면, 복구 직후에는 멀쩡해 보여도
// 다음 입력부터 다시 불일치가 뜬다. "복구했다"는 그 다음 tick들까지 봐야 참이 된다.
TEST_CASE(Network, RecoveredClientKeepsMatchingServerAfterResync) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);

    clientSync.DebugSkipNextConfirmedInputFrameOnce();
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);

    constexpr uint64_t kRecoveryBudgetTicks = 30;
    const RecoveryReport report =
        DriveUntilRecovered(clientWorld, serverWorld, clientSync, dt, tick, kRecoveryBudgetTicks);
    EXPECT_TRUE(report.ticksUntilRecovered != 0);
    tick = report.nextTick;
    const uint64_t snapshotTickAtRecovery = clientSync.LastSnapshotTick();

    // 복구 뒤 다시 움직인다. 이번엔 주입이 없으므로 끝까지 붙어 있어야 한다.
    clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveForward });
    constexpr uint64_t kPostRecoveryMoveTicks = 12;
    for (uint64_t i = 0; i < kPostRecoveryMoveTicks; ++i, ++tick) {
        StepLockstepPair(clientWorld, serverWorld, dt, tick);
        // 움직이는 동안에는 클라가 한 tick 뒤처져 해시가 다를 수 있다.
        // 그래서 여기서 보는 것은 raw 해시가 아니라 tick을 맞춰 비교하는 불일치 플래그다.
        EXPECT_FALSE(clientSync.HasHashMismatch());
    }

    // 다시 멈추고 따라잡히면 해시까지 같아야 한다.
    clientSync.QueueLocalInput(EF::InputCommand{});
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 10);

    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());

    // 복구 이후로는 스냅샷이 더 오지 않았어야 한다 — 왔다면 조용히 resync가 반복되고
    // 있다는 뜻이고, 그건 "복구됐다"가 아니라 "계속 덮어쓰는 중"이다.
    EXPECT_EQ(clientSync.LastSnapshotTick(), snapshotTickAtRecovery);
}

// (4) 대조군 — 주입하지 않으면 같은 각본에서 불일치가 뜨지 않는다.
//
// 위 세 테스트가 본 불일치가 **주입 때문**이지 이 파일의 배선(월드 구성, tick 순서,
// 입력 타이밍) 때문이 아님을 못 박는다. 이게 없으면 "원래 디싱크나는 배선"으로도
// (1)~(3)이 전부 통과한다.
TEST_CASE(Network, NoInjectionKeepsCleanSessionFreeOfHashMismatch) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);

    // (1)~(3)과 완전히 같은 각본. 다른 것은 DebugSkipNextConfirmedInputFrameOnce()를
    // 부르지 않는다는 것 하나뿐이다.
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
    EXPECT_FALSE(clientSync.HasHashMismatch());

    const uint64_t snapshotTickAfterJoin = clientSync.LastSnapshotTick();

    clientSync.QueueLocalInput(EF::InputCommand{});
    constexpr uint64_t kCleanRunTicks = 40;
    for (uint64_t i = 0; i < kCleanRunTicks; ++i, ++tick) {
        StepLockstepPair(clientWorld, serverWorld, dt, tick);
        EXPECT_FALSE(clientSync.HasHashMismatch());
    }

    // 조인 이후 스냅샷이 한 번도 더 오지 않았어야 한다 = resync가 돈 적이 없다.
    EXPECT_EQ(clientSync.LastSnapshotTick(), snapshotTickAfterJoin);
    EXPECT_EQ(clientWorld.ComputeHash(), serverWorld.ComputeHash());
}

// 월드가 **계속 바뀌는 동안에도** resync가 복구한다.
//
// 이 테스트가 이 파일에서 제일 값어치 있다. 예전 코드는 여기서 실패했다 —
// 서버가 만든 스냅샷의 tick 라벨이 하나 앞서 있어서, 클라가 같은 tick의 InputFrame을
// "이미 지난 것"으로 버렸다. 즉 **resync 할 때마다 확정 입력을 정확히 한 프레임 삼켰다.**
// 월드가 정지해 있으면 삼킨 프레임이 no-op이라 우연히 복구됐고, 그래서 기존 테스트가
// 전부 통과하면서 결함이 숨어 있었다. 계속 움직이면 200 tick / 스냅샷 200개를 받고도
// 서버보다 2유닛 뒤처진 채 무한 재동기화를 돌았다.
//
// 그래서 이 테스트는 **입력을 계속 넣으면서** 복구를 요구한다.
TEST_CASE(Network, DesyncRecoversWhileWorldKeepsMoving) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);
    EXPECT_TRUE(clientId != 0);

    // 디싱크를 주입한다.
    clientSync.DebugSkipNextConfirmedInputFrameOnce();
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
    EXPECT_TRUE(clientSync.HasHashMismatch());

    // 이제 **멈추지 않고** 계속 입력을 넣으면서 돌린다.
    // 매 tick 새 입력을 큐에 넣어 서버 월드가 계속 변하게 만드는 것이 핵심이다.
    const uint64_t kBudgetTicks = 60;
    bool recovered = false;
    uint64_t usedTicks = 0;
    for (uint64_t i = 0; i < kBudgetTicks; ++i) {
        clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
        tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
        ++usedTicks;

        // 클라는 항상 최소 1 tick 뒤처지므로 "지금 해시가 같은가"로는 판정할 수 없다.
        // 불일치 플래그가 내려가고 새 스냅샷을 받았는지로 본다.
        if (!clientSync.HasHashMismatch()) {
            recovered = true;
            break;
        }
    }

    EXPECT_TRUE(recovered);
    EXPECT_TRUE(usedTicks < kBudgetTicks);

    // 복구 뒤에도 계속 움직이면서 일치를 유지해야 한다.
    // (한 번 맞았다가 다음 tick에 다시 깨지면 복구라고 할 수 없다.)
    for (uint64_t i = 0; i < 20; ++i) {
        clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
        tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
        EXPECT_FALSE(clientSync.HasHashMismatch());
    }

    // 그리고 실제로 움직이고 있었는지 확인한다. 아무도 안 움직였으면
    // "정지한 월드에서 복구했다"는 예전과 같은 상황이라 이 테스트가 공허해진다.
    const PlayerProbe finalProbe = ProbePlayer(clientWorld, clientId);
    EXPECT_TRUE(finalProbe.found);
    EXPECT_TRUE(finalProbe.position.x > EF::Fixed64::FromInt(5));
}

// 디싱크 하나에 ResyncRequest 가 폭주하지 않는다.
//
// 디싱크가 나면 그 뒤 tick들도 줄줄이 어긋난다. 불일치하는 pending tick 마다 요청하면
// 서버는 요청마다 **월드 전체 스냅샷**을 reliable로 답한다. 엔티티가 수만 개가 되는
// 공장 게임에서는 그게 곧 대역폭 붕괴다.
TEST_CASE(Network, ResyncRequestDoesNotStormOnSingleDesync) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);

    const uint64_t snapshotsBefore = clientSync.LastSnapshotTick();
    static_cast<void>(snapshotsBefore);

    clientSync.DebugSkipNextConfirmedInputFrameOnce();
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
    EXPECT_TRUE(clientSync.HasHashMismatch());

    // ⚠️ LastSnapshotTick 의 변화로 세면 안 된다. 같은 서버 tick 에 만들어진 스냅샷
    //    여러 개가 같은 라벨을 달고 오므로 전부 한 건으로 뭉개져서, 정작 재려는
    //    "중복 요청"이 보이지 않는다. 실제 적용 횟수를 센다.
    const uint64_t appliedBefore = clientSync.AppliedSnapshotCount();
    for (uint64_t i = 0; i < 40; ++i) {
        clientSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
        tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 1);
    }
    const uint64_t appliedSnapshots = clientSync.AppliedSnapshotCount() - appliedBefore;

    // 한 번의 디싱크에 스냅샷 하나면 충분하다. 여유를 조금 두되(경합으로 하나 더 올 수 있다)
    // 예전처럼 매 tick 하나씩 오는 상태는 확실히 잡는다.
    EXPECT_TRUE(appliedSnapshots >= 1);
    EXPECT_TRUE(appliedSnapshots <= 3);
}

// 조용한 클라이언트를 쫓아내지 않는다.
//
// ⚠️ 이 테스트가 존재하는 이유가 있다. 처음에 생존 판정을 **앱 패킷이 올라오는 자리**
//    (TryReceive)에 뒀더니, 키를 안 누르는 클라이언트가 250 tick 만에 쫓겨났다.
//    가만히 서 있으면 InputCommand 를 안 보내기 때문이다. 그래도 서버가 매 tick 보내는
//    reliable InputFrame 에 ack 는 계속 보내는데, **ack 만 든 데이터그램에는 앱 레코드가
//    없어서 TryReceive 로 올라오지 않는다.** 생존은 전송 계층에서 재야 한다.
TEST_CASE(Network, IdleClientIsNotTimedOut) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);
    EXPECT_TRUE(clientId != 0);

    // 입력을 **한 번도 넣지 않고** 타임아웃 길이(250)를 훌쩍 넘겨 돌린다.
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 400);

    // 여전히 등록되어 있어야 하고, 플레이어 엔티티도 살아 있어야 한다.
    EXPECT_TRUE(serverWorld.GetResource<EF::NetworkManager>().FindClient(clientId) != nullptr);
    const PlayerProbe probe = ProbePlayer(serverWorld, clientId);
    EXPECT_TRUE(probe.found);
    EXPECT_FALSE(clientSync.HasHashMismatch());
}

// 정말로 사라진 클라이언트는 정리한다 — 등록, 신뢰성 채널, 플레이어 엔티티까지.
//
// 정리하지 않으면 두 가지가 샌다. 서버는 매 tick 등록된 전원에게 reliable InputFrame 을
// 보내므로 떠난 클라의 채널에 미확인 레코드가 무한히 쌓이고(메모리·재전송 트래픽),
// 조종자 없는 플레이어 엔티티가 월드에 영원히 남는다.
TEST_CASE(Network, VanishedClientIsCleanedUpAfterTimeout) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);
    EXPECT_TRUE(clientId != 0);
    EXPECT_TRUE(ProbePlayer(serverWorld, clientId).found);

    // 클라이언트 월드를 더 이상 돌리지 않는다 = 프로세스가 죽은 것과 같다.
    // 서버만 타임아웃 길이를 넘겨 돌린다.
    for (uint64_t i = 0; i < 300; ++i) {
        serverWorld.FixedUpdate(dt, tick + i);
    }

    EXPECT_TRUE(serverWorld.GetResource<EF::NetworkManager>().FindClient(clientId) == nullptr);
    EXPECT_FALSE(ProbePlayer(serverWorld, clientId).found);
}

// ─────────────────────────────────────────────────────────────────────────────
// N2 — 스폰·디스폰이 확정 입력 경로에서만 일어난다
//
// lockstep에서 스폰은 원래 복제 대상이 아니다. 모두가 같은 확정 입력을 같은 tick에
// 적용하면 각자 알아서 똑같은 엔티티를 만든다. 문제는 "누가 접속했다"가 **입력이
// 아니라는** 것이었다 — 도착 시각이 peer마다 다르고, 클라는 남의 접속을 스스로 모른다.
//
// 예전에는 서버가 Hello를 받는 자리에서 곧장 CreateEntity를 불렀다. 그 순간 서버의
// EntityManager가 클라들과 갈라지고, 이후 같은 확정 입력으로 스폰해도 서로 다른
// EntityId가 나온다. 수습은 resync가 했다 — 즉 **접속 한 번에 남은 전원이 스냅샷을
// 한 번씩 받았다.** 지금은 서버가 그 사건을 tick 하나에 못 박아 확정 프레임에 실어 보낸다.
// ─────────────────────────────────────────────────────────────────────────────

// 두 번째 클라가 **실제로 접속해도** 첫 번째 클라가 resync를 받지 않는다.
//
// 이게 N2의 핵심 성과다. 예전에는 서버가 Hello 자리에서 곧장 CreateEntity를 불렀고,
// 그 순간 서버의 EntityManager가 기존 클라들과 갈라져서 스냅샷으로 덮어야 했다.
// 즉 **접속 한 번에 남은 전원이 스냅샷을 한 번씩 받았다.**
//
// ⚠️ 반드시 진짜 Hello 경로를 타야 한다. 서버에 QueueSessionEvent를 직접 부르면
//    검증하려는 코드(Hello 처리)를 건너뛰어서 테스트가 공허해진다 — 실제로 처음에
//    그렇게 썼다가, 옛 동작으로 되돌려도 통과하는 것을 보고 다시 썼다.
TEST_CASE(Network, SecondClientJoinDoesNotResyncTheFirst) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260824);   // 무손실. 재전송이 결과를 흐리지 않게.
    const Endpoint serverEp = Endpoint::Memory(910);
    const Endpoint clientOneEp = Endpoint::Memory(911);
    const Endpoint clientTwoEp = Endpoint::Memory(912);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);

    const EF::Fixed64 dt = FixedTickDelta();
    uint64_t tick = 1;

    // 1) 첫 클라만 붙여서 안정시킨다.
    for (; tick <= 40; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }
    EXPECT_TRUE(clientOneSync.HasSnapshot());
    const uint32_t firstId = clientOneWorld.GetResource<EF::NetworkManager>().LocalClientId();
    EXPECT_TRUE(firstId != 0);

    const uint64_t snapshotsBefore = clientOneSync.AppliedSnapshotCount();

    // 2) 두 번째 클라가 붙는다. 첫 클라는 계속 움직인다.
    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);

    for (; tick <= 140; ++tick) {
        clientOneSync.QueueLocalInput(EF::InputCommand{ .buttonMask = EF::kInputMoveRight });
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);

        // 접속이 진행되는 내내 첫 클라는 한 번도 흔들리면 안 된다.
        EXPECT_FALSE(clientOneSync.HasHashMismatch());
    }

    EXPECT_TRUE(clientTwoSync.HasSnapshot());
    const uint32_t secondId = clientTwoWorld.GetResource<EF::NetworkManager>().LocalClientId();
    EXPECT_TRUE(secondId != 0 && secondId != firstId);

    // 3) 첫 클라에게 스냅샷이 한 장도 추가로 오지 않았어야 한다. **이게 이 테스트의 요점이다.**
    EXPECT_EQ(clientOneSync.AppliedSnapshotCount(), snapshotsBefore);

    // 4) 그런데도 첫 클라의 월드에는 두 번째 플레이어가 있다 — 자기가 만든 것이다.
    EXPECT_TRUE(ProbePlayer(clientOneWorld, secondId).found);
    EXPECT_TRUE(ProbePlayer(serverWorld, secondId).found);
}

// 서버와 클라가 **각자 만든** 엔티티의 EntityId 까지 같다.
//
// 이게 성립해야 "복제하지 않고 각자 만든다"가 참이 된다. index만 같고 version이 다르면
// IsAlive 판정이 갈라지고, EntityId 로 참조하는 코드가 생기는 순간 무너진다.
//
// ⚠️ 이 테스트는 **옛 동작(확정 경로 밖 즉시 스폰)에서도 통과한다** — 확인해 봤다.
//    스냅샷이 EntityManager 상태를 나르므로(N1) resync가 결국 id까지 맞춰 주기 때문이다.
//    그러니 이건 N2 회귀 탐지기가 아니라 **다른 종류의 파손을 막는 가드**다:
//    스폰 위치를 clientId가 아닌 것에서 유도하거나, 적용 순서를 뒤집거나,
//    EntityManager 복원이 깨지면 여기서 걸린다.
//    N2 자체를 지키는 것은 위의 SecondClientJoinDoesNotResyncTheFirst 다.
TEST_CASE(Network, IndependentlySpawnedEntitiesGetIdenticalEntityIds) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260825);
    const Endpoint serverEp = Endpoint::Memory(920);
    const Endpoint clientOneEp = Endpoint::Memory(921);
    const Endpoint clientTwoEp = Endpoint::Memory(922);

    auto serverHost = net.CreateHost(serverEp);
    auto clientOneHost = net.CreateHost(clientOneEp, serverEp);
    auto clientTwoHost = net.CreateHost(clientTwoEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);

    const EF::Fixed64 dt = FixedTickDelta();
    uint64_t tick = 1;
    for (; tick <= 40; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    EF::World clientTwoWorld;
    AttachClientWorld(clientTwoWorld, clientTwoHost);
    for (; tick <= 140; ++tick) {
        clientOneWorld.FixedUpdate(dt, tick);
        clientTwoWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    const uint32_t secondId = clientTwoWorld.GetResource<EF::NetworkManager>().LocalClientId();
    const EF::EntityId serverEntity = EF::FindPlayerEntityFor(serverWorld, secondId);
    const EF::EntityId firstClientEntity = EF::FindPlayerEntityFor(clientOneWorld, secondId);

    EXPECT_TRUE(serverEntity != EF::kNullEntity);
    EXPECT_EQ(firstClientEntity, serverEntity);   // index와 version 둘 다
    static_cast<void>(clientOneSync);
}

// 확정된 Leave 는 양쪽에서 같은 엔티티를 지운다.
TEST_CASE(Network, ConfirmedLeaveRemovesPlayerOnBothSides) {
    EF::World serverWorld;
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = WireLockstepPair(serverWorld, clientWorld);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t firstId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, firstId);

    EF::NetworkManager& serverNet = serverWorld.GetResource<EF::NetworkManager>();
    const uint32_t secondId = serverNet.AllocateClientId();
    serverNet.QueueSessionEvent(tick + 1, EF::SessionEvent{
        .kind = EF::SessionEvent::Kind::Join, .clientId = secondId });
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 20);
    EXPECT_TRUE(ProbePlayer(clientWorld, secondId).found);

    const uint64_t snapshotsBefore = clientSync.AppliedSnapshotCount();
    serverNet.QueueSessionEvent(tick + 1, EF::SessionEvent{
        .kind = EF::SessionEvent::Kind::Leave, .clientId = secondId });
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 20);

    EXPECT_FALSE(ProbePlayer(serverWorld, secondId).found);
    EXPECT_FALSE(ProbePlayer(clientWorld, secondId).found);
    EXPECT_FALSE(clientSync.HasHashMismatch());
    EXPECT_EQ(clientSync.AppliedSnapshotCount(), snapshotsBefore);   // 여기도 resync 없이
}

// 같은 Join 이벤트가 두 번 적용되어도 엔티티가 둘이 되지 않는다.
//
// 실제로 이런 경로가 있다: 스냅샷에 이미 담겨 온 뒤 같은 tick의 프레임을 또 받는 경우.
// 엔티티가 둘이 되면 peer마다 다르게 나타나지 않아서 **해시로도 안 잡힌다** —
// 조용히 틀린 채로 굴러가는 종류라 여기서 막는다.
TEST_CASE(Network, DuplicateJoinEventDoesNotSpawnTwice) {
    EF::World world;
    world.RegisterSnapshotComponent<EF::Transform>();
    world.RegisterSnapshotComponent<EF::PlayerControl>();

    const EF::SessionEvent join{ .kind = EF::SessionEvent::Kind::Join, .clientId = 7 };
    EF::ApplySessionEvents(world, std::span<const EF::SessionEvent>(&join, 1));
    const uint64_t afterFirst = world.ComputeHash();

    EF::ApplySessionEvents(world, std::span<const EF::SessionEvent>(&join, 1));
    EXPECT_EQ(world.ComputeHash(), afterFirst);

    int count = 0;
    for (auto [entity, control] : world.CreateView<EF::PlayerControl>()) {
        static_cast<void>(entity);
        if (control.clientId == 7) { ++count; }
    }
    EXPECT_EQ(count, 1);
}

// 오래 떠 있던 서버에도 접속할 수 있다.
//
// ⚠️ 실제로 낸 버그다. RegisterClient 가 lastHeardFromTick 을 안 찍어서 0으로 남았고,
//    같은 tick에 도는 DropTimedOutClients 가 `tick >= 0 + 타임아웃` 을 만족시켜
//    **서버 가동 5초 뒤부터 신규 접속이 전부 즉시 쫓겨났다.**
//    실행 테스트를 매번 새 서버로 해서 못 봤고, 다른 벤더 모델의 교차검증에서 잡혔다.
//    타임아웃(250 tick)보다 훨씬 뒤에서 접속을 시작하는 것이 이 테스트의 요점이다.
TEST_CASE(Network, ClientCanJoinLongRunningServer) {
    using EF::Network::Endpoint;

    EF::Network::SimulatedNetwork net(20260826);
    const Endpoint serverEp = Endpoint::Memory(930);
    const Endpoint clientEp = Endpoint::Memory(931);

    auto serverHost = net.CreateHost(serverEp);
    auto clientHost = net.CreateHost(clientEp, serverEp);

    EF::World serverWorld = CreateLockstepServerWorld();
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    const EF::Fixed64 dt = FixedTickDelta();
    uint64_t tick = 1;

    // 아무도 없는 상태로 타임아웃 길이를 훌쩍 넘겨 돌린다(= 오래 떠 있던 서버).
    for (; tick <= 400; ++tick) {
        serverWorld.FixedUpdate(dt, tick);
    }

    // 이제 접속한다.
    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = AttachClientWorld(clientWorld, clientHost);
    for (; tick <= 500; ++tick) {
        clientWorld.FixedUpdate(dt, tick);
        serverWorld.FixedUpdate(dt, tick);
    }

    EXPECT_TRUE(clientSync.HasSnapshot());
    const uint32_t clientId = clientWorld.GetResource<EF::NetworkManager>().LocalClientId();
    EXPECT_TRUE(clientId != 0);
    EXPECT_TRUE(serverWorld.GetResource<EF::NetworkManager>().FindClient(clientId) != nullptr);
    EXPECT_TRUE(ProbePlayer(serverWorld, clientId).found);
    EXPECT_TRUE(ProbePlayer(clientWorld, clientId).found);
}

// N4.2 — 서버와 클라가 tick별 해시 분해표를 기록하고, 같은 tick의 표는 풀 단위까지 일치한다.
//
// 부분 resync는 "클라가 tick T에 본 분해표"와 "서버가 기록해 둔 tick T의 분해표"를 비교해
// 어긋난 풀을 고른다. 그러려면 정상 세션에서 두 기록이 **같은 tick 의미**로 남아야 한다 —
// 서버는 tick의 입력을 적용한 직후, 클라는 그 tick의 확정 프레임을 적용한 직후.
// 한쪽이라도 한 칸 밀려 기록하면 정상 세션에서도 모든 풀이 "다름"으로 보인다.
TEST_CASE(Network, ClientAndServerRecordMatchingHashBreakdownPerTick) {
    auto [clientConnection, serverConnection] = EF::Network::MemoryConnection::CreatePair();

    EF::World serverWorld;
    serverWorld.RegisterSnapshotComponent<EF::Transform>();
    serverWorld.RegisterSnapshotComponent<EF::PlayerControl>();
    serverWorld.AddResource<EF::NetworkManager>(serverConnection);
    EF::NetworkSyncSystem& serverSync = serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientWorld;
    EF::NetworkSyncSystem& clientSync = AttachClientWorld(clientWorld, clientConnection);

    const EF::Fixed64 dt = FixedTickDelta();
    uint32_t clientId = 0;
    uint64_t tick = DriveToMovingLockstep(clientWorld, serverWorld, clientSync, dt, clientId);
    tick = AdvanceLockstepPair(clientWorld, serverWorld, dt, tick, 10);

    EXPECT_TRUE(clientSync.HasSnapshot());
    EXPECT_FALSE(clientSync.HasHashMismatch());

    // 클라가 마지막으로 시뮬레이션한 tick과 그 직전 몇 tick을 비교한다(이동 중이라 매 tick 값이 다르다).
    const uint64_t lastTick = clientSync.ClientSimTick();
    for (uint64_t t = lastTick - 3; t <= lastTick; ++t) {
        const EF::WorldHashBreakdown* client = clientSync.HashBreakdownAt(t);
        const EF::WorldHashBreakdown* server = serverSync.HashBreakdownAt(t);
        EXPECT_TRUE(client != nullptr);
        EXPECT_TRUE(server != nullptr);
        if (client == nullptr || server == nullptr) {
            continue;
        }

        EXPECT_EQ(client->entityHash, server->entityHash);
        EXPECT_EQ(client->worldHash, server->worldHash);
        EXPECT_EQ(client->pools.size(), server->pools.size());
        EXPECT_TRUE(!client->pools.empty());
        for (std::size_t i = 0; i < client->pools.size() && i < server->pools.size(); ++i) {
            EXPECT_EQ(client->pools[i].type, server->pools[i].type);
            EXPECT_EQ(client->pools[i].hash, server->pools[i].hash);
        }
    }

    // 기록하지 않은 tick(아직 오지 않은 미래)은 nullptr이다.
    EXPECT_TRUE(clientSync.HashBreakdownAt(lastTick + 1000) == nullptr);
}
