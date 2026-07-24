#ifndef SCENE_HPP
#define SCENE_HPP

#include "ecs/Entity.hpp"
#include "ecs/Components.hpp"
#include "ecs/EntityHandle.hpp"
#include "meshes/AssetManager.hpp"
#include "core/PhysicsWorld.hpp"

class ScriptHost;
class Camera;
class Window;
class Shader;

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

// Scene owns the ECS registry, the player entity handle, and ties everything
// to a PhysicsWorld for body registration. All iteration over entities goes
// through systems in ecs/systems.
class Scene
{
public:
    Scene() = default;
    ~Scene() = default;

    ecs::Registry &registry() { return reg_; }
    const ecs::Registry &registry() const { return reg_; }

    void populateDefault();
    bool loadFromJSON(const std::string &path);
    bool loadFromString(const std::string &jsonData);
    bool saveToJSON(const std::string &path);
    void checkReload();
    void forceReload();

    static void snapshotScriptsToLocalStorage();
    static void snapshotModelsToLocalStorage();
    static void restoreAssetsFromLocalStorage();

    // Full browser-localStorage key for one of the persisted blobs ("save",
    // "scripts", "models"). The editor, the web game and any exported game all
    // share one origin, so the keys are namespaced per session mode: a
    // multiplayer session uses "cowengine_mp_*" and therefore never inherits the
    // scene an editor/singleplayer session left behind. Loading a foreign scene
    // in multiplayer left objects the server has no netId for frozen on screen —
    // rendered, but never simulated or updated. Native builds always get the
    // singleplayer keys (the string is unused off the web).
    static std::string storageKey(const char *name);

