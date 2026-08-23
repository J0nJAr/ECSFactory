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
    EXPECT_EQ(clientSync.LastSnapshotTick(), static_cast<uint64_t>(2));
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

    EXPECT_TRUE(serverNetwork.SendInputFrame(EF::InputFrame{ .tick = command.tick, .inputs = confirmed }));
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

    EXPECT_TRUE(clientNetwork.SendResyncRequest(50, clientHash, serverHash));
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
