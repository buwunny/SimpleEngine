#include "app/Application.hpp"
#include "ecs/Components.hpp"
#include "ecs/systems/PlayerInputSystem.hpp"
#include "ecs/systems/LocalInputSystem.hpp"
#include "ecs/systems/RenderSystem.hpp"
#include "ecs/systems/NametagSystem.hpp"
#include "ecs/systems/BlastVfxSystem.hpp"
#ifdef __EMSCRIPTEN__
#include "net/WebClientTransport.hpp"
#include "net/NetClient.hpp"
#endif
#include "render/VfxSettings.hpp"
#if ENGINE_WITH_EDITOR
#include "platform/ImGuiLayer.hpp"
#include "app/EditorUI.hpp"
#include <imgui.h>
#endif
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <iomanip>
#include <cstdlib>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

// Accumulated mouse delta from mousemove events (pointer-lock path).
// emscripten_get_mouse_status() returns stale movementX/Y between events,
// causing the camera to keep rotating after the mouse stops. Instead we
// subscribe to the event and accumulate here; the game loop consumes and
// resets each frame.
static double s_mouseAccumDX = 0.0;
static double s_mouseAccumDY = 0.0;

static EM_BOOL app_mousemove_callback(int, const EmscriptenMouseEvent *e, void *)
{
    if (e)
    {
        s_mouseAccumDX += e->movementX;
        s_mouseAccumDY += e->movementY;
    }
    return EM_FALSE; // don't consume — let ImGui's handler still fire
}
#endif

Application::Application()
{
}

Application::~Application()
{
    if (gameDepth)
        glDeleteRenderbuffers(1, &gameDepth);
    if (gameColor)
        glDeleteTextures(1, &gameColor);
    if (gameFbo)
        glDeleteFramebuffers(1, &gameFbo);
    delete shader;
    delete scene;
    delete camera;
    delete window;
    delete physics;
#if ENGINE_WITH_EDITOR
    delete imguiLayer;
    delete editorUI;
    delete editorInput;
    delete colliderDebug;
#else
    delete gameMenu;
#endif
    delete scriptHost;
    delete postfx;
    delete text;
#ifdef __EMSCRIPTEN__
    delete netClient_;
    delete netTransport_;
#endif
}

// One frame's explosion lights, gathered once and handed to both the scene
// shader and the sky pass. Zero-length whenever the effect is off or nothing is
// currently exploding, which is the overwhelmingly common case — the shaders
// skip their light loops entirely on a count of 0.
struct FrameBlastLights
{
    glm::vec4 posRadius[editor::kMaxBlastLights];
    glm::vec4 colorIntensity[editor::kMaxBlastLights];
    int count = 0;

    PostFX::BlastLights forPostFX() const
    {
        return PostFX::BlastLights{posRadius, colorIntensity, count};
    }
};

static FrameBlastLights gatherBlastLights(ecs::Registry &reg, const editor::VFX &vfx)
{
    FrameBlastLights out;
    if (!vfx.explosionLightEnabled)
        return out;
    out.count = ecs::collectBlastLights(reg, out.posRadius, out.colorIntensity,
                                        editor::kMaxBlastLights,
                                        vfx.explosionLightReach,
                                        vfx.explosionLightIntensity);
    return out;
}

// Push VFX uniforms (fog, neon intensity, camera pos) into the main scene
// shader. Called once per render to avoid redundant setUniform churn inside
// renderSystem. Safe to call any time after shader->use().
static void applyVfxToSceneShader(Shader &shader, const glm::vec3 &camPos,
                                  const editor::VFX &vfx,
                                  const FrameBlastLights &lights)
{
    shader.setVec3("uCamPos", camPos);
    shader.setInt("uFogEnabled", vfx.fogEnabled ? 1 : 0);
    shader.setVec3("uFogColor", vfx.fogColor);
    shader.setFloat("uFogStart", vfx.fogStart);
    shader.setFloat("uFogEnd", vfx.fogEnd);
    shader.setFloat("uNeonIntensity", vfx.neonEnabled ? vfx.neonIntensity : 1.0f);

    shader.setInt("uBlastLightCount", lights.count);
    if (lights.count > 0)
    {
        shader.setVec4Array("uBlastLightPos", lights.posRadius, lights.count);
        shader.setVec4Array("uBlastLightColor", lights.colorIntensity, lights.count);
    }
}

