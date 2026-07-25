#include "net/NetClient.hpp"

#include "core/Scene.hpp"
#include "core/PhysicsWorld.hpp"
#include "ecs/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <btBulletDynamicsCommon.h>

#include <cmath>
#include <vector>

namespace net
{
    namespace
    {
        // Remote players are rendered as a cow (models/cow.obj, ~10.4 x 6.4 x 3.4
        // in model units). Uniform 0.2 makes a ~2.1-long, ~1.3-tall avatar.
        const glm::vec3 kAvatarScale(0.2f);

        void setNetTransform(Scene *scene, ecs::Entity e, const glm::vec3 &pos,
                             const glm::quat &rot, const glm::vec3 &scale)
        {
            auto *t = scene->registry().try_get<ecs::Transform>(e);
            if (!t)
                return;
            glm::mat4 rs = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
            t->position = glm::dvec3(pos);
            t->modelNoScale = rs;
            t->model = rs * glm::scale(glm::mat4(1.0f), scale);

            // If the entity carries a kinematic collider (claimed scene body or a
            // net proxy/avatar we gave one), drive its body/motion-state so Bullet
            // collides the locally-predicted player against the interpolated pose.
            if (auto *p = scene->registry().try_get<ecs::Physics>(e); p && p->body)
            {
                btTransform xf;
                xf.setIdentity();
                xf.setOrigin(btVector3(pos.x, pos.y, pos.z));
                xf.setRotation(btQuaternion(rot.x, rot.y, rot.z, rot.w));
                p->body->setWorldTransform(xf);
                if (p->motion)
                    p->motion->setWorldTransform(xf);
            }
        }
    }

    NetClient::NetClient(ITransport *transport, Scene *scene, ecs::Entity localPlayer,
                         std::string playerName)
        : transport_(transport), scene_(scene), localPlayer_(localPlayer),
          playerName_(std::move(playerName))
    {
        claimSceneObjects();
    }

    NetClient::~NetClient()
    {
        for (auto &[id, r] : reps_)
            if (r.ownsEntity && scene_->registry().valid(r.entity))
                scene_->destroyEntity(r.entity);
    }

