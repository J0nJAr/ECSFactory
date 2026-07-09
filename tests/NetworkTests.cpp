#include "TestHarness.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "ECS/World.hpp"
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

    const std::deque<EF::InputCommand>& pending = serverWorld.GetResource<EF::NetworkManager>().PendingInputs(1);
    EXPECT_EQ(pending.size(), static_cast<std::size_t>(0));

    const EF::NetworkClientState* serverClient = serverWorld.GetResource<EF::NetworkManager>().FindClient(1);
    EXPECT_TRUE(serverClient != nullptr);
    EXPECT_TRUE(serverClient->hasControlledEntity);
    EXPECT_TRUE(serverClient->controlledEntity != EF::kNullEntity);

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

    const std::deque<EF::InputCommand>& pending = serverNetwork.PendingInputs(1);
    EXPECT_EQ(pending.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(pending.front().clientId, static_cast<uint32_t>(1));
    EXPECT_EQ(pending.front().tick, static_cast<uint64_t>(4));
    EXPECT_EQ(pending.front().sequence, static_cast<uint32_t>(1));
    EXPECT_EQ(pending.front().buttonMask, static_cast<uint64_t>(0b101));
}

TEST_CASE(Network, ConsumePendingInputsReturnsAndRemovesInputsUpToTick) {
    EF::NetworkManager serverNetwork(EF::Network::MemoryConnection::CreatePair().second);

    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 1 }));
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 3, .sequence = 2 }));
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 2, .sequence = 3 }));

    std::vector<EF::InputCommand> consumed = serverNetwork.ConsumePendingInputs(5, 2);

    EXPECT_EQ(consumed.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(consumed[0].tick, static_cast<uint64_t>(1));
    EXPECT_EQ(consumed[1].tick, static_cast<uint64_t>(2));
    EXPECT_EQ(serverNetwork.PendingInputs(5).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(serverNetwork.PendingInputs(5).front().tick, static_cast<uint64_t>(3));
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

    serverWorld.FixedUpdate(dt, 4); // server sends TickHash
    clientWorld.FixedUpdate(dt, 5); // client detects mismatch and sends ResyncRequest
    EXPECT_TRUE(clientSync.HasHashMismatch());

    serverWorld.FixedUpdate(dt, 5); // server sends fresh snapshot
    clientWorld.FixedUpdate(dt, 6); // client applies snapshot

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
    EXPECT_EQ(serverNetwork.PendingInputs(7).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(serverNetwork.PendingInputs(7).front().buttonMask, command.buttonMask);

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

    EXPECT_EQ(serverClientOneNetwork.PendingInputs(1).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(serverClientTwoNetwork.PendingInputs(2).size(), static_cast<std::size_t>(1));
    EXPECT_EQ(serverClientOneNetwork.PendingInputs(1).front().buttonMask, uint64_t{ 1 });
    EXPECT_EQ(serverClientTwoNetwork.PendingInputs(2).front().buttonMask, uint64_t{ 2 });

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

TEST_CASE(Network, ServerDropsDuplicateAndStaleInputBySequence) {
    EF::NetworkManager serverNetwork(EF::Network::MemoryConnection::CreatePair().second);

    // 시퀀스가 증가하면 수락된다.
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 1 }));
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 2, .sequence = 2 }));

    // 같은 시퀀스(중복)와 과거 시퀀스(구식)는 폐기된다.
    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 2, .sequence = 2 }));
    EXPECT_FALSE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 1, .sequence = 1 }));

    // 다시 증가하면 수락된다.
    EXPECT_TRUE(serverNetwork.EnqueueInput(EF::InputCommand{ .clientId = 5, .tick = 3, .sequence = 3 }));

    EXPECT_EQ(serverNetwork.PendingInputs(5).size(), static_cast<std::size_t>(3));
    EXPECT_EQ(serverNetwork.PendingInputs(5).front().sequence, static_cast<uint32_t>(1));
    EXPECT_EQ(serverNetwork.PendingInputs(5).back().sequence, static_cast<uint32_t>(3));
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
    EXPECT_TRUE(serverNet.PendingInputs(idOne).size() >= static_cast<std::size_t>(1));
    EXPECT_EQ(serverNet.PendingInputs(idTwo).size(), static_cast<std::size_t>(0));
    EXPECT_EQ(serverNet.PendingInputs(idOne).front().buttonMask, static_cast<uint64_t>(0b111));
    EXPECT_EQ(serverNet.PendingInputs(idOne).front().clientId, idOne);
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
