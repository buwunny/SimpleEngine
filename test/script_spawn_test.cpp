// Headless test for script-driven spawning: where objects land, and how long they
// live. No GL context, no sockets — just a Scene, a PhysicsWorld and the script
// systems. Verifies:
//   * the real scripts/shoot_cow.cow puts a cow's *visible centre* on the camera's
//     view axis (cow.obj's origin is 0.89 units off its own centre, which used to
//     land the shot ~2.5 degrees beside the crosshair)
//   * spawn_cow's optional scale argument sizes the collision shape to match
//   * attach_script compiles and attaches a .cow to a spawned object
//   * on start() fires for a script attached after startScripts() has run
//   * several objects can be alive at once, each despawning on its own clock
//     (the regression: shoot_cow used to destroy the previous cow on every shot)
//   * Scene::explode pushes bodies by a velocity change rather than a flat
//     impulse, so the same blast is correctly tuned whatever the target weighs
//   * a static, script-moved pressure plate (scripts/button.cow) detects what
//     lands on it via self_contact_above, sinks, launches it, and rises again
//   * scripts that spawn/attach/destroy during updateScripts() don't corrupt the
//     iteration over the ScriptComponent pool

#include "core/Scene.hpp"
#include "core/PhysicsWorld.hpp"
#include "core/Camera.hpp"
#include "ecs/Components.hpp"
#include "ecs/Factories.hpp"
#include "ecs/InputKeys.hpp"
#include "ecs/systems/ScriptSystem.hpp"
#include "meshes/AssetManager.hpp"
#include "script/ScriptHost.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <btBulletDynamicsCommon.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while(0)

namespace
{
    // Count the live cows, i.e. everything the spawner produced (the driver
    // entity itself carries no mesh).
    int liveCows(Scene &scene)
    {
        int n = 0;
        for (auto e : scene.registry().view<ecs::Identity>())
            if (scene.registry().get<ecs::Identity>(e).meshPath.find("cow") != std::string::npos)
                ++n;
        return n;
    }

    // Advance the sim by one frame at `dt`, exactly as GameServer::tick does.
    void step(Scene &scene, PhysicsWorld &phys, ScriptHost &host, double &t, float dt)
    {
        phys.stepSimulation(dt, 1);
        t += dt;
        host.setTime(t);
        host.setDelta(dt);
        scene.updateScripts(host, dt);
    }
}

// Fires the real scripts/shoot_cow.cow from a known eye position and facing, and
// checks where the cow actually ends up on screen.
static void testShotIsCentred()
{
    PhysicsWorld physics;
    Scene scene;
    scene.populateDefault();
    scene.addRigidBodiesToWorld(physics);

    ScriptHost host;
    host.setContext(&scene, nullptr);

    const glm::vec3 eye(3.0f, 12.0f, -7.0f);
    Camera camera(eye, glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    camera.setLook(37.0f, -11.0f); // an arbitrary off-axis pose, not a lucky one
    camera.setPosition(eye);

    // shoot_cow.cow alone: player_movement.cow would move the camera off `eye`.
    ecs::Entity player = ecs::createPlayer(scene.registry(), &physics, &camera,
                                           glm::translate(glm::mat4(1.0f), eye));
    scene.registry().get<ecs::Identity>(player).scriptPaths = {"scripts/shoot_cow.cow"};
    auto &input = scene.registry().emplace<ecs::PlayerInput>(player);

    host.setTime(0.0);
    host.setDelta(0.0);
    scene.loadScripts(host);
    scene.startScripts(host);

    input.keys = 1ull << ecs::inputKeyBit("c"); // hold C: one shot on the edge
    host.setTime(0.1);
    host.setDelta(1.0f / 60.0f);
    scene.updateScripts(host, 1.0f / 60.0f);

    ecs::Entity cow = ecs::NullEntity;
    for (auto e : scene.registry().view<ecs::Identity>())
        if (scene.registry().get<ecs::Identity>(e).meshPath.find("cow") != std::string::npos)
            cow = e;
    CHECK(cow != ecs::NullEntity);
    if (cow == ecs::NullEntity)
        return;

    const auto &t = scene.registry().get<ecs::Transform>(cow);
    const auto &rd = scene.registry().get<ecs::Renderable>(cow);
    CHECK(std::fabs(t.scale.x - 0.1) < 1e-5); // the scale argument took effect

    // Where the player sees the cow: its model origin plus the mesh's own
    // off-centre bounding box, scaled the same way the renderer scales it.
    glm::vec3 visible = glm::vec3(t.position) + glm::vec3(t.scale) * rd.mesh->getLocalCenter();
    glm::vec3 toCow = visible - eye;
    glm::vec3 front = camera.getFront();

    float distance = glm::length(toCow);
    float offAxis = glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(toCow), front), -1.0f, 1.0f)));
    printf("  shot lands %.3f m ahead, %.4f deg off the view axis\n", distance, offAxis);
    CHECK(offAxis < 0.01f);                      // dead centre (was ~2.55 deg)
    CHECK(std::fabs(distance - 1.5f) < 0.01f);   // at the script's `muzzle` distance

    // The collision hull is sized for the cow the player can see, not the
    // full-size model — the scale reaches Bullet at spawn, not a frame later.
    const auto &phys = scene.registry().get<ecs::Physics>(cow);
    btVector3 lo, hi;
    phys.shape->getAabb(btTransform::getIdentity(), lo, hi);
    printf("  collider extent = %.3f m (cow.obj is 10.44 m at scale 1)\n", hi.x() - lo.x());
    CHECK(hi.x() - lo.x() < 1.2f);
}