void Application::init()
{
    physics = new PhysicsWorld();
    window = new Window(1920, 1080, "CowEngine");
    camera = new Camera(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    scene = new Scene();
    bool sceneLoaded = false;
#ifdef __EMSCRIPTEN__
    // Restore script/model files from localStorage into the in-memory FS so that
    // any subsequent script/mesh loads pick up the latest edits or, for game
    // builds, the assets baked into the HTML by GameBuilder.
    Scene::restoreAssetsFromLocalStorage();

    // For game builds: kGameHtmlTemplate writes the exported scene to localStorage
    // ('cowengine_save') synchronously before CowEngine.js loads, so we find it here.
    // For editor builds: this restores the scene from the last editor save.
    // A multiplayer session reads its own key (see Scene::storageKey) and so
    // normally finds nothing here, falling through to the baked scenes/scene.json
    // that the server loaded too — the two worlds have to agree.
    const std::string saveKey = Scene::storageKey("save");
    EM_ASM({
        var data = localStorage.getItem(UTF8ToString($0));
        if (data)
        {
            Module.ccall('app_set_has_local_storage_data', null, ['number'], [1]);
            Module.ccall('app_set_saved_data', null, ['string'], [data]);
        }
    }, saveKey.c_str());
    if (hasLocalStorageData)
        sceneLoaded = scene->loadFromString(pendingLocalStorageData);
#endif
    if (!sceneLoaded && !scene->loadFromJSON("scenes/scene.json"))
        scene->populateDefault();
    scene->addRigidBodiesToWorld(*physics);

    scene->addPlayer(camera, glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 3.0f, 10.0f)), window, *physics);
    if (scene->hasPlayer())
        scene->registry().get<ecs::Identity>(scene->getPlayerEntity()).scriptPaths = {"scripts/player_movement.cow", "scripts/shoot_cow.cow"};

    // Ensure camera is positioned to match the player's initial transform on web builds
    camera->setPosition(glm::vec3(0.0f, 3.0f, 10.0f));

    shader = new Shader("./shaders/vertex.glsl", "./shaders/fragment.glsl");
    postfx = new PostFX();

    text = new TextRenderer();
    text->init("engine_assets/fonts/JetBrainsMono-2.304/fonts/ttf/JetBrainsMono-Regular.ttf");

#if !ENGINE_WITH_EDITOR
    // Standalone runtime: no editor UI, and no ImGui at all. Keyboard input is
    // served by Window's own backend (GLFW on desktop, a native emscripten
    // keydown/keyup handler on web), so no ImGui frame lifecycle is required.
    window->setCursorDisabled(true);

    // The settings the player last chose, before the first frame is drawn —
    // booting into the defaults and then snapping to their choices would be
    // visible. A first run finds nothing stored and keeps the defaults.
    gameMenu = new GameMenu();
    GameMenu::loadSettings(gameVfx);

    scriptHost = new ScriptHost();
    scriptHost->setContext(scene, window);
    scriptHost->setGlobalKeyQuery([this](std::string_view name)
                                  { return ecs::localKeyPressed(window, name); });
    scriptTime = 0.0;
    scene->loadScripts(*scriptHost);
    scriptHost->setTime(0.0);
    scriptHost->setDelta(0.0);
    scene->startScripts(*scriptHost);

