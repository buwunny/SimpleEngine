#ifndef SERVER_GAME_SERVER_HPP
#define SERVER_GAME_SERVER_HPP

#include "core/Scene.hpp"
#include "core/PhysicsWorld.hpp"
#include "script/ScriptHost.hpp"
#include "ecs/Entity.hpp"
#include "net/Protocol.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class Camera;

// The authoritative game simulation, headless. It reuses the exact engine
// pieces the client runs — Scene, Bullet physics, and the .cow scripts — so the
// movement the server computes is identical to what a client predicts. One
// GameServer owns one room/scene and a set of connected sessions, each mapped to
// a player entity. Transport is external: the owner wires setSend() and feeds
// onConnect/onDisconnect/onMessage, then calls tick() at a fixed rate.
class GameServer
{
public:
    // Emits an outgoing message addressed to a session. The transport layer
    // encodes it and routes it on the channel channelFor() selects.
    using SendFn = std::function<void(uint32_t session, const net::Message &)>;

    GameServer();
    ~GameServer();

    bool init(const std::string &scenePath);
    void setSend(SendFn fn) { send_ = std::move(fn); }

    // Session lifecycle, driven by the transport/sidecar.
    void onConnect(uint32_t session);
    void onDisconnect(uint32_t session);
    void onMessage(uint32_t session, const net::Message &msg);

    // Advance the simulation one fixed step and broadcast a snapshot.
    void tick(float dt);

    uint32_t serverTick() const { return serverTick_; }
    size_t playerCount() const { return sessions_.size(); }

private:
    struct Session
    {
        ecs::Entity entity = ecs::NullEntity;
        uint32_t netId = 0;
        std::string name; // sanitised display name, broadcast in PlayerJoin
        Camera *camera = nullptr;
        net::InputCommand lastInput;
        uint32_t lastSeq = 0;
        double lastActive = 0.0; // server time of the last message from this session
        bool spawned = false;
    };

    void spawnPlayer(Session &s);
    void despawnPlayer(Session &s);
    net::Snapshot buildSnapshot(uint32_t ackSeq) const;
    // Assign NetIds to newly script-spawned bodies and broadcast SpawnEntity so
    // clients build a visual for each. Called once per tick after scripts run.
    void detectAndAnnounceSpawns();
    // EnTT on_destroy<NetId> observer: records the netId of any replicated entity
    // a script (e.g. shoot_cow) destroys, so tick() can broadcast DespawnEntity.
    void onNetIdDestroyed(ecs::Registry &reg, ecs::Entity e);
    // Broadcast DespawnEntity for entities destroyed since the last tick and drop
    // them from the late-join replay list.
    void flushDespawns();
    // Broadcast every blast that went off this tick, so each client can apply the
    // same knockback to the player it predicts locally instead of discovering it
    // several snapshots later as a position error.
    void flushExplosions();

    Scene scene_;
    PhysicsWorld physics_;
    ScriptHost host_;
    SendFn send_;

    std::unordered_map<uint32_t, Session> sessions_; // keyed by session id
    uint32_t nextPlayerIdx_ = 0;                      // -> net::kPlayerNetIdBase + idx
    uint32_t nextSpawnNetId_ = 0;                     // -> net::kSpawnNetIdBase + counter
    std::vector<net::SpawnEntity> spawnedObjects_;    // for late-join replay
    std::vector<uint32_t> pendingDespawns_;           // netIds destroyed this tick
    std::vector<net::Explosion> pendingExplosions_;   // blasts fired this tick
    void sweepIdleSessions();

    uint32_t serverTick_ = 0;
    double scriptTime_ = 0.0;
    double serverTime_ = 0.0;  // wall-clock sim time, for idle timeouts
    uint16_t tickRate_ = 60;
    uint16_t snapshotHz_ = 20;  // snapshots sent at this rate (physics still tickRate_); clients interpolate
    uint16_t maxPlayers_ = 16; // room capacity; further joins are refused
    double idleTimeout_ = 10.0; // despawn a session silent for this long (missed disconnect)
    bool ready_ = false;
};

#endif // SERVER_GAME_SERVER_HPP