// The pressure plate: scripts/button.cow driving a static cube. Covers the two
// things a script-driven plate needs from the engine — knowing what is standing
// on it, and being able to move itself while still colliding where it now is.
static void testButton()
{
    PhysicsWorld physics;
    Scene scene;
    ecs::createPlane(scene.registry(), nullptr, 200, 200, glm::mat4(1.0f), glm::vec4(1.0f), 0.0f);

    // Deliberately the exact plate from scenes/scene.json — non-uniformly
    // scaled and sitting *on* the floor rather than a tidy unit cube floating
    // in space. That is the configuration that exposed the plate holding itself
    // down via its own floor, and a test that used a convenient shape instead
    // would have gone on passing through it.
    //
    // Static, per the note in button.cow: a dynamic plate gets pushed through
    // the floor by whatever lands on it.
    glm::mat4 bm = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.15f, 0.0f)),
                              glm::vec3(2.4f, 0.3f, 2.4f));
    ecs::Entity button = ecs::createCube(scene.registry(), &physics, 1, bm,
                                         glm::vec4(1.0f), 0.0f);
    scene.registry().get<ecs::Identity>(button).scriptPaths = {"scripts/button.cow"};

    // Something to drop on it, from well above.
    ecs::Entity weight = ecs::createCube(scene.registry(), &physics, 1,
                                         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f)),
                                         glm::vec4(1.0f), 1.0f);
    scene.addRigidBodiesToWorld(physics);

    ScriptHost host;
    host.setContext(&scene, nullptr);
    double t = 0.0;
    host.setTime(t);
    host.setDelta(0.0);
    scene.loadScripts(host);
    scene.startScripts(host);

    auto buttonY = [&] { return scene.registry().get<ecs::Transform>(button).position.y; };
    auto weightVY = [&] {
        return scene.registry().get<ecs::Physics>(weight).body->getLinearVelocity().y();
    };

    const float restY = static_cast<float>(buttonY());
    const float dt = 1.0f / 60.0f;

    float lowest = restY;
    float fastestUp = 0.0f;
    bool fired = false;
    bool roseAgain = false;
    for (int i = 0; i < 180; ++i) // 3 s: fall, press, fire, rise
    {
        step(scene, physics, host, t, dt);
        lowest = std::min(lowest, static_cast<float>(buttonY()));
        fastestUp = std::max(fastestUp, weightVY());
        if (weightVY() > 20.0f)
            fired = true;
        // Checked only after a launch, and only as "came back up at some point":
        // the weight is thrown straight up, so it lands on the plate again and
        // presses it a second time. Sampling the height at a fixed moment would
        // be asking which half of that cycle 3 seconds happens to land in.
        if (fired && std::fabs(static_cast<float>(buttonY()) - restY) < 0.01f)
            roseAgain = true;
    }

    printf("  button: rest y=%.2f, sank to %.2f, launched the weight at %.1f m/s\n",
           restY, lowest, fastestUp);

    // It sank the full depth. The bar is most of press_depth rather than a
    // twitch, because the failure this guards against is a plate that dips a
    // frame's worth and springs straight back: moving down breaks the contact
    // that is holding it down, so without the latch in button.cow it oscillates
    // a couple of centimetres below rest and never fires.
    CHECK(restY - lowest > 0.15f); // most of button.cow's press_depth
    // ...and fired. The weight arrives falling, so any large positive vertical
    // speed can only have come from the plate.
    CHECK(fastestUp > 20.0f);
    // ...and came back up once the weight was thrown clear.
    CHECK(roseAgain);
}