#ifdef __EMSCRIPTEN__
    // If a multiplayer server was configured (window.CowNet resolved a wt/ws
    // URL), connect and drive the netcode. Single-player otherwise.
    if (net::WebClientTransport::serverConfigured() && scene->hasPlayer())
    {
        // Server-authoritative world: don't spawn objects locally, and let
        // NetClient claim the dynamic scene bodies (stop simulating them here).
        scriptHost->setSpawnEnabled(false);
        netTransport_ = new net::WebClientTransport();
        netTransport_->connect();
        netClient_ = new net::NetClient(netTransport_, scene, scene->getPlayerEntity(),
                                        net::WebClientTransport::playerName());
    }
#endif
#else
    imguiLayer = new ImGuiLayer(window);
    editorUI = new EditorUI();
    editorUI->setCamera(camera);
    editorInput = new InputHandler(camera);

    // Render collider wireframes for the selected object via Bullet's debug-draw hook.
    colliderDebug = new ColliderDebugDrawer();
    colliderDebug->setDebugMode(btIDebugDraw::DBG_DrawWireframe);
    physics->getWorld()->setDebugDrawer(colliderDebug);

    scriptHost = new ScriptHost();
    scriptHost->setContext(scene, window);
    scriptHost->setGlobalKeyQuery([this](std::string_view name)
                                  { return ecs::localKeyPressed(window, name); });
    scriptHost->setLogger([this](const std::string &line)
                          {
        if (editorUI)
            editorUI->addLog("[cow] " + line, ImVec4(0.7f, 0.95f, 0.7f, 1.0f)); });
    editorUI->setScriptHost(scriptHost);
#endif

#ifdef __EMSCRIPTEN__
    // Register the per-frame mouse-delta accumulator. Registered after ImGui's
    // backend so both handlers fire; we return EM_FALSE to let it propagate.
    emscripten_set_mousemove_callback("#canvas", nullptr, EM_TRUE, app_mousemove_callback);
    lastFrame = emscripten_get_now() / 1000.0;
#else
    lastFrame = glfwGetTime();
#endif
}

static double getTimeSeconds()
{
#ifdef __EMSCRIPTEN__
    return emscripten_get_now() / 1000.0;
#else
    return glfwGetTime();
#endif
}

void Application::tick()
{
    double current = getTimeSeconds();
    float delta = static_cast<float>(current - lastFrame);
    lastFrame = current;

    // FPS bookkeeping
    fpsCount++;
    fpsTimer += delta;
    if (fpsTimer >= 0.5)
    {
        displayFps = static_cast<float>(fpsCount / fpsTimer);
        fpsCount = 0;
        fpsTimer = 0.0;
    }

    // Explosion rings, on real frame time rather than the fixed sim step, and
    // outside advanceSim on purpose: they're animation with no bearing on the
    // simulation, and in the editor advanceSim only runs while testing — a ring
    // left over from the moment testing stopped would otherwise hang in the
    // scene forever instead of expiring.
    ecs::blastVfxSystem(scene->registry(), delta);
#if !ENGINE_WITH_EDITOR
    {
        int width = 0, height = 0;
#ifdef __EMSCRIPTEN__
        emscripten_get_canvas_element_size("canvas", &width, &height);
#else
        glfwGetFramebufferSize(window->getWindow(), &width, &height);
#endif
        if (width < 1)
            width = 1;
        if (height < 1)
            height = 1;

        // Escape opens the settings menu rather than quitting outright — Quit
        // is a row in it now, so the key that used to end the game without
        // warning asks first.
        const bool menuActive = gameMenu && gameMenu->update(*window, gameVfx, delta);
        if (gameMenu && gameMenu->quitRequested())
        {
            window->close();
            return;
        }

        // Gameplay is frozen while the menu is up. Skipping advanceSim rather
        // than merely ignoring input is deliberate: the menu is modal, and a
        // player who opens it mid-flight should not land — or be shot — while
        // reading it.
        if (!menuActive)
        {
            // Fixed-step gameplay (input + physics + scripts); mouse-look and
            // cursor toggle stay per-frame below.
            advanceSim(delta, true);

            ecs::playerInputSystem(scene->registry(), window, physics, delta);
        }

        glm::mat4 view = glm::lookAt(camera->getPosition(), camera->getPosition() + camera->getFront(), camera->getUp());
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 10000.0f);

        const FrameBlastLights blastLights = gatherBlastLights(scene->registry(), gameVfx);

        postfx->ensure(width, height, gameVfx.quality);
        postfx->beginSceneCapture();
        postfx->drawBackground(view, projection, camera->getPosition(), gameVfx,
                               blastLights.forPostFX());

        glEnable(GL_DEPTH_TEST);
        ecs::setWireframeFillEnabled(gameVfx.wireframeFill);
        shader->use();
        shader->setViewMatrix(view);
        shader->setProjectionMatrix(projection);
        applyVfxToSceneShader(*shader, camera->getPosition(), gameVfx, blastLights);

        scene->syncFromPhysics();
        scene->render(*window, *shader);
        if (text)
            ecs::nametagSystem(scene->registry(), *text, view, projection, camera->getPosition());

        postfx->compositeTo(0, 0, 0, width, height, gameVfx, static_cast<float>(scriptTime));

        text->drawScreen("FPS: " + std::to_string(static_cast<int>(displayFps)), 10.0f, 10.0f, 16.0f, glm::vec4(1.0f), width, height);

        // Last, over the finished frame — including the post-process, so the
        // menu is never itself bloomed or scanlined.
        if (gameMenu)
            gameMenu->render(*text, gameVfx, width, height);

        window->update();
        return;
    }
