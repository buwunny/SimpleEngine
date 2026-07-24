#ifndef ECS_COMPONENTS_HPP
#define ECS_COMPONENTS_HPP

#include "meshes/Mesh.hpp"

#include <glm/glm.hpp>
#include <btBulletDynamicsCommon.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Camera;

namespace ecs
{
    // Position in world space, Euler degrees (Rz*Ry*Rx convention — matches
    // ImGuizmo), and per-axis local scale. Cached model + modelNoScale are
    // rebuilt by Transform::setTRS() and by PhysicsSyncSystem each frame.
    struct Transform
    {
        glm::dvec3 position{0.0};
        glm::dvec3 rotation{0.0}; // degrees
        glm::dvec3 scale{1.0};
        glm::vec3 localScale{1.0f};
        glm::mat4 model{1.0f};
        glm::mat4 modelNoScale{1.0f};
    };

    struct Renderable
    {
        std::shared_ptr<Mesh> mesh;
        glm::vec4 color{1.0f};
        double lineWidth = 1.0;
        bool wireframe = false;
    };

    // Bullet-owned resources for a single body. Ownership is RAII: removing
    // the component (or destroying the entity) destroys the rigid body.
    // Removal from the dynamics world must happen *before* destruction —
    // Scene::destroyEntity handles that.
    struct Physics
    {
        std::unique_ptr<btRigidBody> body;
        std::unique_ptr<btCollisionShape> shape;
        std::unique_ptr<btMotionState> motion;
        double mass = 0.0;
    };

    struct Identity
    {
        int id = 0;
        std::string name;
        // Zero or more .cow scripts driving this entity. Every script's
        // `on start`/`on update` runs, in order, sharing the same `self`.
        std::vector<std::string> scriptPaths;
        std::string meshPath;
    };

    // Stable type marker for serialization + editor display. The marker
    // also captures the per-shape parameters we need to round-trip
    // (length/width for planes, size for cubes).
    enum class ShapeKind
    {
        Cube,
        Plane,
        Static,
        Player,
    };

    struct ShapeMarker
    {
        ShapeKind kind = ShapeKind::Static;
        int cubeSize = 0;       // Cube only
        float planeLength = 0.f; // Plane only
        float planeWidth = 0.f;  // Plane only
    };

    namespace cowscript_fwd { class Script; }
}

// Forward-declare the script type without pulling the header into every TU
// that includes Components.hpp.
namespace cowscript { class Script; }

namespace ecs
{
    // One compiled script plus the path it came from (kept for diagnostics).
    struct ScriptInstance
    {
        std::string path;
        std::shared_ptr<cowscript::Script> script;
        // Whether on start() has run. Scripts attached mid-session (attach_script)
        // miss the one startScripts() call, so updateScripts() starts any instance
        // that is still false before its first update.
        bool started = false;
    };

    // Holds every compiled script attached to an entity, parallel to (but
    // possibly shorter than, if some failed to compile) Identity::scriptPaths.
    struct ScriptComponent
    {
        std::vector<ScriptInstance> scripts;
    };

    // Drives WSAD/jump/mouse-look on the entity that has one. The body's
    // collision shape is a btCapsuleShape; rotation is locked in factory.
    struct PlayerController
    {
        Camera *camera = nullptr;
        float movementSpeed = 100.0f;
        float mass = 10.0f;
        float lastX = 0.f;
        float lastY = 0.f;
        bool firstMouse = true;
        bool lastTabPressed = false;
        float pendingMouseDx = 0.f;
        float pendingMouseDy = 0.f;
    };

    // Per-tick input for a player-controlled entity. On the client this is
    // filled from the local Window by LocalInputSystem; on the headless server
    // it will be filled from the network. `key()` in .cow reads this via
    // ScriptHost, so the same movement script drives client and server.
    // `keys` is a bitmask over ecs::kInputKeyNames (see ecs/InputKeys.hpp).
    struct PlayerInput
    {
        uint64_t keys = 0;      // pressed-key bitmask, indexed by kInputKeyNames
        float lookYaw = 0.0f;   // absolute camera yaw (degrees)
        float lookPitch = 0.0f; // absolute camera pitch (degrees)
        uint32_t sequence = 0;  // monotonic per-tick input sequence number
    };

    // A camera-facing text label floating above an entity — player nametags in
    // multiplayer, and available to anything else that wants one. Drawn by
    // ecs::nametagSystem; an empty `text` draws nothing. Purely visual: no
    // physics, no serialization, and never present on the headless server.
    struct Nametag
    {
        std::string text;
        float offset = 1.6f;      // world units above the entity's origin
        float size = 0.32f;       // em height in world units at close range
        glm::vec4 color{1.0f};
    };

    // The expanding ring an explosion leaves behind, driven by ecs::blastVfxSystem
    // (which also destroys the entity once `age` passes `life`).
    //
    // Purely cosmetic, and deliberately outside every other pipeline: no physics
    // body, never serialized with the scene, and never created on the headless
    // server. A blast that mattered to the simulation would have to be replicated
    // as state; this one is re-derived on each client from the Explosion event.
    struct BlastVfx
    {
        float age = 0.0f;
        float life = 0.35f;   // seconds from detonation to gone
        float radius = 6.0f;  // radius the ring expands to — the blast's own
        glm::vec4 color{1.0f, 0.72f, 0.25f, 1.0f};
    };

    // Network replication id. On the server, every replicated entity carries one
    // (scene dynamic objects use their Identity.id; players and spawned objects
    // use high id ranges — see net::kPlayerNetIdBase / kSpawnNetIdBase). Snapshots
    // and spawn/despawn events are keyed by this id.
    struct NetId
    {
        uint32_t id = 0;
    };

    // Tags. Stored as empty components so views can filter on them.
    struct Selected
    {
    };
    struct Hovered
    {
    };
    struct PlayerTag
    {
    };
    // Marks the single player entity this client owns: it drives the local
    // Camera and is fed by LocalInputSystem. Remote avatars carry PlayerTag
    // (and PlayerInput, filled from the network) but never LocalPlayer.
    struct LocalPlayer
    {
    };
}

#endif // ECS_COMPONENTS_HPP