// The other two plates from scenes/scene.json, on the same press mechanism:
// one dispenses a cube, one resets the world. Both are checked through the real
// scripts, and both also confirm the plate labelled itself via the nametag
// system rather than needing the label authored into the scene.
static void testOtherPlates(const char *script, const char *expectLabel,
                            bool expectSpawn, bool expectReset)
{
    PhysicsWorld physics;
    Scene scene;
    ecs::createPlane(scene.registry(), nullptr, 200, 200, glm::mat4(1.0f), glm::vec4(1.0f), 0.0f);

    glm::mat4 bm = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.15f, 0.0f)),
                              glm::vec3(2.4f, 0.3f, 2.4f));
    ecs::Entity plate = ecs::createCube(scene.registry(), &physics, 1, bm, glm::vec4(1.0f), 0.0f);
    scene.registry().get<ecs::Identity>(plate).scriptPaths = {script};

    ecs::Entity weight = ecs::createCube(scene.registry(), &physics, 1,
                                         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f)),
                                         glm::vec4(1.0f), 1.0f);
    scene.addRigidBodiesToWorld(physics);

    ScriptHost host;
    host.setContext(&scene, nullptr);
    double t = 0.0;
    host.setTime(t);
    host.setDelta(0.0);
    scene.loadScripts(host);
    scene.startScripts(host);

    // The label is set in on start(), so it must already be there.
    const auto *tag = scene.registry().try_get<ecs::Nametag>(plate);
    CHECK(tag != nullptr);
    if (tag)
        CHECK(tag->text == expectLabel);

    const int before = static_cast<int>(scene.registry().view<ecs::Physics>().size());
    const float restY = static_cast<float>(scene.registry().get<ecs::Transform>(plate).position.y);
    float lowest = restY;
    for (int i = 0; i < 120; ++i)
    {
        step(scene, physics, host, t, 1.0f / 60.0f);
        lowest = std::min(lowest, static_cast<float>(scene.registry().get<ecs::Transform>(plate).position.y));
    }
    const int after = static_cast<int>(scene.registry().view<ecs::Physics>().size());

    // Consumed rather than peeked, so the flag's clear-on-read is exercised too.
    const bool askedForReset = scene.consumeResetRequest();

    printf("  %-24s label='%s' sank %.2f m, bodies %d -> %d, reset=%s\n",
           script, tag ? tag->text.c_str() : "?", restY - lowest, before, after,
           askedForReset ? "yes" : "no");

    CHECK(restY - lowest > 0.15f);          // it pressed
    CHECK(askedForReset == expectReset);    // ...and asked (or didn't) for a reset
    if (expectSpawn)
        CHECK(after > before);              // a cube appeared
    else
        CHECK(after == before);             // and the other plate made nothing
    (void)weight;
}