#else

    // Hot-reload
    scene->checkReload();

    bool testingMode = editorUI && editorUI->isTestingMode();
    if (testingMode != lastTestingMode)
    {

        editorUI->clearSelection();
        scene->setSelectedEntity(ecs::NullEntity);
        scene->setHoveredEntity(ecs::NullEntity);
        if (testingMode)
        {
            reloadScripts();
        }
        else
        {
            scene->forceReload();
            scene->resetScripts();
            // Pointer-lock during testing can cause key-up events to be missed, leaving
            // ImGui's key state stale. Clear it so editor shortcuts work immediately.
            ImGui::GetIO().ClearInputKeys();
        }

        lastTestingMode = testingMode;
    }

    // Computed before the testing step so PlayerInput sampling can be gated on
    // whether the game view (not an editor panel) currently owns input.
    ImGuiIO &io = ImGui::GetIO();
    bool uiCapturing = io.WantCaptureMouse || io.WantCaptureKeyboard;
    bool allowGameInput = editorUI && editorUI->isGameViewInputEnabled();
    bool heiarchyInput = editorUI && editorUI->isHeiarchyInputEnabled();

    if (testingMode)
    {
        // Edge detect reload (R)
        bool r = window->isKeyPressed(GLFW_KEY_R);
        if (r && !lastRPressed)
        {
            scene->forceReload();
            reloadScripts();
        }
        lastRPressed = r;

        editorUI->setRequestedTab(EditorUI::WorkspaceTab::SceneTab);
        // Physics + scripts advance every testing frame; only sample the local
        // keyboard into PlayerInput when the game view owns input, so typing in
        // editor panels doesn't drive the player.
        bool sampleInput = scene->hasPlayer() && (!uiCapturing || allowGameInput);
        advanceSim(delta, sampleInput);
    }

    if (testingMode && scene->hasPlayer() && (!uiCapturing || allowGameInput))
        ecs::playerInputSystem(scene->registry(), window, physics, delta);
    if (!window->isCursorDisabled())
    {
        if (editorInput)
            editorInput->resetFirstMouse();
        if (!testingMode && scene->hasPlayer())
            ecs::playerResetInputState(scene->registry(), scene->getPlayerEntity());
    }

    if (!testingMode && editorInput)
    {
        if (allowGameInput || heiarchyInput)
        {
            // Focus camera on selected object when F is pressed
            ecs::Entity sel = scene->getSelectedEntity();
            if (window->isKeyPressed(GLFW_KEY_F) && sel != ecs::NullEntity)
            {
                if (auto *t = scene->registry().try_get<ecs::Transform>(sel))
                {
                    glm::vec3 targetPos = glm::vec3(t->model[3]);
                    glm::vec3 camDir = glm::normalize(camera->getPosition() - targetPos);
                    camera->setPosition(targetPos + camDir * 5.0f);
                }
            }
        }
        if (allowGameInput)
        {
            editorInput->setMovementSpeed(editorUI->getCameraSpeed());
            editorInput->processInput(window, delta);
            checkSelection();

            // process mouse when in editor mode and game view is focused, only if cursor is disabled (e.g. on web)
            bool cursorNowDisabled = window->isCursorDisabled();
            if (editorUI && editorUI->isGameViewInputEnabled() && cursorNowDisabled)
            {
#ifdef __EMSCRIPTEN__
                // Discard any delta accumulated while the cursor was free so a
                // fresh right-click doesn't snap the camera to a stale position.
                if (!prevCursorDisabled)
                {
                    s_mouseAccumDX = 0.0;
                    s_mouseAccumDY = 0.0;
                }
                // Consume the delta accumulated by app_mousemove_callback this
                // frame. Polling emscripten_get_mouse_status() here would return
                // stale movementX/Y from the last event, making the camera glide
                // after the mouse stops.
                float dx = static_cast<float>(s_mouseAccumDX);
                float dy = static_cast<float>(-s_mouseAccumDY);
                s_mouseAccumDX = 0.0;
                s_mouseAccumDY = 0.0;
                if (dx != 0.0f || dy != 0.0f)
                    editorInput->processMouseDelta(dx, dy);
#else
                double mouseX, mouseY;
                glfwGetCursorPos(window->getWindow(), &mouseX, &mouseY);
                editorInput->processMouse(window->getWindow(), mouseX, mouseY);
#endif
            }
#ifdef __EMSCRIPTEN__
            prevCursorDisabled = cursorNowDisabled;
#endif
        }
    }
    // Resize viewport / compute aspect
    int width = 0, height = 0;
