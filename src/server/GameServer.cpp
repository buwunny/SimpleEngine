#include "server/GameServer.hpp"

#include "core/Camera.hpp"
#include "ecs/Components.hpp"
#include "ecs/Factories.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <btBulletDynamicsCommon.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace
{
    // Names arrive from a browser text box, are relayed verbatim to every other
    // player, and are drawn with an ASCII-only glyph atlas. So: keep printable
    // ASCII only (which drops control characters and anything the atlas has no
    // glyph for), collapse runs of whitespace, trim, and clamp the length.
    // Whatever is left of an empty or all-junk name falls back to "Cow <n>".
    std::string sanitizePlayerName(const std::string &raw, uint32_t netId)
    {
        std::string out;
        out.reserve(net::kMaxPlayerNameLen);
        bool pendingSpace = false;
        for (unsigned char c : raw)
        {
            if (c == ' ' || c == '\t')
            {
                pendingSpace = !out.empty();
                continue;
            }
            if (c < 0x21 || c > 0x7E)
                continue;
            if (pendingSpace && out.size() < net::kMaxPlayerNameLen)
            {
                out.push_back(' ');
                pendingSpace = false;
            }
            if (out.size() >= net::kMaxPlayerNameLen)
                break;
            out.push_back(static_cast<char>(c));
        }
        if (out.empty())
            out = "Cow " + std::to_string(netId - net::kPlayerNetIdBase + 1);
        return out;
    }
}

GameServer::GameServer() = default;

GameServer::~GameServer()
{
    // Disconnect before members tear down: destroying the scene fires
    // on_destroy<NetId> for every remaining entity, and the observer must not
    // touch a half-destroyed GameServer.
    scene_.registry().on_destroy<ecs::NetId>().disconnect(this);
    for (auto &[id, s] : sessions_)
        delete s.camera;
}

bool GameServer::init(const std::string &scenePath)
{
    host_.setContext(&scene_, nullptr); // no Window on the server

    // Published scripts are untrusted code running on shared hardware, so the
    // server runs them on a much tighter budget than the editor does. Set
    // before init loads the scene, which is what compiles its scripts.
    host_.setScriptLimits(ScriptHost::serverLimits());
    // Bodies are a cumulative resource the per-call budget cannot bound: a
    // script spawning a few per tick stays under every limit and still fills
    // the world. 512 is far above anything the shipped scenes reach.
    host_.setMaxSpawnedEntities(512);

    // Observe entity destruction so a scripted destroy (shoot_cow despawning a
    // cow) is announced to clients. Connected before any entities exist.
    scene_.registry().on_destroy<ecs::NetId>().connect<&GameServer::onNetIdDestroyed>(*this);

    // Explosions can't be inferred from snapshots fast enough to feel right, so
    // they're replicated as events. Queue rather than send here: this fires from
    // inside the script update, and every other outbound message in a tick is
    // flushed together once the simulation for that tick is complete.
    scene_.setExplosionObserver(
        [this](const glm::vec3 &pos, const PhysicsWorld::BlastParams &p)
        {
            net::Explosion e;
            e.pos = pos;
            e.radius = p.radius;
            e.speed = p.speed;
            e.upBias = p.upBias;
            e.spin = p.spin;
            pendingExplosions_.push_back(e);
        });

    if (!scene_.loadFromJSON(scenePath))
    {
        std::cerr << "GameServer: failed to load scene '" << scenePath
                  << "', using default\n";
        scene_.populateDefault();
    }
    scene_.addRigidBodiesToWorld(physics_);

    // Compile + start the scene's own scripts (props like spin/jump). Player
    // scripts are compiled per-spawn; player_movement/shoot_cow have no
    // on-start handler so they need no explicit start.
    scene_.loadScripts(host_);
    host_.setTime(0.0);
    host_.setDelta(0.0);
    scene_.startScripts(host_);

    // Replicate every dynamic scene body. Their netId is the (deterministic)
    // Identity.id, which the client assigns identically at scene load, so no
    // spawn message is needed — the client already has these entities and just
    // stops simulating them and follows our snapshots. Static (mass 0) bodies
    // never move, so they aren't replicated.
    assignSceneNetIds(/*firstLoad=*/true);

    ready_ = true;
    return true;
}