    // Returns the entity hit by a world-space ray, or NullEntity if nothing hit.
    ecs::Entity raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance);

    // Set off an explosion at `pos`: knock nearby dynamic bodies away (see
    // PhysicsWorld::applyRadialBlast) and leave a shockwave ring behind.
    //
    // This is the single entry point for a blast wherever it originates — a
    // .cow script calling self_explode, or, on a networked client, an Explosion
    // event replayed from the server. On that client every body except the
    // locally-predicted player is kinematic, and applyRadialBlast skips those,
    // so the same call lands the knockback on exactly the one body the client
    // simulates itself.
    void explode(const glm::vec3 &pos, const PhysicsWorld::BlastParams &params);

    // Notified after every explosion. The server hooks this to replicate the
    // blast to clients; it is left unset in single-player and on the client,
    // where an incoming Explosion must not bounce straight back out.
    using ExplosionObserver = std::function<void(const glm::vec3 &, const PhysicsWorld::BlastParams &)>;
    void setExplosionObserver(ExplosionObserver fn) { onExplosion_ = std::move(fn); }

    // A script has asked for the world to be put back the way it started.
    //
    // Deferred rather than acted on immediately, because a reset destroys every
    // entity in the scene and the request necessarily arrives from a script
    // that is itself running inside the iteration over those entities — the
    // plate would be pulling the floor out from under its own update. The owner
    // of the frame loop consumes it once scripts are done.
    void requestReset() { resetRequested_ = true; }

    // Put the world back to the state it was loaded in.
    //
    // Restores from the JSON captured at load rather than re-reading the file,
    // because an exported web game's scene arrives from localStorage and never
    // exists on disk — forceReload() finds no file there and silently does
    // nothing. Going through the captured copy also makes this a true reset to
    // the starting state rather than to whatever the file says now.
    void resetToInitial();
    bool consumeResetRequest()
    {
        const bool r = resetRequested_;
        resetRequested_ = false;
        return r;
    }

    void addPlayer(Camera *camera, const glm::mat4 &model, Window *window, PhysicsWorld &physics);
    ecs::Entity getPlayerEntity() const { return playerEntity_; }
    bool hasPlayer() const { return playerEntity_ != ecs::NullEntity && reg_.valid(playerEntity_); }
    void removePlayer();

    // Add an already-created entity's rigid body to the physics world (if it
    // has one). Used by spawn paths that create entities without a physics
    // world reference.
    void registerRigidBody(ecs::Entity e);

    // Spawn a fresh entity into the scene + physics world via the appropriate
    // factory. Returns the new entity (NullEntity on failure).
    ecs::Entity spawnCube(int size, const glm::mat4 &model, const glm::vec4 &color, float mass = 1.0f);
    ecs::Entity spawnPlane(float length, float width, const glm::mat4 &model, const glm::vec4 &color, float mass = 0.0f);
    ecs::Entity spawnStaticFromAsset(const std::string &meshPath, const std::string &meshName,
                                     const glm::mat4 &model, const glm::vec4 &color, float mass = 1.0f);

    // Create a blank entity carrying only Identity + Transform. Use the
    // component-ops helpers (ecs::add*) to attach renderable/physics/script
    // afterwards.
    ecs::Entity createEmpty(const std::string &name = "Entity",
                            const glm::mat4 &model = glm::mat4(1.0f));

    // Create a render-only avatar (Transform + Renderable cube, no physics/script)
    // for a remote player. NetClient drives its transform via interpolation.
    ecs::Entity createRemoteAvatar(const glm::vec4 &color);

    // Create a render-only proxy for a server-spawned object (kind: 0=cube,
    // 1=cow, 2=plane). No physics/scripts — NetClient drives its transform from
    // snapshots. Used on networked clients where spawns are server-authoritative.
    ecs::Entity createNetProxy(int kind, const glm::vec4 &color);

    // Give a network-driven entity (avatar/proxy) a *kinematic* collider so the
    // locally-predicted player collides with it, while its motion still comes
    // from snapshots (never simulated). The box is sized from the entity's mesh
    // AABB times `scale` and placed at the entity's current transform, so call
    // it only after the entity has been positioned by its first snapshot.
    void attachNetCollider(ecs::Entity e, const glm::vec3 &scale);

    // Selection / hover are stored as tag components but mirrored here for
    // quick lookup in the editor.
    void setSelectedEntity(ecs::Entity e);
    ecs::Entity getSelectedEntity() const { return selectedEntity_; }
    void setHoveredEntity(ecs::Entity e);
    ecs::Entity getHoveredEntity() const { return hoveredEntity_; }

    const std::string &getScenePath() const { return scenePath_; }

    static Scene *getCurrent() { return s_current; }

    // Register every entity's rigid body with the physics world. Used after
    // loading a scene from disk (factories don't have a world reference at
    // load time — we batch-register here).
    void addRigidBodiesToWorld(PhysicsWorld &physics);

    // Run the per-frame physics → transform sync. Replaces Scene::update().
    void syncFromPhysics();

    int loadScripts(ScriptHost &host);
    void resetScripts();
    void startScripts(ScriptHost &host);
    void updateScripts(ScriptHost &host, float dt);

    void render(Window &window, Shader &shader);
    void renderTransparent(Window &window, Shader &shader);
    void renderFill(Window &window, Shader &shader);

    PhysicsWorld *physicsWorld() { return physicsWorld_; }

    // Destroy an entity, removing its body from physics first.
    void destroyEntity(ecs::Entity e);

    // Iterate entities. Used by EditorUI to populate the hierarchy panel.
    // Excludes the player entity to mirror the old `objects` vector — callers
    // who want it can check getPlayerEntity() separately.
    template <typename Fn>
    void forEachEntity(Fn &&fn)
    {
        auto view = reg_.view<ecs::Identity>();
        for (auto e : view)
        {
            if (e == playerEntity_)
                continue;
            fn(e);
        }
    }

    // Convenience handle wrapper.
    ecs::EntityHandle handle(ecs::Entity e) { return ecs::EntityHandle(&reg_, e); }

private:
    ecs::Registry reg_;
    ecs::Entity playerEntity_ = ecs::NullEntity;
    ecs::Entity selectedEntity_ = ecs::NullEntity;
    ecs::Entity hoveredEntity_ = ecs::NullEntity;

    std::string scenePath_;
    std::filesystem::file_time_type lastWriteTime_;
    PhysicsWorld *physicsWorld_ = nullptr;
    ExplosionObserver onExplosion_;
    bool resetRequested_ = false;
    std::string initialJson_; // the scene as loaded, for resetToInitial()
    std::chrono::steady_clock::time_point lastAutoReloadTime_ = std::chrono::steady_clock::time_point::min();
    std::chrono::milliseconds reloadDebounce_{500};
    static Scene *s_current;
};

#endif // SCENE_HPP