#ifdef __EMSCRIPTEN__
    emscripten_get_canvas_element_size("canvas", &width, &height);
#else
    glfwGetFramebufferSize(window->getWindow(), &width, &height);
#endif
    imguiLayer->newFrame();
    float fps = fpsTimer > 0.0 ? static_cast<float>(fpsCount / fpsTimer) : 0.0f;
    if (editorUI)
        editorUI->render(scene, window, physics, delta, fps);

    float vpX = 0.0f, vpY = 0.0f, vpW = static_cast<float>(width), vpH = static_cast<float>(height);
    float scaleX = 1.0f, scaleY = 1.0f;
    if (editorUI && editorUI->getGameViewport(vpX, vpY, vpW, vpH, scaleX, scaleY))
    {
        vpW *= scaleX;
        vpH *= scaleY;
    }

    int targetW = std::max(1, static_cast<int>(vpW));
    int targetH = std::max(1, static_cast<int>(vpH));
    if (targetW < 2 || targetH < 2)
    {
        if (gameFbWidth > 1 && gameFbHeight > 1)
        {
            targetW = gameFbWidth;
            targetH = gameFbHeight;
        }
        else
        {
            targetW = 2;
            targetH = 2;
        }
    }
    if (gameFbo == 0 || targetW != gameFbWidth || targetH != gameFbHeight)
    {
        if (gameDepth)
            glDeleteRenderbuffers(1, &gameDepth);
        if (gameColor)
            glDeleteTextures(1, &gameColor);
        if (gameFbo)
            glDeleteFramebuffers(1, &gameFbo);

        glGenFramebuffers(1, &gameFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, gameFbo);

        glGenTextures(1, &gameColor);
        glBindTexture(GL_TEXTURE_2D, gameColor);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, targetW, targetH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gameColor, 0);

        glGenRenderbuffers(1, &gameDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, gameDepth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, targetW, targetH);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, gameDepth);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        gameFbWidth = targetW;
        gameFbHeight = targetH;
    }

    if (editorUI && gameColor)
        editorUI->setGameTexture(static_cast<ImTextureID>(static_cast<uintptr_t>(gameColor)),
                                 static_cast<float>(gameFbWidth), static_cast<float>(gameFbHeight));

    glm::mat4 view = glm::lookAt(camera->getPosition(), camera->getPosition() + camera->getFront(), camera->getUp());
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), (float)gameFbWidth / (float)gameFbHeight, 0.1f, 10000.0f);

    const editor::VFX &vfx = editorUI ? editorUI->getVFX() : gameVfx;

    const FrameBlastLights blastLights = gatherBlastLights(scene->registry(), vfx);

    postfx->ensure(gameFbWidth, gameFbHeight, vfx.quality);
    postfx->beginSceneCapture();
    postfx->drawBackground(view, projection, camera->getPosition(), vfx,
                           blastLights.forPostFX());

    glEnable(GL_DEPTH_TEST);
    ecs::setWireframeFillEnabled(vfx.wireframeFill);
    shader->use();
    shader->setViewMatrix(view);
    shader->setProjectionMatrix(projection);
    applyVfxToSceneShader(*shader, camera->getPosition(), vfx, blastLights);

    scene->syncFromPhysics();
    scene->render(*window, *shader);
    if (text)
        ecs::nametagSystem(scene->registry(), *text, view, projection, camera->getPosition());

    // Editor-only: draw the selected object's collider as a wireframe overlay
    // so the user can see what the physics shape actually looks like.
    if (colliderDebug && scene->getSelectedEntity() != ecs::NullEntity && !testingMode && editorUI && editorUI->isColliderVisualizationEnabled())
    {
        auto *p = scene->registry().try_get<ecs::Physics>(scene->getSelectedEntity());
        if (p && p->body && p->shape)
        {
            btTransform trans;
            if (p->body->getMotionState())
                p->body->getMotionState()->getWorldTransform(trans);
            else
                trans = p->body->getWorldTransform();

            colliderDebug->beginFrame();
            physics->getWorld()->debugDrawObject(trans, p->shape.get(), btVector3(0.2f, 1.0f, 0.4f));
            glDisable(GL_DEPTH_TEST);
            window->setLineWidth(2.0f);
            colliderDebug->flush(*shader, glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
            window->setLineWidth(1.0f);
            glEnable(GL_DEPTH_TEST);
        }
    }

    // Bloom + tonemap + composite the captured scene into the gameFbo, whose
    // color attachment is sampled by the ImGui workspace panel.
    postfx->compositeTo(gameFbo, 0, 0, gameFbWidth, gameFbHeight, vfx,
                        static_cast<float>(scriptTime));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    imguiLayer->render();

    window->update();
#endif // ENGINE_WITH_EDITOR
}

