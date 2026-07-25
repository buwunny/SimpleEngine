// Repro harness for "reset pressure plate breaks after a couple of presses"
// in multiplayer. Drives GameServer through several scene resets in a row
// (via requestSceneReset(), a test-only hook -- no need to physically walk a
// player onto the plate) and watches the actual wire traffic: SpawnEntity,
// DespawnEntity, and Snapshot entity counts, with two concurrent sessions
// since every Spawn/Despawn is multicast once per connected session.

#include "net/Protocol.hpp"
#include "server/GameServer.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace net;

static int failures = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while(0)

int main()
{
    struct Outbox
    {
        int spawns = 0;
        int despawns = 0;
        std::map<uint32_t, std::set<uint32_t>> liveNetIdsPerSession;
        size_t lastSnapshotEntities = 0;
    } out;

    GameServer server;
    server.setSend([&](uint32_t session, const Message &m)
    {
        auto &live = out.liveNetIdsPerSession[session];
        if (const auto *s = std::get_if<SpawnEntity>(&m))
        {
            out.spawns++;
            auto [it, inserted] = live.insert(s->netId);
            if (!inserted)
                printf("  ! session %u got a duplicate SpawnEntity for netId=%u\n", session, s->netId);
        }
        else if (const auto *d = std::get_if<DespawnEntity>(&m))
        {
            out.despawns++;
            if (live.erase(d->netId) == 0)
                printf("  ! session %u got a DespawnEntity for netId=%u it never saw spawned\n", session, d->netId);
        }
        else if (const auto *snap = std::get_if<Snapshot>(&m))
        {
            out.lastSnapshotEntities = snap->entities.size();
        }
    });

    if (!server.init("scenes/scene.json"))
    {
        printf("FAIL: GameServer::init\n");
        return 1;
    }

    server.onConnect(1);
    ClientHello hello;
    hello.name = "Bessie";
    server.onMessage(1, hello);
    server.onConnect(2);
    ClientHello hello2;
    hello2.name = "Daisy";
    server.onMessage(2, hello2);
    CHECK(server.playerCount() == 2);

    const float dt = 1.0f / 60.0f;
    auto settle = [&](int ticks) { for (int i = 0; i < ticks; ++i) server.tick(dt); };

    settle(5); // let the world settle + get an initial snapshot
    size_t baseline = out.lastSnapshotEntities;
    printf("baseline snapshot entities: %zu\n", baseline);
    CHECK(baseline > 0);

    for (int reset = 1; reset <= 6; ++reset)
    {
        server.requestSceneReset();
        settle(10); // enough ticks for the reset tick + a snapshot after it

        printf("reset #%d: spawns=%d despawns=%d snapshotEntities=%zu\n",
               reset, out.spawns, out.despawns, out.lastSnapshotEntities);

        // The players are spared, so the snapshot entity count should return
        // to the same baseline every time -- not grow, not shrink to just the
        // players, not vanish.
        CHECK(out.lastSnapshotEntities == baseline);
    }

    // The runtime-spawn ids handed out to the reset scene's dynamic objects
    // should never repeat across sessions or resets (each session's tracked
    // set is fully drained by matching despawns above, so nothing lingers).
    for (auto &[session, live] : out.liveNetIdsPerSession)
        CHECK(live.size() == baseline - 2); // minus the two player entities

    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