// Scene::setSparedEntities: GameServer has no single playerEntity_ (it owns one
// player entity per session), so a multiplayer scene reset relies entirely on
// this to keep every connected player alive across resetToInitial() while
// everything else is torn down and rebuilt from the loaded JSON.
static void testMultiEntitySpare()
{
    PhysicsWorld physics;
    Scene scene;
    const std::string json = R"({
        "objects": [
            {"type": "Cube", "size": 1, "position": [1, 0, 0], "mass": 1.0},
            {"type": "Cube", "size": 1, "position": [-1, 0, 0], "mass": 1.0}
        ]
    })";
    CHECK(scene.loadFromString(json));
    scene.addRigidBodiesToWorld(physics);
    CHECK(scene.registry().view<ecs::Physics>().size() == 2);

    // Two stand-ins for the player entities GameServer tracks per session.
    Camera cam1(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    Camera cam2(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    ecs::Entity p1 = ecs::createPlayer(scene.registry(), &physics, &cam1, glm::mat4(1.0f));
    ecs::Entity p2 = ecs::createPlayer(scene.registry(), &physics, &cam2, glm::mat4(1.0f));

    scene.setSparedEntities({p1, p2});
    scene.resetToInitial();
    scene.setSparedEntities({});

    CHECK(scene.registry().valid(p1));
    CHECK(scene.registry().valid(p2));

    int cubesAfter = 0;
    for (auto e : scene.registry().view<ecs::Physics, ecs::Identity>())
        if (e != p1 && e != p2)
            ++cubesAfter;
    printf("  multi-entity spare: both players survived, %d cube(s) rebuilt\n", cubesAfter);
    CHECK(cubesAfter == 2); // rebuilt fresh from the same JSON, not left destroyed
}

// Scene::explode's blast model: that its strength is set by distance alone and
// not by what the target weighs, that it falls off, and that it tilts upward.
static void testBlast()
{
    PhysicsWorld physics;
    Scene scene;
    // No scene geometry on purpose: an empty world means nothing can occlude
    // the blast, so these numbers are the falloff curve and nothing else.
    scene.addRigidBodiesToWorld(physics);

    // Two cubes side by side at the same distance from the blast, differing
    // only in mass by a factor of 100.
    const glm::vec3 blastAt(0.0f, 0.0f, 0.0f);
    ecs::Entity light = ecs::createCube(scene.registry(), &physics, 1,
                                        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f)),
                                        glm::vec4(1.0f), 1.0f);
    ecs::Entity heavy = ecs::createCube(scene.registry(), &physics, 1,
                                        glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)),
                                        glm::vec4(1.0f), 100.0f);
    // Four times as far out as the other two, to read the falloff.
    ecs::Entity far = ecs::createCube(scene.registry(), &physics, 1,
                                      glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 4.0f)),
                                      glm::vec4(1.0f), 1.0f);

    PhysicsWorld::BlastParams params; // radius 6, speed 20, upBias 0.35
    scene.explode(blastAt, params);

    auto velocityOf = [&scene](ecs::Entity e) {
        const btVector3 &v = scene.registry().get<ecs::Physics>(e).body->getLinearVelocity();
        return glm::vec3(v.x(), v.y(), v.z());
    };
    glm::vec3 vLight = velocityOf(light);
    glm::vec3 vHeavy = velocityOf(heavy);
    glm::vec3 vFar = velocityOf(far);

    printf("  blast: light |v|=%.2f heavy |v|=%.2f far |v|=%.2f\n",
           glm::length(vLight), glm::length(vHeavy), glm::length(vFar));

    // The regression this guards: the blast used to hand Bullet a flat impulse,
    // so the velocity it produced was divided by mass and these two differed by
    // 100x — one number could not be right for the player and for debris.
    CHECK(std::fabs(glm::length(vLight) - glm::length(vHeavy)) < 0.01f);

    // Expected magnitude at d=2, r=6: speed * (1 - (2/6)^2). Read from params
    // rather than written out, so retuning the blast doesn't fail the curve.
    const float expected = params.speed * (1.0f - (2.0f / 6.0f) * (2.0f / 6.0f));
    CHECK(std::fabs(glm::length(vLight) - expected) < 0.01f);

    // Falloff: four times the distance, much less push, still non-zero.
    CHECK(glm::length(vFar) < glm::length(vLight));
    CHECK(glm::length(vFar) > 0.0f);

    // Pushed away from the centre, and tilted upward even though both cubes sit
    // at exactly the blast's own height — this is what makes a floor-level
    // blast beside you a launch instead of a shove into the floor.
    CHECK(vLight.x > 0.0f);
    CHECK(vHeavy.x < 0.0f);
    CHECK(vLight.y > 0.0f);
    CHECK(vHeavy.y > 0.0f);

    // Nothing outside the radius is touched at all.
    ecs::Entity outside = ecs::createCube(scene.registry(), &physics, 1,
                                          glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 50.0f)),
                                          glm::vec4(1.0f), 1.0f);
    scene.explode(blastAt, params);
    CHECK(glm::length(velocityOf(outside)) == 0.0f);
}

