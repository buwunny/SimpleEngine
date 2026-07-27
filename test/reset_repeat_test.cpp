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
        std::set<uint32_t> lastSnapshotNetIds;
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
            for (const auto &e : snap->entities)
                out.lastSnapshotNetIds.insert(e.netId);
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

    // The scene ids in play before any reset. These are what a client loading
    // this scene for the first time derives locally, so they are the contract
    // every later reset has to keep.
    std::vector<uint32_t> sceneIdsAtInit;
    for (uint32_t id : out.lastSnapshotNetIds)
        if (id < kPlayerNetIdBase)
            sceneIdsAtInit.push_back(id);
    printf("scene netIds at init: %zu\n", sceneIdsAtInit.size());
    CHECK(!sceneIdsAtInit.empty());

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

    // A reset must NOT re-announce the scene's own objects.
    //
    // This used to assert the opposite -- that every reset handed the scene's
    // dynamic bodies fresh runtime-spawn ids -- which looked healthy because the
    // spawns and despawns balanced. They were balanced and wrong: renumbering
    // the scene into the spawn id range breaks the one thing those ids exist to
    // guarantee, that a client can derive them by loading the same file. A
    // client reloading after a reset then claimed the ids it derived locally,
    // received nothing for them, and rendered them as inert ghosts beside the
    // live spawn-range copies. See assignSceneNetIds().
    CHECK(out.spawns == 0);
    for (auto &[session, live] : out.liveNetIdsPerSession)
        CHECK(live.empty());

    // Nothing was spawned, so nothing should have been despawned either: the
    // scene bodies are rebuilt under their original ids and the client's
    // representation of each stays valid across the reset.
    CHECK(out.despawns == 0);

    // ---- the reported repro: reset the scene, then RELOAD the page ---------
    //
    // A reload is a brand-new client process, so it derives the scene's netIds
    // from a fresh entity counter -- it has no idea the world has been reset six
    // times. Whatever the server replicates after a reset therefore has to match
    // what a first-time loader would compute, which is the ids captured at init.
    //
    // The ghost duplicates came from exactly this gap: the fresh client claimed
    // 2..5 and got nothing, while the spawn replay built live copies alongside.
    {
        const std::set<uint32_t> derivedByAFreshClient(sceneIdsAtInit.begin(),
                                                       sceneIdsAtInit.end());

        out.lastSnapshotNetIds.clear();
        server.onConnect(3);
        ClientHello hello3;
        hello3.name = "Latecomer";
        server.onMessage(3, hello3);
        settle(10);

        // Every scene id the fresh client derives must be one the server is
        // actually replicating, or that object is an inert ghost on its screen.
        for (uint32_t id : derivedByAFreshClient)
            CHECK(out.lastSnapshotNetIds.count(id) == 1);

        // And the reverse: nothing in the spawn range should exist for a scene
        // that never ran a spawning script, or the client builds a second copy
        // of an object it already has.
        size_t spawnRange = 0;
        for (uint32_t id : out.lastSnapshotNetIds)
            if (id >= net::kSpawnNetIdBase)
                ++spawnRange;
        CHECK(spawnRange == 0);

        printf("fresh client after %d resets: scene ids %zu/%zu present, %zu spawn-range\n",
               6, derivedByAFreshClient.size(), derivedByAFreshClient.size(), spawnRange);
    }

    if (failures == 0) printf("ALL PASS\n");
    else printf("%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