    void NetClient::claimSceneObjects()
    {
        // Take over every dynamic scene body: the server is authoritative for it,
        // so we stop simulating it and drop its scripts (spin/jump), then follow
        // snapshots keyed by Identity.id — which both ends assigned deterministically
        // at scene load. Rather than removing the body, we make it *kinematic* and
        // keep it in the world, so the locally-predicted player still collides with
        // it while its motion comes entirely from snapshots.
        struct Claim
        {
            ecs::Entity e;
            uint32_t netId;
            glm::vec3 scale;
        };
        std::vector<Claim> claims;

        auto view = scene_->registry().view<ecs::Physics, ecs::Identity>();
        for (auto e : view)
        {
            if (e == localPlayer_)
                continue;
            auto &p = view.get<ecs::Physics>(e);
            if (p.mass <= 0.0)
                continue; // static geometry stays local
            glm::vec3 scale(1.0f);
            if (auto *t = scene_->registry().try_get<ecs::Transform>(e))
                scale = glm::vec3(t->scale);
            if (p.body)
            {
                p.body->setCollisionFlags(p.body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                p.body->setActivationState(DISABLE_DEACTIVATION);
                p.body->setMassProps(0.0, btVector3(0, 0, 0));
                p.body->setLinearVelocity(btVector3(0, 0, 0));
                p.body->setAngularVelocity(btVector3(0, 0, 0));
            }
            claims.push_back({e, static_cast<uint32_t>(view.get<ecs::Identity>(e).id), scale});
        }

        for (auto &c : claims)
        {
            scene_->registry().remove<ecs::ScriptComponent>(c.e);
            Rep rep;
            rep.entity = c.e;
            rep.scale = c.scale;
            reps_[c.netId] = rep; // ownsEntity=false, placed=true, collider already present
        }
    }

    void NetClient::update(float dt)
    {
        clientTime_ += dt;
        lastDt_ = dt;
        if (!transport_)
            return;
        sendHelloIfNeeded();
        processIncoming();
        if (joined_)
            sendInput();
        updateReplicated();
    }

    void NetClient::sendHelloIfNeeded()
    {
        if (sentHello_ || !transport_->connected())
            return;
        ClientHello hello;
        hello.name = playerName_;
        transport_->send(Message{hello});
        sentHello_ = true;
    }

    void NetClient::processIncoming()
    {
        Incoming in;
        while (transport_->poll(in))
        {
            auto msg = decode(in.bytes.data(), in.bytes.size());
            if (!msg)
                continue;

            if (const auto *w = std::get_if<ServerWelcome>(&*msg))
            {
                myNetId_ = w->playerNetId;
                joined_ = true;
            }
            else if (const auto *join = std::get_if<PlayerJoin>(&*msg))
            {
                // Register the avatar on the join event; it stays hidden until its
                // first snapshot positions it. (Snapshots would create it too, but
                // this keeps the roster explicit for join/leave.) This is also the
                // only message carrying the player's name, so it may be updating an
                // avatar a snapshot already created.
                if (join->netId != myNetId_)
                {
                    ensurePlayerAvatar(join->netId);
                    setAvatarName(join->netId, join->name);
                }
            }
            else if (const auto *leave = std::get_if<PlayerLeave>(&*msg))
            {
                removeRep(leave->netId);
            }
            else if (const auto *spawn = std::get_if<SpawnEntity>(&*msg))
            {
                onSpawn(*spawn);
            }
            else if (const auto *despawn = std::get_if<DespawnEntity>(&*msg))
            {
                removeRep(despawn->netId);
            }
            else if (const auto *boom = std::get_if<Explosion>(&*msg))
            {
                // Replay the server's blast locally. Every body here except the
                // player we predict is kinematic (claimSceneObjects made it so),
                // and applyRadialBlast skips those, so this lands the knockback
                // on exactly the one body this client simulates itself — and
                // draws the shockwave for the rest.
                //
                // Gated on joined_ so a blast arriving before ServerWelcome
                // can't shove a player that isn't in the world yet.
                if (joined_)
                {
                    PhysicsWorld::BlastParams p;
                    p.radius = boom->radius;
                    p.speed = boom->speed;
                    p.upBias = boom->upBias;
                    p.spin = boom->spin;
                    scene_->explode(boom->pos, p);
                }
            }
            else if (const auto *snap = std::get_if<Snapshot>(&*msg))
            {
                if (!joined_)
                    continue;
                for (const auto &e : snap->entities)
                {
                    if (e.netId == myNetId_)
                    {
                        applyLocalReconcile(e);
                        continue;
                    }
                    Rep *rep = nullptr;
                    auto it = reps_.find(e.netId);
                    if (it != reps_.end())
                        rep = &it->second;
                    else if (e.netId >= kPlayerNetIdBase && e.netId < kSpawnNetIdBase)
                        rep = ensurePlayerAvatar(e.netId); // another player
                    // else: a spawned object we haven't received SpawnEntity for
                    // yet (reliable, should arrive) — ignore this sample.
                    if (rep)
                    {
                        rep->buf.push_back(Sample{clientTime_, e.pos, e.rot});
                        while (rep->buf.size() > 32)
                            rep->buf.pop_front();
                    }
                }
            }
        }
    }

    void NetClient::sendInput()
    {
        InputCommand c;
        c.sequence = ++inputSeq_;
        if (auto *pin = scene_->registry().try_get<ecs::PlayerInput>(localPlayer_))
        {
            c.keys = pin->keys;
            c.lookYaw = pin->lookYaw;
            c.lookPitch = pin->lookPitch;
        }
        c.dt = lastDt_;
        transport_->send(Message{c});
    }

    void NetClient::applyLocalReconcile(const EntityState &s)
    {
        auto *p = scene_->registry().try_get<ecs::Physics>(localPlayer_);
        if (!p || !p->body)
            return;
        btTransform xf = p->body->getWorldTransform();
        const btVector3 &cur = xf.getOrigin();
        glm::vec3 curPos(cur.x(), cur.y(), cur.z());
        float err = glm::distance(curPos, s.pos);
        if (err <= snapThreshold_)
            return; // trust prediction for small errors

        if (err >= hardSnapThreshold_)
        {
            // Big desync (teleport / respawn / long stall): snap hard — there's
            // nothing meaningful to smooth toward.
            xf.setIdentity();
            xf.setOrigin(btVector3(s.pos.x, s.pos.y, s.pos.z));
            xf.setRotation(btQuaternion(s.rot.x, s.rot.y, s.rot.z, s.rot.w));
            p->body->setWorldTransform(xf);
            if (p->motion)
                p->motion->setWorldTransform(xf);
            p->body->setLinearVelocity(btVector3(s.vel.x, s.vel.y, s.vel.z));
            p->body->activate(true);
            return;
        }

        // Moderate error: nudge a fraction of the way toward the server position
        // each snapshot instead of snapping, so the first-person camera glides to
        // the corrected spot over a few frames.
        glm::vec3 corrected = glm::mix(curPos, s.pos, correctionRate_);
        xf.setOrigin(btVector3(corrected.x, corrected.y, corrected.z));
        p->body->setWorldTransform(xf);
        if (p->motion)
            p->motion->setWorldTransform(xf);

        // Velocity gets the same nudge. Grounded movement is driven by input
        // every frame (player_movement.cow re-derives rb.vx/vz from the keys
        // held right now), so a partial velocity correction here is invisible
        // by the next tick and input stays responsive. Airborne movement is
        // the opposite: after an explosion the script deliberately leaves
        // velocity to gravity + the blast alone, so with position-only
        // correction any client/server mismatch in the blast (falloff/direction
        // computed from whatever position each side happened to be predicting
        // at the moment) never converges -- the body keeps flying its own
        // slightly-wrong arc and gets yanked back toward the server's arc every
        // snapshot for the whole flight. That constant tug is what reads as
        // "laggy" flight. Blending velocity too lets the arc itself converge,
        // so the position correction shrinks and eventually stops.
        const btVector3 curVel = p->body->getLinearVelocity();
        glm::vec3 correctedVel = glm::mix(glm::vec3(curVel.x(), curVel.y(), curVel.z()), s.vel, correctionRate_);
        p->body->setLinearVelocity(btVector3(correctedVel.x, correctedVel.y, correctedVel.z));
        p->body->activate(true);
    }

    NetClient::Rep *NetClient::ensurePlayerAvatar(uint32_t netId)
    {
        auto it = reps_.find(netId);
        if (it != reps_.end())
            return &it->second;
        float hue = static_cast<float>((netId * 47u) % 360u) / 360.0f;
        glm::vec4 color(0.4f + 0.5f * std::fabs(std::sin(hue * 6.28f)),
                        0.4f + 0.5f * std::fabs(std::sin((hue + 0.33f) * 6.28f)),
                        0.4f + 0.5f * std::fabs(std::sin((hue + 0.66f) * 6.28f)),
                        1.0f);
        ecs::Entity e = scene_->createRemoteAvatar(color);

        // Float the nametag just clear of the avatar's own silhouette. The cow's
        // origin is nowhere near its centre, so the mesh's local bounding box is
        // the only thing that actually says how tall it stands.
        ecs::Nametag tag;
        tag.color = color;
        if (auto *rd = scene_->registry().try_get<ecs::Renderable>(e); rd && rd->mesh)
            tag.offset = rd->mesh->getLocalMax().y * kAvatarScale.y + 0.35f;
        tag.text.clear(); // filled in by PlayerJoin; draws nothing until then
        scene_->registry().emplace<ecs::Nametag>(e, std::move(tag));

        Rep rep;
        rep.entity = e;
        rep.scale = kAvatarScale;
        rep.ownsEntity = true;
        rep.placed = false;        // hidden until the first snapshot positions it
        rep.wantsCollider = true;  // give it a kinematic collider once placed
        reps_[netId] = rep;
        return &reps_[netId];
    }

    void NetClient::setAvatarName(uint32_t netId, const std::string &name)
    {
        auto it = reps_.find(netId);
        if (it == reps_.end() || !scene_->registry().valid(it->second.entity))
            return;
        if (auto *tag = scene_->registry().try_get<ecs::Nametag>(it->second.entity))
            tag->text = name;
    }

    void NetClient::onSpawn(const SpawnEntity &s)
    {
        if (reps_.count(s.netId))
            return; // already have it
        ecs::Entity e = scene_->createNetProxy(static_cast<int>(s.kind), s.color);
        Rep rep;
        rep.entity = e;
        rep.scale = s.scale;
        rep.ownsEntity = true;
        rep.placed = false;
        rep.wantsCollider = true;
        reps_[s.netId] = rep;
    }

    void NetClient::removeRep(uint32_t netId)
    {
        auto it = reps_.find(netId);
        if (it == reps_.end())
            return;
        // Always tear down the entity, not just proxies/avatars we created
        // (ownsEntity). A claimed scene object (ownsEntity=false) only gets a
        // DespawnEntity when the server rebuilt the world out from under it
        // (a scene reset) — the old entity has to go, since a fresh one is on
        // its way via SpawnEntity.
        if (scene_->registry().valid(it->second.entity))
            scene_->destroyEntity(it->second.entity);
        reps_.erase(it);
    }

    void NetClient::updateReplicated()
    {
        double target = clientTime_ - interpDelay_;
        for (auto &[id, r] : reps_)
        {
            if (r.buf.empty())
                continue;
            while (r.buf.size() >= 2 && r.buf[1].t <= target)
                r.buf.pop_front();

            glm::vec3 pos;
            glm::quat rot;
            if (target <= r.buf.front().t || r.buf.size() == 1)
            {
                pos = r.buf.front().pos;
                rot = r.buf.front().rot;
            }
            else
            {
                const Sample &a = r.buf[0];
                const Sample &b = r.buf[1];
                double span = b.t - a.t;
                float alpha = span > 1e-6 ? static_cast<float>((target - a.t) / span) : 1.0f;
                alpha = glm::clamp(alpha, 0.0f, 1.0f);
                pos = glm::mix(a.pos, b.pos, alpha);
                rot = glm::slerp(a.rot, b.rot, alpha);
            }
            setNetTransform(scene_, r.entity, pos, rot, r.scale);

            // First real placement: now that the proxy sits at its true pose, give
            // it a kinematic collider (built at that pose, so it can't spawn on top
            // of the player and launch them).
            if (!r.placed)
            {
                r.placed = true;
                if (r.wantsCollider)
                    scene_->attachNetCollider(r.entity, r.scale);
            }
        }
    }
}