// The rocket jump end to end: the real player_movement.cow driving the real
// player capsule, launched by a real blast. Both things this checks are
// properties of the *pair* — the blast can only launch the player as far as the
// movement script lets it keep.
static void testRocketJump()
{
    PhysicsWorld physics;
    Scene scene;
    // A bare floor. populateDefault drops loose cubes down the Y axis, and they
    // would land on the player in the middle of a measurement.
    ecs::createPlane(scene.registry(), nullptr, 1000, 1000, glm::mat4(1.0f), glm::vec4(1.0f), 0.0f);
    scene.addRigidBodiesToWorld(physics);

    Camera camera(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    ecs::Entity player = ecs::createPlayer(scene.registry(), &physics, &camera,
                                           glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    scene.registry().get<ecs::Identity>(player).scriptPaths = {"scripts/player_movement.cow"};
    auto &input = scene.registry().emplace<ecs::PlayerInput>(player);

    double t = 0.0;
    ScriptHost host;
    host.setContext(&scene, nullptr);
    host.setTime(t);
    host.setDelta(0.0);
    scene.loadScripts(host);
    scene.startScripts(host);

    const float dt = 1.0f / 60.0f;
    auto *body = scene.registry().get<ecs::Physics>(player).body.get();
    auto posY = [&] { return body->getWorldTransform().getOrigin().y(); };
    auto horizontalSpeed = [&] {
        const btVector3 &v = body->getLinearVelocity();
        return std::sqrt(v.x() * v.x() + v.z() * v.z());
    };
    auto settle = [&] {
        input.keys = 0;
        for (int i = 0; i < 60; ++i)
            step(scene, physics, host, t, dt);
    };

    // Baseline: how high the plain jump in player_movement.cow reaches.
    settle();
    float ground = posY();
    input.keys = 1ull << ecs::inputKeyBit("space");
    step(scene, physics, host, t, dt);
    input.keys = 0;
    float jumpPeak = ground;
    for (int i = 0; i < 120; ++i)
    {
        step(scene, physics, host, t, dt);
        if (posY() > jumpPeak)
            jumpPeak = posY();
    }
    const float jumpHeight = jumpPeak - ground;

    // The rocket jump: a blast at the player's feet, no jump key at all.
    settle();
    ground = posY();
    PhysicsWorld::BlastParams params;
    scene.explode(glm::vec3(0.0f, ground - 1.0f, 0.0f), params);
    float blastPeak = ground;
    for (int i = 0; i < 240; ++i)
    {
        step(scene, physics, host, t, dt);
        if (posY() > blastPeak)
            blastPeak = posY();
    }
    const float blastHeight = blastPeak - ground;

    printf("  jump height = %.2f m, rocket jump = %.2f m (%.1fx)\n",
           jumpHeight, blastHeight, blastHeight / jumpHeight);
    CHECK(jumpHeight > 2.0f && jumpHeight < 3.5f); // the plain jump still works
    CHECK(blastHeight > 3.0f * jumpHeight);        // and the blast clearly beats it

    // Horizontal momentum has to survive the flight. The movement script caps
    // what the *keys* can reach at max_speed (10), and the regression being
    // guarded is the script treating that as a ceiling on the body itself and
    // braking a launch back down to a walk in mid-air.
    settle();
    scene.explode(glm::vec3(3.0f, posY(), 0.0f), params); // off to one side
    // Measured once clear of the ground: the grounded branch legitimately
    // applies friction-fighting steering, and for a frame or two after the
    // blast the ground ray still hits.
    for (int i = 0; i < 10; ++i)
        step(scene, physics, host, t, dt);
    const float launchSpeed = horizontalSpeed();
    // Sampled again while still airborne. A sideways blast is mostly sideways,
    // so this one only buys about half a second of hang time.
    for (int i = 0; i < 12; ++i) // no keys held throughout
        step(scene, physics, host, t, dt);
    const float midflightSpeed = horizontalSpeed();

    printf("  launched sideways at %.2f m/s, still %.2f m/s later in the arc\n",
           launchSpeed, midflightSpeed);
    CHECK(launchSpeed > 12.0f);                  // the sideways push landed
    CHECK(midflightSpeed > launchSpeed - 0.01f); // and nothing bled it off

    // Landing must not scrub that speed off instantly. Before land_grace the
    // grounded branch braked anything over the cap at the full ground
    // acceleration — 100 m/s² on top of friction, which stopped a 17 m/s slide
    // dead in about an eighth of a second and made chaining jumps impossible.
    //
    // Sampled inside the window, because friction is deliberately left alone:
    // the capsule's own μ≈1 against gravity bleeds ~19.6 m/s per second no
    // matter what the script does, so a slide always ends within about a second
    // and a late sample would read zero either way. What is under test is that
    // the *script* has stopped adding a brake of its own on top.
    float prevY = posY();
    bool landed = false;
    for (int i = 0; i < 120 && !landed; ++i)
    {
        step(scene, physics, host, t, dt);
        if (posY() >= prevY - 1e-4f) // stopped descending: touchdown
            landed = true;
        prevY = posY();
    }
    CHECK(landed);
    const float touchdownSpeed = horizontalSpeed();
    for (int i = 0; i < 12; ++i) // 0.2 s, inside the 0.35 s window
        step(scene, physics, host, t, dt);
    const float slideSpeed = horizontalSpeed();

    printf("  touched down at %.2f m/s, still %.2f m/s 0.2 s into the slide\n",
           touchdownSpeed, slideSpeed);
    // Landing used to cost ~6 m/s in the contact frame alone, on top of the
    // script braking the rest away within an eighth of a second.
    CHECK(touchdownSpeed > 15.0f);
    CHECK(slideSpeed > 12.0f);

    // ...and the window has to shut again. Both halves of it — the momentum
    // rule and the lowered friction — are gated on being over the cap, so a
    // player who keeps no keys held must still come to a stop rather than
    // sliding forever on ice.
    for (int i = 0; i < 180; ++i) // 3 s, no keys held
        step(scene, physics, host, t, dt);
    printf("  3 s later, at rest: %.2f m/s\n", horizontalSpeed());
    CHECK(horizontalSpeed() < 0.5f);
}

int main()
{
    testShotIsCentred();
    testBlast();
    testRocketJump();
    testButton();
    testOtherPlates("scripts/plate_spawn.cow", "CUBE DISPENSER", true, false);
    testOtherPlates("scripts/plate_reset.cow", "RESET SCENE", false, true);
    testMultiEntitySpare();

    PhysicsWorld physics;
    Scene scene;
    scene.populateDefault();
    scene.addRigidBodiesToWorld(physics);

    ScriptHost host;
    host.setContext(&scene, nullptr);

    // A driver entity standing in for the player: it fires one cow per call to
    // its `shoot` flag, the same way shoot_cow.cow does on a key edge.
    ecs::Entity driver = scene.createEmpty("Driver", glm::mat4(1.0f));
    auto &ident = scene.registry().get<ecs::Identity>(driver);
    ident.scriptPaths = {"scripts/test_shooter.cow"};

    // Written next to the scripts the engine searches so readScriptFile finds it.
    {
        FILE *f = std::fopen("scripts/test_shooter.cow", "w");
        CHECK(f != nullptr);
        if (!f)
            return 1;
        std::fputs(
            "let fired = 0\n"
            "let next_shot = 0\n"
            "on update(dt) {\n"
            "    if (fired < 3 and time() >= next_shot) {\n"
            // Each cow gets clear air, because despawn_after.cow now ends a cow
            // on impact as well as on the clock and this section is about the
            // clock. High: from 5 m they would all hit the ground within a
            // second; from 500 m they are still ~86 m up when the last expires.
            // Spread out: these are unscaled cows, 10.44 m of mesh each, and
            // dropped down a single column they are barely a second apart in
            // free fall — the second cow spawns straight into the first.
            "        let cow = spawn_cow(fired * 50, 500, 0)\n"
            "        attach_script(cow, \"scripts/despawn_after.cow\")\n"
            "        fired = fired + 1\n"
            "        next_shot = time() + 1\n"
            "    }\n"
            "}\n", f);
        std::fclose(f);
    }

    double t = 0.0;
    host.setTime(t);
    host.setDelta(0.0);
    scene.loadScripts(host);
    scene.startScripts(host);

    const float dt = 0.1f;

    // 0.0s: first shot. 1.0s: second. 2.0s: third. despawn_after.cow gives each
    // cow 4 seconds from spawn if it hits nothing, so all three are airborne
    // together at t≈2.5 — the whole point of moving the lifetime onto the cow.
    for (int i = 0; i < 25; ++i)
        step(scene, physics, host, t, dt);
    printf("  t=%.1f live cows = %d (expect 3 coexisting)\n", t, liveCows(scene));
    CHECK(liveCows(scene) == 3);

    // Each cow got its own compiled script and its own on start().
    {
        int scripted = 0;
        for (auto e : scene.registry().view<ecs::ScriptComponent>())
        {
            const auto &sc = scene.registry().get<ecs::ScriptComponent>(e);
            for (const auto &inst : sc.scripts)
                if (inst.path == "scripts/despawn_after.cow")
                {
                    CHECK(inst.started); // start() ran even though it was attached late
                    ++scripted;
                }
        }
        printf("  cows carrying despawn_after.cow = %d\n", scripted);
        CHECK(scripted == 3);
        // Recorded on Identity too, so a scene save round-trips the attachment.
        for (auto e : scene.registry().view<ecs::Identity>())
        {
            const auto &id = scene.registry().get<ecs::Identity>(e);
            if (id.meshPath.find("cow") != std::string::npos)
                CHECK(id.scriptPaths.size() == 1 && id.scriptPaths[0] == "scripts/despawn_after.cow");
        }
    }

    // Past the first cow's 4s lifetime but not the third's: they expire in the
    // order they were fired, one at a time, rather than all at once.
    for (int i = 0; i < 20; ++i) // t -> 4.5
        step(scene, physics, host, t, dt);
    printf("  t=%.1f live cows = %d (expect 2)\n", t, liveCows(scene));
    CHECK(liveCows(scene) == 2);

    for (int i = 0; i < 10; ++i) // t -> 5.5
        step(scene, physics, host, t, dt);
    printf("  t=%.1f live cows = %d (expect 1)\n", t, liveCows(scene));
    CHECK(liveCows(scene) == 1);

    for (int i = 0; i < 10; ++i) // t -> 6.5
        step(scene, physics, host, t, dt);
    printf("  t=%.1f live cows = %d (expect 0)\n", t, liveCows(scene));
    CHECK(liveCows(scene) == 0);

    // The driver survived its cows destroying themselves mid-iteration.
    CHECK(scene.registry().valid(driver));
    CHECK(scene.registry().all_of<ecs::ScriptComponent>(driver));

    std::remove("scripts/test_shooter.cow");
    printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
