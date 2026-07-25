// Repro harness: does NetClient stay correct across several scene resets in a
// row? A reset despawns the claimed scene object(s) and respawns replacements
// under new (spawn-range) netIds -- see GameServer::applyPendingReset. This
// drives that exact Despawn+Spawn cycle repeatedly against a real Scene +
// entt registry (not just message counts) to catch anything that leaks or,
// worse, silently starts controlling the wrong entity via a recycled id.

#include "core/Scene.hpp"
#include "core/PhysicsWorld.hpp"
#include "core/Camera.hpp"
#include "ecs/Components.hpp"
#include "net/NetClient.hpp"
#include "net/ITransport.hpp"
#include "net/Protocol.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdio>
#include <deque>
#include <vector>

using namespace net;

static int failures = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while(0)

struct MockTransport : ITransport
{
    std::deque<Incoming> inbox;
    std::vector<Message> sent;
    bool up = true;
    void sendUnreliable(const uint8_t *d, size_t n) override { rec(d, n); }
    void sendReliable(const uint8_t *d, size_t n) override { rec(d, n); }
    void rec(const uint8_t *d, size_t n) { if (auto m = decode(d, n)) sent.push_back(*m); }
    bool poll(Incoming &out) override
    {
        if (inbox.empty()) return false;
        out = std::move(inbox.front());
        inbox.pop_front();
        return true;
    }
    TransportState state() const override { return up ? TransportState::Connected : TransportState::Disconnected; }
    void inject(const Message &m)
    {
        Incoming i;
        i.channel = channelFor(typeOf(m));
        i.bytes = encode(m);
        inbox.push_back(std::move(i));
    }
};

static const float DT = 1.0f / 60.0f;

int main()
{
    PhysicsWorld physics;
    Scene scene;
    Camera cam(glm::vec3(0, 3, 10), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));

    ecs::Entity sceneCube = scene.spawnCube(1, glm::translate(glm::mat4(1.0f), glm::vec3(5, 1, 5)),
                                            glm::vec4(1, 0, 0, 1), 1.0f);
    uint32_t sceneCubeNet = scene.registry().get<ecs::Identity>(sceneCube).id;

    scene.addPlayer(&cam, glm::translate(glm::mat4(1.0f), glm::vec3(0, 3, 10)), nullptr, physics);
    ecs::Entity local = scene.getPlayerEntity();

    MockTransport mock;
    NetClient nc(&mock, &scene, local, "Moo Deng");
    CHECK(nc.replicatedCount() == 1); // the claimed scene cube

    mock.inject(ServerWelcome{kPlayerNetIdBase, 0, 60});
    nc.update(DT);

    uint32_t currentNetId = sceneCubeNet;
    ecs::Entity currentEntity = sceneCube;

    for (int reset = 1; reset <= 8; ++reset)
    {
        uint32_t newNetId = kSpawnNetIdBase + static_cast<uint32_t>(reset);

        // Mirrors GameServer::tick() ordering: SpawnEntity for the rebuilt
        // object is broadcast before the DespawnEntity for the one it replaces
        // (detectAndAnnounceSpawns runs before flushDespawns).
        mock.inject(SpawnEntity{newNetId, SpawnKind::Cube, glm::vec3(1.0f), glm::vec4(1, 0, 0, 1)});
        mock.inject(DespawnEntity{currentNetId});
        nc.update(DT);

        CHECK(nc.replicatedCount() == 1); // still exactly one cube tracked
        CHECK(!scene.registry().valid(currentEntity)); // old one really gone

        // Place it via a snapshot, like a real reset would a few ticks later.
        Snapshot s; s.serverTick = static_cast<uint32_t>(100 + reset); s.ackSeq = 1;
        EntityState es; es.netId = newNetId; es.pos = {5, 1, 5};
        s.entities = {es};
        mock.inject(s);
        nc.update(DT);

        // Count live entities carrying Identity in the whole registry, aside
        // from the local player -- should never exceed 1 (the current cube),
        // however many reset cycles have run.
        int identityCount = 0;
        ecs::Entity found = ecs::NullEntity;
        for (auto e : scene.registry().view<ecs::Identity>())
        {
            if (e == local) continue;
            ++identityCount;
            found = e;
        }
        printf("reset #%d: netId %u -> %u, replicatedCount=%zu, identityEntities=%d\n",
               reset, currentNetId, newNetId, nc.replicatedCount(), identityCount);
        CHECK(identityCount == 1); // no ghost left behind from the old entity/id

        currentNetId = newNetId;
        currentEntity = found;
    }

    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
