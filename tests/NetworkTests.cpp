#include "TestHarness.hpp"

#include <array>
#include <cstdint>
#include <vector>

#include "Components/Transform.hpp"
#include "Components/Velocity.hpp"
#include "ECS/World.hpp"
#include "Network/MemoryConnection.hpp"
#include "Network/NetworkManager.hpp"

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

    EXPECT_TRUE(clientNetwork.SendHello(1));
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

TEST_CASE(Network, ServerHandlesMultipleClientConnectionsIndependently) {
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

    EXPECT_TRUE(clientOneNetwork.SendInputCommand(EF::InputCommand{ .clientId = 1, .tick = 10, .sequence = 1, .buttonMask = 1 }));
    EXPECT_TRUE(clientTwoNetwork.SendInputCommand(EF::InputCommand{ .clientId = 2, .tick = 10, .sequence = 1, .buttonMask = 2 }));

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

    const EF::WorldSnapshot snapshot = serverWorld.CreateSnapshot(10);
    EXPECT_TRUE(serverClientOneNetwork.SendWorldSnapshot(snapshot));
    EXPECT_TRUE(serverClientTwoNetwork.SendWorldSnapshot(snapshot));

    EXPECT_TRUE(ReceiveSnapshotInto(clientOneNetwork, clientOneWorld));
    EXPECT_TRUE(ReceiveSnapshotInto(clientTwoNetwork, clientTwoWorld));
    EXPECT_EQ(clientOneWorld.ComputeHash(), serverWorld.ComputeHash());
    EXPECT_EQ(clientTwoWorld.ComputeHash(), serverWorld.ComputeHash());
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

    EXPECT_TRUE(clientNetwork.SendHello(0));
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