void GameServer::spawnPlayer(Session &s)
{
    if (s.spawned)
        return;

    // Spread players out along X so fresh joins don't stack on the spawn point.
    float offset = static_cast<float>(s.netId % 8) * 2.0f;
    glm::vec3 spawnPos(offset, 3.0f, 10.0f);
    glm::mat4 model = glm::translate(glm::mat4(1.0f), spawnPos);

    // Each player owns a Camera the movement script reads for facing; the
    // server drives its look angles from InputCommand each tick.
    s.camera = new Camera(spawnPos, glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    s.entity = ecs::createPlayer(scene_.registry(), &physics_, s.camera, model);

    auto &ident = scene_.registry().get<ecs::Identity>(s.entity);
    ident.scriptPaths = {"scripts/player_movement.cow", "scripts/shoot_cow.cow"};
    scene_.registry().emplace<ecs::PlayerInput>(s.entity); // no LocalPlayer server-side
    scene_.registry().emplace<ecs::NetId>(s.entity, ecs::NetId{s.netId});

    // Compile the newly-attached player scripts (idempotent for existing ones).
    scene_.loadScripts(host_);
    s.spawned = true;

    std::cout << "GameServer: spawned player netId=" << s.netId
              << " '" << s.name << "' (players=" << sessions_.size() << ")\n";
}

void GameServer::despawnPlayer(Session &s)
{
    if (s.spawned && scene_.registry().valid(s.entity))
    {
        if (auto *p = scene_.registry().try_get<ecs::Physics>(s.entity); p && p->body)
            physics_.removeRigidBody(p->body.get());
        scene_.destroyEntity(s.entity);
    }
    delete s.camera;
    s.camera = nullptr;
    s.spawned = false;
}

// Scene bodies replicate under ids the client derives independently, by loading
// the same scene file in the same order. On the first load that derivation is
// just Identity.id, so record what we handed out; on a reset we must hand out
// exactly the same ids again.
//
// The ids cannot simply be re-derived after a reset: resetToInitial() destroys
// and rebuilds the entities, and Identity.id comes from a counter that only ever
// climbs, so the rebuilt world numbers its objects differently from the file a
// client loads. Left alone, the rebuilt bodies also arrive here with no NetId at
// all, and detectAndAnnounceSpawns() -- which runs immediately after the reset in
// tick() -- would adopt them as script spawns and re-announce the whole scene in
// the spawn id range. A client that reloaded after that reset then claims the
// scene ids it derived locally, gets no snapshots for them (they no longer
// exist), and leaves them rendered but inert, while the spawn replay builds a
// second, live copy of the same objects beside them.
//
// Load order is recovered by sorting on Identity.id: within one load the counter
// increases monotonically, so ascending id is the order the file was read in --
// the one thing that is stable across a rebuild.
void GameServer::assignSceneNetIds(bool firstLoad)
{
    std::vector<std::pair<int, ecs::Entity>> bodies;
    auto view = scene_.registry().view<ecs::Physics, ecs::Identity>();
    for (auto e : view)
    {
        if (view.get<ecs::Physics>(e).mass <= 0.0)
            continue; // static geometry never moves, so it is never replicated
        if (scene_.registry().all_of<ecs::NetId>(e))
            continue; // a spared player keeps the id it already had
        bodies.emplace_back(view.get<ecs::Identity>(e).id, e);
    }
    std::sort(bodies.begin(), bodies.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    if (firstLoad)
    {
        sceneNetIds_.clear();
        sceneNetIds_.reserve(bodies.size());
        for (auto &[id, e] : bodies)
        {
            const auto netId = static_cast<uint32_t>(id);
            scene_.registry().emplace<ecs::NetId>(e, ecs::NetId{netId});
            sceneNetIds_.push_back(netId);
        }
        return;
    }

    // A reset rebuilds the same file, so the counts match. If they ever do not,
    // assign what we can and leave the rest to be announced as spawns rather
    // than silently pairing objects with the wrong ids.
    const size_t n = std::min(bodies.size(), sceneNetIds_.size());
    for (size_t i = 0; i < n; ++i)
        scene_.registry().emplace<ecs::NetId>(bodies[i].second, ecs::NetId{sceneNetIds_[i]});
    if (bodies.size() != sceneNetIds_.size())
        std::cerr << "reset: scene had " << sceneNetIds_.size() << " replicated bodies, now "
                  << bodies.size() << "; ids past the overlap will be re-announced\n";
}

void GameServer::applyPendingReset()
{
    if (!scene_.consumeResetRequest())
        return;

    // Spare every connected player's entity — Scene only tracks a single
    // playerEntity_ (for the client's own local player), but GameServer owns
    // one per session, so it has to hand the whole list in itself.
    std::vector<ecs::Entity> alive;
    for (auto &[id, s] : sessions_)
        if (s.spawned)
            alive.push_back(s.entity);
    scene_.setSparedEntities(alive);
    // Runtime spawns (cows) really are gone and are still announced; only the
    // scene's own bodies are exempt, because they come straight back.
    resettingScene_ = true;
    scene_.resetToInitial();
    resettingScene_ = false;
    scene_.setSparedEntities({});

    // Before scripts run: a script's on start() may spawn, and those spawns must
    // fall through to detectAndAnnounceSpawns() as genuine spawns rather than
    // being mistaken for rebuilt scene objects.
    assignSceneNetIds(/*firstLoad=*/false);

    // Recompile + restart every script, same as the client's reset — the
    // spared players keep their entity/NetId, but their scripts (movement,
    // shoot cooldown, ...) go back to a fresh on start() too.
    scriptTime_ = 0.0;
    scene_.resetScripts();
    scene_.loadScripts(host_);
    host_.setTime(0.0);
    host_.setDelta(0.0);
    scene_.startScripts(host_);

    // Put every surviving player back at their spawn point, same placement
    // spawnPlayer uses, with velocity cleared — a reset that rebuilt the
    // world but left everyone wherever they were standing is only half of one.
    for (auto &[id, s] : sessions_)
    {
        if (!s.spawned || !scene_.registry().valid(s.entity))
            continue;
        auto *p = scene_.registry().try_get<ecs::Physics>(s.entity);
        if (!p || !p->body)
            continue;
        float offset = static_cast<float>(s.netId % 8) * 2.0f;
        btTransform xf;
        xf.setIdentity();
        xf.setOrigin(btVector3(offset, 3.0f, 10.0f));
        p->body->setWorldTransform(xf);
        if (p->motion)
            p->motion->setWorldTransform(xf);
        p->body->setLinearVelocity(btVector3(0, 0, 0));
        p->body->setAngularVelocity(btVector3(0, 0, 0));
        p->body->activate(true);
    }

    std::cout << "GameServer: scene reset\n";
}

void GameServer::onConnect(uint32_t session)
{
    // Register the session; the player entity is created when ClientHello
    // arrives (so we know the client speaks a compatible protocol first).
    auto [it, inserted] = sessions_.try_emplace(session);
    it->second.lastActive = serverTime_;
}

void GameServer::onDisconnect(uint32_t session)
{
    auto it = sessions_.find(session);
    if (it == sessions_.end())
        return;
    uint32_t netId = it->second.netId;
    despawnPlayer(it->second);
    sessions_.erase(it);

    // Tell everyone else the avatar is gone.
    if (send_ && netId)
        for (auto &[id, other] : sessions_)
            send_(id, net::PlayerLeave{netId});
}

void GameServer::onMessage(uint32_t session, const net::Message &msg)
{
    if (auto it = sessions_.find(session); it != sessions_.end())
        it->second.lastActive = serverTime_; // any traffic keeps the session alive

    if (const auto *hello = std::get_if<net::ClientHello>(&msg))
    {
        // Reject incompatible clients (or a stale client hitting a fresh server):
        // no ServerWelcome, so it stays unjoined rather than desyncing into ghosts.
        if (hello->protocolVersion != net::kProtocolVersion)
        {
            std::cout << "GameServer: refusing session " << session
                      << " — protocol v" << hello->protocolVersion
                      << " != server v" << net::kProtocolVersion
                      << " (stale binary? rebuild both ends)\n";
            return;
        }
        Session &s = sessions_[session];
        if (!s.spawned)
        {
            // Enforce room capacity. A refused client gets no ServerWelcome, so it
            // stays unjoined (and can retry later as slots free up).
            size_t active = 0;
            for (auto &[id, o] : sessions_)
                if (o.spawned)
                    ++active;
            if (active >= maxPlayers_)
            {
                std::cout << "GameServer: room full (" << active << "/" << maxPlayers_
                          << "), refusing session " << session << "\n";
                return;
            }

            s.netId = net::kPlayerNetIdBase + (nextPlayerIdx_++);
            s.name = sanitizePlayerName(hello->name, s.netId);
            spawnPlayer(s);
            if (send_)
            {
                net::ServerWelcome w;
                w.playerNetId = s.netId;
                w.sceneId = 0;
                w.tickRate = tickRate_;
                send_(session, w);
                for (auto &[id, other] : sessions_)
                {
                    if (id == session)
                        continue;
                    // Announce the newcomer to the others...
                    send_(id, net::PlayerJoin{s.netId, s.name});
                    // ...and the others to the newcomer, which otherwise only
                    // meets them through snapshots and would have no name to
                    // put on their avatars.
                    if (other.spawned)
                        send_(session, net::PlayerJoin{other.netId, other.name});
                }
                // Replay existing spawned objects so this client can build them.
                for (const auto &spawn : spawnedObjects_)
                    send_(session, spawn);
            }
        }
        return;
    }

    if (const auto *in = std::get_if<net::InputCommand>(&msg))
    {
        auto it = sessions_.find(session);
        if (it == sessions_.end())
            return;
        Session &s = it->second;
        // Drop stale/reordered inputs (unreliable channel can duplicate/reorder).
        if (in->sequence < s.lastSeq)
            return;
        s.lastInput = *in;
        s.lastSeq = in->sequence;
        return;
    }
    // Other message types are server-authored; ignore inbound.
}

net::Snapshot GameServer::buildSnapshot(uint32_t ackSeq) const
{
    net::Snapshot snap;
    snap.serverTick = serverTick_;
    snap.ackSeq = ackSeq;

    // Every replicated body: players, dynamic scene objects, and spawned objects.
    auto view = scene_.registry().view<const ecs::NetId, const ecs::Physics>();
    for (auto e : view)
    {
        const auto &p = view.get<const ecs::Physics>(e);
        if (!p.body)
            continue;
        const btTransform &xf = p.body->getWorldTransform();
        const btVector3 &o = xf.getOrigin();
        const btQuaternion q = xf.getRotation();

        const btVector3 &v = p.body->getLinearVelocity();

        net::EntityState es;
        es.netId = view.get<const ecs::NetId>(e).id;
        es.pos = {o.x(), o.y(), o.z()};
        es.rot = glm::quat(q.w(), q.x(), q.y(), q.z());
        es.vel = {v.x(), v.y(), v.z()};

        // For player entities the upright capsule body carries no meaningful
        // yaw, so replace the rotation with the player's look heading: a
        // Y-axis turn that points the cow avatar's +X (nose) axis along the
        // camera's horizontal facing. Clients render other players as cows and
        // interpolate this rotation, so avatars turn to face where each player
        // is looking.
        if (const auto *pc = scene_.registry().try_get<ecs::PlayerController>(e); pc && pc->camera)
        {
            glm::vec3 f = pc->camera->getFront();
            float heading = std::atan2(-f.z, f.x); // maps model +X onto (f.x,0,f.z)
            es.rot = glm::angleAxis(heading, glm::vec3(0.0f, 1.0f, 0.0f));
        }

        snap.entities.push_back(es);
    }
    return snap;
}

void GameServer::detectAndAnnounceSpawns()
{
    // Bodies created by scripts this tick (e.g. shoot_cow) have Physics but no
    // NetId yet. Give each a spawn-range id, describe it, and broadcast so all
    // clients build the visual. Static (mass 0) bodies are never replicated.
    auto fresh = scene_.registry().view<ecs::Physics, ecs::Identity>(entt::exclude<ecs::NetId>);
    for (auto e : fresh)
    {
        auto &p = fresh.get<ecs::Physics>(e);
        if (p.mass <= 0.0)
            continue;

        uint32_t netId = net::kSpawnNetIdBase + (nextSpawnNetId_++);
        scene_.registry().emplace<ecs::NetId>(e, ecs::NetId{netId});

        net::SpawnEntity spawn;
        spawn.netId = netId;
        spawn.kind = net::SpawnKind::Cube;
        const auto &ident = fresh.get<ecs::Identity>(e);
        if (ident.meshPath.find("cow") != std::string::npos)
            spawn.kind = net::SpawnKind::Cow;
        else if (auto *sm = scene_.registry().try_get<ecs::ShapeMarker>(e))
            spawn.kind = (sm->kind == ecs::ShapeKind::Plane) ? net::SpawnKind::Plane : net::SpawnKind::Cube;
        if (auto *t = scene_.registry().try_get<ecs::Transform>(e))
            spawn.scale = glm::vec3(t->scale);
        if (auto *rd = scene_.registry().try_get<ecs::Renderable>(e))
            spawn.color = rd->color;

        spawnedObjects_.push_back(spawn);
        if (send_)
            for (auto &[id, s] : sessions_)
                if (s.spawned)
                    send_(id, spawn);
    }
}

void GameServer::onNetIdDestroyed(ecs::Registry &reg, ecs::Entity e)
{
    // Fires while the entity is being destroyed; the NetId is still readable.
    // Player entities also carry a NetId, but their removal is announced via
    // PlayerLeave, so exclude that range.
    //
    // Base-scene dynamic objects are announced when a script destroys one, but
    // NOT when a reset does: the reset rebuilds them under the same netIds
    // (assignSceneNetIds), so the client's representation stays valid the whole
    // way through and a despawn would only make it throw away something that is
    // about to start moving again.
    uint32_t netId = reg.get<ecs::NetId>(e).id;
    const bool sceneBody = netId < net::kPlayerNetIdBase;
    if (sceneBody && resettingScene_)
        return;
    if (sceneBody || netId >= net::kSpawnNetIdBase)
        pendingDespawns_.push_back(netId);
}

void GameServer::flushDespawns()
{
    if (pendingDespawns_.empty())
        return;
    for (uint32_t netId : pendingDespawns_)
    {
        // Drop from the late-join replay list so a client joining later isn't
        // told to spawn something that no longer exists.
        spawnedObjects_.erase(
            std::remove_if(spawnedObjects_.begin(), spawnedObjects_.end(),
                           [netId](const net::SpawnEntity &s) { return s.netId == netId; }),
            spawnedObjects_.end());

        net::DespawnEntity d;
        d.netId = netId;
        if (send_)
            for (auto &[id, s] : sessions_)
                if (s.spawned)
                    send_(id, d);
    }
    pendingDespawns_.clear();
}

void GameServer::flushExplosions()
{
    if (pendingExplosions_.empty())
        return;
    // Not replayed to late joiners, unlike SpawnEntity: a blast is an instant,
    // not a piece of world state, and re-sending it on join would shove a player
    // who wasn't there when it happened.
    if (send_)
        for (const net::Explosion &e : pendingExplosions_)
            for (auto &[id, s] : sessions_)
                if (s.spawned)
                    send_(id, e);
    pendingExplosions_.clear();
}

size_t GameServer::spawnedCount() const
{
    size_t n = 0;
    for (const auto &[id, s] : sessions_)
        if (s.spawned)
            ++n;
    return n;
}

void GameServer::sweepIdleSessions()
{
    // A clean disconnect sends PlayerLeave via onDisconnect. But if the transport
    // drops without notice (abrupt reload, crashed sidecar worker), that frame can
    // be lost and the avatar would linger forever. Since a joined client streams an
    // InputCommand every tick, prolonged silence means it's gone — reap it so
    // players can't pile up as motionless ghosts.
    std::vector<uint32_t> dead;
    for (auto &[id, s] : sessions_)
        if (serverTime_ - s.lastActive > idleTimeout_)
            dead.push_back(id);

    for (uint32_t id : dead)
    {
        auto it = sessions_.find(id);
        if (it == sessions_.end())
            continue;
        uint32_t netId = it->second.netId;
        std::cout << "GameServer: reaping idle session " << id << " (netId=" << netId << ")\n";
        despawnPlayer(it->second);
        sessions_.erase(it);
        if (send_ && netId)
            for (auto &[other, o] : sessions_)
                send_(other, net::PlayerLeave{netId});
    }
}

void GameServer::tick(float dt)
{
    if (!ready_)
        return;

    serverTime_ += dt;
    sweepIdleSessions();

    // 1) Fold each session's latest input into its player: keys for the script,
    //    and camera look angles so movement is relative to where they face.
    for (auto &[id, s] : sessions_)
    {
        if (!s.spawned)
            continue;
        auto &pin = scene_.registry().get<ecs::PlayerInput>(s.entity);
        pin.keys = s.lastInput.keys;
        pin.lookYaw = s.lastInput.lookYaw;
        pin.lookPitch = s.lastInput.lookPitch;
        pin.sequence = s.lastSeq;
        if (s.camera)
            s.camera->setLook(s.lastInput.lookYaw, s.lastInput.lookPitch);
    }

    // 2) Advance physics then scripts — same order as the client's advanceSim,
    //    so a predicting client stays in lockstep with the server.
    physics_.stepSimulation(dt, 1);
    scriptTime_ += dt;
    host_.setTime(scriptTime_);
    host_.setDelta(dt);
    scene_.updateScripts(host_, dt);

    // A plate (or any script) may have called reset_scene() this tick. Acted
    // on here, after scripts have finished running for the frame — see
    // Scene::requestReset for why it can't happen mid-iteration.
    applyPendingReset();

    // Pick up anything the scripts spawned and tell the clients about it, then
    // announce anything they destroyed (e.g. shoot_cow despawning old cows).
    detectAndAnnounceSpawns();
    // Before the despawn: a cow announces its blast and only then removes
    // itself, so a client that applies them in arrival order sees the same
    // sequence the server simulated.
    flushExplosions();
    flushDespawns();

    ++serverTick_;

    // 3) Broadcast snapshots per session, each carrying that client's ack. The
    // world state is identical across sessions (only ackSeq differs), so build the
    // entity list once, then split it into datagram-sized chunks (see
    // net::kMaxSnapshotEntities) so a large world never overflows an unreliable
    // datagram and gets truncated + dropped wholesale by the client's decoder.
    //
    // Snapshots go out at snapshotHz_ (< tickRate_), not every tick — the client
    // interpolates ~interpDelay behind real time, so a lower rate is invisible but
    // cuts server egress proportionally. Reliable spawn/despawn are unaffected.
    const uint32_t snapDivisor = std::max<uint32_t>(1u, tickRate_ / std::max<uint16_t>(1, snapshotHz_));
    if (send_ && (serverTick_ % snapDivisor == 0))
    {
        const net::Snapshot world = buildSnapshot(0);
        for (const auto &[id, s] : sessions_)
        {
            if (!s.spawned)
                continue;
            const auto &ents = world.entities;
            size_t sent = 0;
            do // send at least one snapshot (possibly empty) so the ack flows
            {
                net::Snapshot chunk;
                chunk.serverTick = serverTick_;
                chunk.ackSeq = s.lastSeq;
                size_t n = std::min(net::kMaxSnapshotEntities, ents.size() - sent);
                chunk.entities.assign(ents.begin() + sent,
                                      ents.begin() + sent + n);
                send_(id, net::Message{chunk});
                sent += n;
            } while (sent < ents.size());
        }
    }
}
