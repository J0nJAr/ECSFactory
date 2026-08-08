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
        world.AddResource<EF::NetworkManager>(std::move(connection));
        return world.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Client);
    }

    EF::Fixed64 FixedTickDelta() {
        return EF::Fixed64::FromRaw(EF::Fixed64::Scale / 50);
    }

    EF::World CreateLockstepPlayerWorld(uint32_t clientId) {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::PlayerControl>();

        const EF::EntityId entity = world.CreateEntity();
        world.AddComponent<EF::Transform>(entity, EF::Transform{
            .position = EF::Vector3(0, 0, 0),
            .rotation = EF::Quaternion::Identity()
        });
        world.AddComponent<EF::PlayerControl>(entity, EF::PlayerControl{ .clientId = clientId });
        return world;
    }

    EF::World CreateLockstepPlayersWorld(uint32_t count) {
        EF::World world;
        world.RegisterSnapshotComponent<EF::Transform>();
        world.RegisterSnapshotComponent<EF::PlayerControl>();

        for (uint32_t clientId = 1; clientId <= count; ++clientId) {
            const EF::EntityId entity = world.CreateEntity();
            world.AddComponent<EF::Transform>(entity, EF::Transform{
                .position = EF::Vector3(0, 0, 0),
                .rotation = EF::Quaternion::Identity()
            });
            world.AddComponent<EF::PlayerControl>(entity, EF::PlayerControl{ .clientId = clientId });
        }
        return world;
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

    EF::World serverWorld = CreateLockstepPlayerWorld(1);
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

    EF::World serverWorld = CreateLockstepPlayerWorld(1);
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

    auto applyMutated = [&valid](auto&& mutate) {
        EF::WorldSnapshot snapshot = valid;
        mutate(snapshot);

        EF::World clientWorld;
        clientWorld.RegisterSnapshotComponent<EF::Transform>();
        return clientWorld.ApplySnapshot(snapshot);
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

    EF::World serverWorld = CreateLockstepPlayersWorld(2);
    serverWorld.AddResource<EF::NetworkManager>(serverHost);
    serverWorld.RegisterSystem<EF::NetworkSyncSystem>(EF::NetworkSyncMode::Server);

    EF::World clientOneWorld;
    EF::NetworkSyncSystem& clientOneSync = AttachClientWorld(clientOneWorld, clientOneHost);
    clientOneWorld.RegisterSnapshotComponent<EF::PlayerControl>();

    EF::World clientTwoWorld;
    EF::NetworkSyncSystem& clientTwoSync = AttachClientWorld(clientTwoWorld, clientTwoHost);
    clientTwoWorld.RegisterSnapshotComponent<EF::PlayerControl>();

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