void Application::runDesktop()
{
    // Desktop main loop
    while (!window->shouldClose())
    {
        tick();

        // Update window title with FPS periodically
        // Note: fps count/timer already handled in tick; compute instantaneous fps occasionally
        // (Kept minimal to avoid excessive allocations)
        // Sleep/yield not added; rely on vsync or GL swap
    }
}

#if ENGINE_WITH_EDITOR
void Application::checkSelection()
{
    // Send raycast to scene to select objects in the editor when hovering in the game view and left-clicking
    if (editorUI && editorUI->isGameViewInputEnabled() && !window->isCursorDisabled())
    {
        ImVec2 mousePos = ImGui::GetMousePos();

        float gameViewportX, gameViewportY, gameViewportW, gameViewportH, scaleX, scaleY;
        if (editorUI->getGameViewport(gameViewportX, gameViewportY, gameViewportW, gameViewportH, scaleX, scaleY))
        {
            float mouseX = mousePos.x - gameViewportX;
            float mouseY = mousePos.y - gameViewportY;
            // Scale mouse correctly
            float scaledW = gameViewportW * scaleX;
            float scaledH = gameViewportH * scaleY;
            float scaledMouseX = mouseX * scaleX;
            float scaledMouseY = mouseY * scaleY;
            if (mouseX >= 0 && mouseY >= 0 && mouseX <= gameViewportW && mouseY <= gameViewportH)
            {
                // Convert mouse coordinates to normalized device coordinates (-1 to 1)
                float ndcX = (mouseX / gameViewportW) * 2.0f - 1.0f;
                float ndcY = 1.0f - (mouseY / gameViewportH) * 2.0f; // Invert Y for NDC

                // Get camera matrices
                glm::mat4 view = glm::lookAt(camera->getPosition(), camera->getPosition() + camera->getFront(), camera->getUp());
                glm::mat4 projection = glm::perspective(glm::radians(45.0f), scaledW / scaledH, 0.1f, 10000.0f);
                // Unproject screen coordinates to world ray
                glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 rayEye = glm::inverse(projection) * rayClip;
                rayEye.z = -1.0f; // Forward direction in eye space
                rayEye.w = 0.0f;  // Direction vector
                glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(view) * rayEye));

                ecs::Entity hit = scene->raycast(camera->getPosition(), rayWorld, 10000.0f);
                if (hit != ecs::NullEntity)
                {
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        if (hit == scene->getSelectedEntity())
                        {
                            if (!editorUI->isMouseOverGizmo())
                                editorUI->clearSelection();
                        }
                        else
                        {
                            editorUI->setSelection(hit);
                        }
                    }
                }
                else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (!editorUI->isMouseOverGizmo())
                        editorUI->clearSelection();
                }
            }
        }
    }
}
#endif // ENGINE_WITH_EDITOR

