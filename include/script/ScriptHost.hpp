#ifndef SCRIPT_HOST_HPP
#define SCRIPT_HOST_HPP

#include "script/CowScript.hpp"
#include "ecs/Entity.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Scene;
class Window;

// ScriptHost binds engine state (current `self` entity, key state, time, etc.)
// to the built-in functions that .cow scripts may call. One ScriptHost is
// reused across frames; before invoking a script the engine sets the current
// `self` entity so that `self_*` builtins act on that entity.
class ScriptHost
{
public:
    using LogFn = std::function<void(const std::string &)>;
    // Fallback keyboard query for `key()` on entities that carry no PlayerInput
    // (e.g. non-player scripts like jump_on_space). Wired to the local Window on
    // the client; left unset on the headless server so those scripts see no
    // input there.
    using KeyQueryFn = std::function<bool(std::string_view)>;

    ScriptHost();

    void setContext(Scene *scene, Window *window) { sceneRef = scene; windowRef = window; }
    void setLogger(LogFn fn) { logger = std::move(fn); }
    void setGlobalKeyQuery(KeyQueryFn fn) { globalKeyQuery = std::move(fn); }
    // When false, the builtins that *change the shared world* — spawn_*,
    // destroy, self_explode — do nothing (spawns return an inert handle whose
    // property reads/writes are absorbed). The networked client turns this off
    // so those effects come authoritatively from the server, replicated back,
    // instead of the client also producing its own copy of each one.
    void setSpawnEnabled(bool enabled) { spawnEnabled = enabled; }

    // Execution limits applied to every script compiled through this host --
    // both the ones loadScripts() reads off disk and the ones a running script
    // pulls in with attach_script. Set before scripts are loaded.
    void setScriptLimits(const cowscript::Limits &l) { scriptLimits = l; }
    const cowscript::Limits &limits() const { return scriptLimits; }

    // Ceiling on how many physics bodies may exist before spawn_* starts
    // refusing. Separate from the execution budget because it bounds a
    // *cumulative* resource rather than the work of any single call. 0 = no
    // cap (the editor default -- a local author only hurts themselves).
    void setMaxSpawnedEntities(int n) { maxSpawnedEntities = n; }

    // Limits for a room server running published, untrusted scripts. Much
    // tighter than the editor defaults because here a runaway script costs
    // every other player in the world, not just its author: the server steps
    // 60 times a second and runs every entity's `update` inside each step, so
    // the per-call budget has to leave room for a whole scene's worth of them.
    // Inline (pure policy data, no engine state) so the limits test can assert
    // against the real production values without linking the whole host.
    static cowscript::Limits serverLimits()
    {
        cowscript::Limits l;
        // A 60 Hz tick is 16.6 ms for the *whole* world -- physics, networking
        // and every entity's update() -- so one script getting a slice of that
        // is already generous. 200k steps runs any reasonable per-frame script
        // thousands of times over while capping a runaway one well under a
        // frame.
        l.maxSteps = 200000ull;
        // Deep enough for ordinary helper-function nesting, shallow enough that
        // recursion unwinds long before the native stack does.
        l.maxCallDepth = 32;
        // Wall-clock backstop: even if the step counter under-counts (a slow
        // builtin costs one step no matter what it does), no single call gets
        // to eat more than a quarter of the frame.
        l.maxMillis = 4.0;
        return l;
    }

    void bindBuiltins(cowscript::Script &script);

    void setSelf(ecs::Entity e) { selfEntity = e; }
    ecs::Entity self() const { return selfEntity; }

    void setTime(double t) { timeSeconds = t; }
    void setDelta(double d) { lastDelta = d; }

    void log(const std::string &line);

private:
    cowscript::Value builtinPrint(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinTime(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinDt(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinKey(const std::vector<cowscript::Value> &args);

    cowscript::Value builtinSelfPos(int axis);
    cowscript::Value builtinSelfRot(int axis);
    cowscript::Value builtinSelfScale(int axis);
    cowscript::Value builtinSelfSetPos(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetRot(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetScaleFn(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetColor(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfApplyImpulse(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfApplyForce(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetVelocity(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfOnGround(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetFriction(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfCollided(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfContactAbove(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfSetNametag(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinResetScene(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfExplode(const std::vector<cowscript::Value> &args);

    cowscript::Value builtinSpawn(const std::vector<cowscript::Value> &args, const std::string &kind);
    cowscript::Value builtinDestroy(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinAttachScript(const std::vector<cowscript::Value> &args);

    cowscript::Value builtinTransform(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinRigidbody(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinSelfHandle(const std::vector<cowscript::Value> &args);
    cowscript::Value builtinCamera(const std::vector<cowscript::Value> &args);

    cowscript::Value getProperty(const cowscript::Value &target, const std::string &prop);
    void setProperty(const cowscript::Value &target, const std::string &prop, const cowscript::Value &value);

    Scene *sceneRef = nullptr;
    Window *windowRef = nullptr;
    ecs::Entity selfEntity = ecs::NullEntity;
    double timeSeconds = 0.0;
    double lastDelta = 0.0;
    LogFn logger;
    KeyQueryFn globalKeyQuery;
    bool spawnEnabled = true;
    cowscript::Limits scriptLimits;
    int maxSpawnedEntities = 0;
    bool spawnCapLogged = false; // log the cap once, not 60 times a second
};

#endif // SCRIPT_HOST_HPP
