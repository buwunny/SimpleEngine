#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include <memory>
#include "core/EngineConfig.hpp"
#include "core/PhysicsWorld.hpp"
#include "platform/Window.hpp"
#include "core/Camera.hpp"
#include "core/Scene.hpp"
#include "render/Shader.hpp"
#include "script/ScriptHost.hpp"
#include "render/PostFX.hpp"
#include "render/TextRenderer.hpp"
#include "render/VfxSettings.hpp"

#if ENGINE_WITH_EDITOR
// Editor-only subsystems. In a standalone (ENGINE_WITH_EDITOR=0) build these
// headers — and the ImGui/ImGuizmo dependency they drag in — are never included.
#include "platform/ImGuiLayer.hpp"
#include "app/EditorUI.hpp"
#include "core/InputHandler.hpp"
#include "render/ColliderDebugDrawer.hpp"
#include "editor/EditorContext.hpp"
#else
// The standalone counterpart: the editor configures VFX through its ImGui
// panel, a shipped game through this.
#include "app/GameMenu.hpp"
#endif

namespace net
{
    class WebClientTransport;
    class NetClient;
}

class Application
{
public:
    Application();
    ~Application();

    // Initialize resources (load scene, create player, shader)
    void init();

    // Single tick: update physics, process input, render
    void tick();

    // Desktop run loop
    void runDesktop();

#ifdef __EMSCRIPTEN__
    // flag to see if we have local storage data to load on startup
    void setHasLocalStorageData(bool hasData) { hasLocalStorageData = hasData; };
    void setPendingLocalStorageData(const std::string &data) { pendingLocalStorageData = data; };
#endif

private:
    void reloadScripts();

    // Advance the simulation in fixed 1/60s steps, accumulating real frame
    // time. Runs local input sampling (when sampleLocalInput), physics, and
    // scripts per step so client prediction and the future server tick share
    // one deterministic cadence. Rendering stays per-frame.
    void advanceSim(float frameDelta, bool sampleLocalInput);

    // Carry out a reset_scene() a script asked for, once scripts have finished
    // running for the frame. See Scene::requestReset.
    void applyPendingReset();
    double simAccumulator = 0.0;

    PhysicsWorld *physics = nullptr;
    Window *window = nullptr;
    Camera *camera = nullptr;
    Scene *scene = nullptr;
    Shader *shader = nullptr;
#if ENGINE_WITH_EDITOR
    void checkSelection();

    ImGuiLayer *imguiLayer = nullptr;
    EditorUI *editorUI = nullptr;
    InputHandler *editorInput = nullptr;
    ColliderDebugDrawer *colliderDebug = nullptr;
#else
    GameMenu *gameMenu = nullptr;
#endif
    ScriptHost *scriptHost = nullptr;
    PostFX *postfx = nullptr;
    // Engine text. Drawn inside the scene pass so labels are lit by the same
    // post-processing as everything else; disabled (and a no-op) if the font is
    // missing. Currently feeds multiplayer nametags.
    TextRenderer *text = nullptr;
#ifdef __EMSCRIPTEN__
    // Multiplayer client (web only). Null in single-player or when no server is
    // configured. NetClient sends input, reconciles the local player, and drives
    // interpolated remote avatars; the transport bridges to window.CowNet.
    net::WebClientTransport *netTransport_ = nullptr;
    net::NetClient *netClient_ = nullptr;
#endif
    editor::VFX gameVfx;  // default VFX settings used in standalone game builds
    double scriptTime = 0.0;

    unsigned int gameFbo = 0;
    unsigned int gameColor = 0;
    unsigned int gameDepth = 0;
    int gameFbWidth = 0;
    int gameFbHeight = 0;

    double lastFrame = 0.0;

    // FPS accounting for desktop builds
    int fpsCount = 0;
    double fpsTimer = 0.0;
    float displayFps = 0.0f;
    bool lastRPressed = false;
    bool lastTestingMode = false;
#ifdef __EMSCRIPTEN__
    // flag to see if we have local storage data to load on startup
    bool hasLocalStorageData = false;
    std::string pendingLocalStorageData;
    bool prevCursorDisabled = false;
#endif
};

#endif // APPLICATION_HPP

#ifdef __EMSCRIPTEN__
extern "C"
{
    void app_tick();
    void app_set_global(Application *a);
    void app_run_main_loop();
}
#endif