void Application::advanceSim(float frameDelta, bool sampleLocalInput)
{
    const double FIXED_DT = 1.0 / 60.0;
    simAccumulator += frameDelta;
    // Clamp to avoid a spiral of death after a long stall (tab backgrounded,
    // breakpoint, etc.): drop the excess rather than trying to catch up.
    if (simAccumulator > 0.25)
        simAccumulator = 0.25;

    while (simAccumulator >= FIXED_DT)
    {
        if (sampleLocalInput)
            ecs::localInputSystem(scene->registry(), window);
        physics->stepSimulation(static_cast<float>(FIXED_DT), 1);
        scriptTime += FIXED_DT;
        scriptHost->setTime(scriptTime);
        scriptHost->setDelta(static_cast<float>(FIXED_DT));
        scene->updateScripts(*scriptHost, static_cast<float>(FIXED_DT));
#ifdef __EMSCRIPTEN__
        // After the local prediction step: send this tick's input, reconcile the
        // local player against server snapshots, and advance remote avatars.
        if (netClient_)
            netClient_->update(static_cast<float>(FIXED_DT));
#endif
        simAccumulator -= FIXED_DT;
    }
}

void Application::reloadScripts()
{
    scriptTime = 0.0;
    scene->resetScripts();
    scene->loadScripts(*scriptHost);
    scriptHost->setTime(0.0);
    scriptHost->setDelta(0.0);
    scene->startScripts(*scriptHost);
}

// Expose a web-friendly tick function when building with Emscripten
#ifdef __EMSCRIPTEN__
static Application *g_app = nullptr;
extern "C"
{
    EMSCRIPTEN_KEEPALIVE
    void app_tick()
    {
        if (g_app)
            g_app->tick();
    }
}
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void app_run_main_loop()
{
    if (g_app)
        emscripten_set_main_loop(app_tick, 0, 1);
}
#endif
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void app_set_global(Application *a)
{
    g_app = a;
}
#endif
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void app_set_has_local_storage_data(int hasData)
{
    if (g_app)
        g_app->setHasLocalStorageData(hasData != 0);
}
#endif
#ifdef __EMSCRIPTEN__
extern "C" EMSCRIPTEN_KEEPALIVE void app_set_saved_data(const char *data)
{
    if (g_app)
        g_app->setPendingLocalStorageData(std::string(data));
}
#endif
#endif