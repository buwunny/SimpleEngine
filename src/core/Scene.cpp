#include "core/Scene.hpp"

#include "ecs/Factories.hpp"
#include "ecs/ComponentOps.hpp"
#include "ecs/systems/PhysicsSyncSystem.hpp"
#include "ecs/systems/RenderSystem.hpp"
#include "ecs/systems/ScriptSystem.hpp"
#if !defined(ENGINE_HEADLESS)
#include "ecs/systems/PlayerInputSystem.hpp"
#endif

#include "meshes/AssetManager.hpp"
#include "meshes/CubeMesh.hpp"
#include "meshes/PlaneMesh.hpp"
#if !defined(ENGINE_HEADLESS)
#include "meshes/ShockwaveMesh.hpp"
#endif
#include "script/CowScript.hpp"
#include "script/ScriptHost.hpp"
#include "core/Camera.hpp"
#if !defined(ENGINE_HEADLESS)
#include "platform/Window.hpp"
#endif

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <btBulletDynamicsCommon.h>

#include <limits>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

EM_JS(void, em_local_storage_set, (const char *key, const char *data), {
    try { localStorage.setItem(UTF8ToString(key), UTF8ToString(data)); }
    catch (e) { console.warn('localStorage.setItem failed for', UTF8ToString(key), e); }
});

// True when this page resolved a multiplayer server (see the CowNet shim in
// GameTemplate.html). The editor shell has no CowNet at all, and the join gate's
// ?singleplayer=1 leaves the config empty, so both read as single-player.
EM_JS(int, em_multiplayer_session, (), {
    return (window.CowNet && window.CowNet.hasServer()) ? 1 : 0;
});

EM_JS(void, em_restore_assets_to_fs, (const char *scriptsKey, const char *modelsKey), {
    function restoreKey(key) {
        var raw = null;
        try { raw = localStorage.getItem(key); } catch (e) { return; }
        if (raw == null) return;
        var obj;
        try { obj = JSON.parse(raw); }
        catch (e) { console.warn('cowengine: bad JSON in', key, e); return; }
        if (!obj || typeof obj !== 'object') return;
        for (var p in obj) {
            if (!obj.hasOwnProperty(p)) continue;
            var v = obj[p];
            if (typeof v !== 'string') continue;
            var abs = (p.charAt(0) === "/") ? p : ("/" + p);
            var parts = abs.split("/");
            var dir = "";
            for (var i = 1; i < parts.length - 1; ++i) {
                dir += "/" + parts[i];
                try { FS.mkdir(dir); } catch (e) { /* exists */ }
            }
            try { FS.writeFile(abs, v); }
            catch (e) { console.error("cowengine: FS.writeFile failed for", abs, e); }
        }
    }
    restoreKey(UTF8ToString(scriptsKey));
    restoreKey(UTF8ToString(modelsKey));
});
#endif

using json = nlohmann::json;
Scene *Scene::s_current = nullptr;

namespace
{
    glm::vec3 jsonVec3(const json &j, const std::string &key, glm::vec3 def)
    {
        if (!j.contains(key) || !j[key].is_array())
            return def;
        const auto &a = j[key];
        glm::vec3 v = def;
        for (size_t i = 0; i < std::min<size_t>(3, a.size()); ++i)
            (&v.x)[i] = a[i].get<float>();
        return v;
    }

    glm::vec4 parseColor(const json &j)
    {
        glm::vec4 color = glm::vec4(1.0f);
        if (!j.contains("color"))
            return color;
        const auto &c = j["color"];
        if (c.is_array())
        {
            for (size_t k = 0; k < std::min<size_t>(4, c.size()); ++k)
                (&color.r)[k] = c[k].get<float>();
            return color;
        }
        if (c.is_string())
        {
            std::string s = c.get<std::string>();
            if (!s.empty() && s[0] == '#')
            {
                try
                {
                    std::string hex = s.substr(1);
                    unsigned int v = std::stoul(hex, nullptr, 16);
                    if (hex.size() == 6)
                        color = glm::vec4(((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f, 1.0f);
                    else if (hex.size() == 8)
                        color = glm::vec4(((v >> 24) & 0xFF) / 255.f, ((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f);
                }
                catch (...) {}
            }
        }
        return color;
    }

    std::string colorToString(const glm::vec4 &c)
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
                 int(c.r * 255), int(c.g * 255), int(c.b * 255), int(c.a * 255));
        return std::string(buf);
    }

    glm::mat4 modelFromPRS(const glm::vec3 &pos, const glm::vec3 &rot, const glm::vec3 &scl)
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        m = glm::rotate(m, glm::radians(rot.z), glm::vec3(0, 0, 1));
        m = glm::rotate(m, glm::radians(rot.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(rot.x), glm::vec3(1, 0, 0));
        m = glm::scale(m, scl);
        return m;
    }
}

void Scene::populateDefault()
{
    using namespace ecs;
    createCube(reg_, physicsWorld_, 3, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 15.f, 0.f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.f), 10.f);
    createCube(reg_, physicsWorld_, 2, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 3.f, 0.f)), glm::vec4(0.f, 0.5f, 0.5f, 1.f), 1.f);
    createCube(reg_, physicsWorld_, 2, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 6.f, 0.f)), glm::vec4(0.5f, 0.5f, 0.f, 1.f), 1.f);
    createCube(reg_, physicsWorld_, 2, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 9.f, 0.f)), glm::vec4(0.5f, 0.f, 0.5f, 1.f), 1.f);

    createPlane(reg_, physicsWorld_, 1000, 1000, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 0.f, 0.f)), glm::vec4(0.60f, 0.60f, 0.60f, 1.0f), 0.f);

    createPlane(reg_, physicsWorld_, 50, 45, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.f, 25.f, 27.5f)), glm::radians(90.f), glm::vec3(0, 0, 1)), glm::vec4(0.80f, 0.90f, 0.95f, 1.0f), 0.f);
    createPlane(reg_, physicsWorld_, 50, 45, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.f, 25.f, -27.5f)), glm::radians(90.f), glm::vec3(0, 0, 1)), glm::vec4(0.80f, 0.90f, 0.95f, 1.0f), 0.f);
    createPlane(reg_, physicsWorld_, 40, 10, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(50.f, 30.f, 0.f)), glm::radians(90.f), glm::vec3(0, 0, 1)), glm::vec4(0.85f, 0.85f, 0.90f, 1.0f), 0.f);
    createPlane(reg_, physicsWorld_, 50, 100, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(-50.f, 25.f, 0.f)), glm::radians(90.f), glm::vec3(0, 0, 1)), glm::vec4(0.80f, 0.90f, 0.95f, 1.0f), 0.f);
    createPlane(reg_, physicsWorld_, 100, 50, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 25.f, 50.f)), glm::radians(90.f), glm::vec3(1, 0, 0)), glm::vec4(0.75f, 0.90f, 0.80f, 1.0f), 0.f);
    createPlane(reg_, physicsWorld_, 100, 50, glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 25.f, -50.f)), glm::radians(90.f), glm::vec3(1, 0, 0)), glm::vec4(0.75f, 0.90f, 0.80f, 1.0f), 0.f);

    createCube(reg_, physicsWorld_, 3, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 15.f, 0.f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.f), 10.f);
    createCube(reg_, physicsWorld_, 3, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 15.f, 0.f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.f), 10.f);
    createCube(reg_, physicsWorld_, 3, glm::translate(glm::mat4(1.0f), glm::vec3(0.f, 15.f, 0.f)), glm::vec4(0.5f, 0.5f, 0.5f, 1.f), 10.f);
}

bool Scene::loadFromJSON(const std::string &path)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    candidates.emplace_back(path);
#ifdef ASSET_ROOT
    candidates.emplace_back(fs::path(ASSET_ROOT) / path);
#endif
    candidates.emplace_back(fs::path("./") / path);
    candidates.emplace_back(fs::path("../") / path);
    candidates.emplace_back(fs::path("../../") / path);

    fs::path chosen;
    for (auto &c : candidates)
        if (fs::exists(c)) { chosen = c; break; }
    if (chosen.empty())
    {
        std::cerr << "Scene: JSON file not found: " << path << std::endl;
        return false;
    }

    std::ifstream in(chosen);
    if (!in)
    {
        std::cerr << "Scene: failed to open " << chosen << std::endl;
        return false;
    }
    json j;
    try { in >> j; }
    catch (const std::exception &e)
    {
        std::cerr << "Scene: JSON parse error: " << e.what() << std::endl;
        return false;
    }

    // Snapshot for resetToInitial(). Taken on every successful load, so a
    // reset returns to the most recently loaded world rather than the first.
    initialJson_ = j.dump();

    // Tear down all entities except the player. The player is rebuilt by the
    // caller (Application) after load if needed.
    if (physicsWorld_)
    {
        auto view = reg_.view<ecs::Physics>();
        for (auto e : view)
        {
            if (isSpared(e))
                continue;
            auto &p = view.get<ecs::Physics>(e);
            if (p.body)
                physicsWorld_->removeRigidBody(p.body.get());
        }
    }
    // Destroy non-player entities
    {
        std::vector<ecs::Entity> toDestroy;
        auto view = reg_.view<ecs::Identity>();
        for (auto e : view)
            if (!isSpared(e))
                toDestroy.push_back(e);
        for (auto e : toDestroy)
            reg_.destroy(e);
    }
    selectedEntity_ = ecs::NullEntity;
    hoveredEntity_ = ecs::NullEntity;

    if (j.contains("objects") && j["objects"].is_array())
    {
        for (const auto &obj : j["objects"])
        {
            try
            {
                std::string type = obj.value("type", "StaticObject");

                glm::mat4 model = glm::mat4(1.0f);
                glm::vec3 pos(0.0f), rot(0.0f), scl(1.0f);
                if (obj.contains("model") && obj["model"].is_array() && obj["model"].size() == 16)
                {
                    for (int i = 0; i < 16; ++i)
                        model[i / 4][i % 4] = obj["model"][i].get<float>();
                }
                else
                {
                    pos = jsonVec3(obj, "position", glm::vec3(0));
                    rot = jsonVec3(obj, "rotation", glm::vec3(0));
                    scl = jsonVec3(obj, "scale", glm::vec3(1));
                    model = modelFromPRS(pos, rot, scl);
                }

                glm::vec4 color = parseColor(obj);
                float mass = obj.value("mass", 0.0f);
                std::string name = obj.value("name", "");
                // Accept the new array form ("scripts": [...]) and fall back to
                // the legacy single-string form ("script": "...").
                std::vector<std::string> scriptPaths;
                if (obj.contains("scripts") && obj["scripts"].is_array())
                {
                    for (const auto &sp : obj["scripts"])
                        if (sp.is_string())
                            scriptPaths.push_back(sp.get<std::string>());
                }
                else if (obj.contains("script") && obj["script"].is_string())
                {
                    std::string legacy = obj["script"].get<std::string>();
                    if (!legacy.empty())
                        scriptPaths.push_back(legacy);
                }

                ecs::Entity e = ecs::NullEntity;
                if (type == "Plane")
                {
                    float length = obj.value("length", 100.0f);
                    float width = obj.value("width", 100.0f);
                    e = ecs::createPlane(reg_, physicsWorld_, length, width, model, color, mass);
                }
                else if (type == "Cube")
                {
                    int size = obj.value("size", 1);
                    e = ecs::createCube(reg_, physicsWorld_, size, model, color, mass);
                }
                else
                {
                    std::string meshPath = obj.value("mesh", "");
                    std::string meshName = obj.value("mesh_name", meshPath);
                    auto &am = AssetManager::instance();
                    auto mesh = meshPath.empty() ? nullptr : am.loadStaticMeshFromOBJ(meshPath, meshName);
                    if (mesh)
                    {
                        const auto &verts = mesh->getVertices();
                        const auto &inds = mesh->getIndices();
                        int stride = mesh->getFloatsPerVertex();
                        e = ecs::createStaticObject(reg_, physicsWorld_, mesh, verts.data(),
                                                    verts.size() / stride, inds.data(), inds.size(),
                                                    stride, model, color, mass);
                        if (e != ecs::NullEntity)
                            reg_.get<ecs::Identity>(e).meshPath = meshPath;
                    }
                }

                if (e != ecs::NullEntity)
                {
                    // Honor PRS over the matrix extraction so gimbal-lock
                    // edits in the inspector survive serialization.
                    if (!obj.contains("model"))
                        ecs::applyTransform(reg_, e, pos, rot, scl);
                    if (!name.empty())
                        reg_.get<ecs::Identity>(e).name = name;
                    if (!scriptPaths.empty())
                        reg_.get<ecs::Identity>(e).scriptPaths = scriptPaths;
                }
            }
            catch (const std::exception &ex)
            {
                std::cerr << "Scene: error parsing object: " << ex.what() << std::endl;
            }
        }
    }

#if !defined(ENGINE_HEADLESS)
    if (physicsWorld_ && hasPlayer())
        ecs::playerResetInputState(reg_, playerEntity_);
#endif

    scenePath_ = chosen.string();
    lastWriteTime_ = fs::last_write_time(chosen);
    return true;
}

bool Scene::loadFromString(const std::string &jsonData)
{
    std::string tempPath = "scene.json";
    {
        std::ofstream out(tempPath);
        if (!out) return false;
        out << jsonData;
    }
    bool ok = loadFromJSON(tempPath);
    std::filesystem::remove(tempPath);
    return ok;
}

bool Scene::saveToJSON(const std::string &path)
{
    json j;
    j["objects"] = json::array();

    auto view = reg_.view<ecs::Identity, ecs::Transform, ecs::ShapeMarker>();
    for (auto e : view)
    {
        if (e == playerEntity_)
            continue;
        auto &ident = view.get<ecs::Identity>(e);
        auto &t = view.get<ecs::Transform>(e);
        auto &sm = view.get<ecs::ShapeMarker>(e);

        json obj;
        switch (sm.kind)
        {
            case ecs::ShapeKind::Cube:
                obj["type"] = "Cube";
                obj["size"] = sm.cubeSize;
                break;
            case ecs::ShapeKind::Plane:
                obj["type"] = "Plane";
                obj["length"] = sm.planeLength;
                obj["width"] = sm.planeWidth;
                break;
            case ecs::ShapeKind::Static:
                obj["type"] = "StaticObject";
                if (!ident.meshPath.empty())
                    obj["mesh"] = ident.meshPath;
                break;
            case ecs::ShapeKind::Player:
                continue; // saved via "player" key (currently not round-tripped)
        }

        obj["position"] = {t.position.x, t.position.y, t.position.z};
        obj["rotation"] = {t.rotation.x, t.rotation.y, t.rotation.z};
        obj["scale"] = {t.scale.x, t.scale.y, t.scale.z};

        if (auto *rd = reg_.try_get<ecs::Renderable>(e))
            obj["color"] = colorToString(rd->color);

        obj["name"] = ident.name;
        if (auto *p = reg_.try_get<ecs::Physics>(e))
            obj["mass"] = p->mass;
        if (!ident.scriptPaths.empty())
            obj["scripts"] = ident.scriptPaths;

        j["objects"].push_back(obj);
    }

    if (!scenePath_.empty())
        j["scenePath"] = scenePath_;

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path outPath(path);
    if (outPath.has_parent_path())
        fs::create_directories(outPath.parent_path(), ec);
    std::ofstream out(outPath);
    if (!out)
        return false;
    out << j.dump(4);

#ifdef __EMSCRIPTEN__
    em_local_storage_set(storageKey("save").c_str(), j.dump().c_str());
    snapshotScriptsToLocalStorage();
    snapshotModelsToLocalStorage();
#endif
    return true;
}

namespace
{
#ifdef __EMSCRIPTEN__
    std::string snapshotDirectory(const std::string &root, const std::string &ext)
    {
        namespace fs = std::filesystem;
        json out = json::object();
        std::error_code ec;
        fs::path rootPath(root);
        if (!fs::exists(rootPath, ec))
            return "";
        for (auto it = fs::recursive_directory_iterator(rootPath, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        {
            const fs::directory_entry &entry = *it;
            if (!entry.is_regular_file(ec))
                continue;
            if (entry.path().extension() != ext)
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            if (!in)
                continue;
            std::stringstream ss;
            ss << in.rdbuf();
            fs::path rel = fs::relative(entry.path(), rootPath, ec);
            if (ec)
                continue;
            out[root + "/" + rel.generic_string()] = ss.str();
        }
        return out.dump();
    }
#endif
}

std::string Scene::storageKey(const char *name)
{
#ifdef __EMSCRIPTEN__
    // CowNet resolves its config in an inline script that runs before the wasm
    // module loads, so this answer is fixed for the lifetime of the page.
    static const bool multiplayer = em_multiplayer_session() != 0;
    if (multiplayer)
        return std::string("cowengine_mp_") + name;
#endif
    return std::string("cowengine_") + name;
}

void Scene::snapshotScriptsToLocalStorage()
{
#ifdef __EMSCRIPTEN__
    std::string blob = snapshotDirectory("scripts", ".cow");
    if (!blob.empty())
        em_local_storage_set(storageKey("scripts").c_str(), blob.c_str());
#endif
}

void Scene::snapshotModelsToLocalStorage()
{
#ifdef __EMSCRIPTEN__
    std::string blob = snapshotDirectory("models", ".obj");
    if (!blob.empty())
        em_local_storage_set(storageKey("models").c_str(), blob.c_str());
#endif
}

void Scene::restoreAssetsFromLocalStorage()
{
#ifdef __EMSCRIPTEN__
    em_restore_assets_to_fs(storageKey("scripts").c_str(), storageKey("models").c_str());
#endif
}

void Scene::checkReload()
{
    if (scenePath_.empty())
        return;
    namespace fs = std::filesystem;
    fs::path p(scenePath_);
    if (!fs::exists(p))
        return;
    auto t = fs::last_write_time(p);
    if (t != lastWriteTime_)
    {
        auto now = std::chrono::steady_clock::now();
        if ((now - lastAutoReloadTime_) < reloadDebounce_)
            return;
        lastAutoReloadTime_ = now;
        loadFromJSON(scenePath_);
    }
}

void Scene::resetToInitial()
{
    if (initialJson_.empty())
    {
        forceReload();
        return;
    }
    // loadFromString round-trips through a temp file, which leaves scenePath_
    // and the reload stamp pointing at a file that is deleted moments later.
    // Preserve them, or the editor's hot-reload watch is left aimed at nothing.
    const std::string path = scenePath_;
    const auto stamp = lastWriteTime_;
    loadFromString(initialJson_);
    scenePath_ = path;
    lastWriteTime_ = stamp;
}

void Scene::forceReload()
{
    if (scenePath_.empty())
        return;
    namespace fs = std::filesystem;
    fs::path p(scenePath_);
    if (!fs::exists(p))
        return;
    loadFromJSON(scenePath_);
    lastAutoReloadTime_ = std::chrono::steady_clock::now();
}

void Scene::addPlayer(Camera *camera, const glm::mat4 &model, Window *window, PhysicsWorld &physics)
{
    if (hasPlayer())
        removePlayer();
    physicsWorld_ = &physics;
    playerEntity_ = ecs::createPlayer(reg_, &physics, camera, model);

    // Per-tick input state driving the movement script, plus the tag marking
    // this as the client-owned player. LocalInputSystem fills these each frame.
    reg_.emplace_or_replace<ecs::PlayerInput>(playerEntity_);
    reg_.emplace_or_replace<ecs::LocalPlayer>(playerEntity_);

#if !defined(ENGINE_HEADLESS)
    if (window)
    {
#ifndef __EMSCRIPTEN__
        glfwSetCursorPosCallback(window->getWindow(), ecs::playerGlfwMouseCallback);
#else
        Window::setEmscriptenMouseDeltaCallback(
            [](void *user, float dx, float dy) {
                (void)user;
                auto *r = ecs::detail::activePlayerRegistry();
                auto e = ecs::detail::activePlayer();
                if (r && e != ecs::NullEntity)
                    ecs::playerMouseDelta(*r, e, dx, dy);
            },
            nullptr);
#endif
    }

    ecs::detail::setActivePlayer(&reg_, playerEntity_);
#else
    (void)window;
#endif
    s_current = this;
}

void Scene::removePlayer()
{
    if (!hasPlayer())
        return;
    if (physicsWorld_)
    {
        if (auto *p = reg_.try_get<ecs::Physics>(playerEntity_); p && p->body)
            physicsWorld_->removeRigidBody(p->body.get());
    }
    reg_.destroy(playerEntity_);
    playerEntity_ = ecs::NullEntity;
#if !defined(ENGINE_HEADLESS)
    ecs::detail::setActivePlayer(&reg_, ecs::NullEntity);
#endif
}

void Scene::registerRigidBody(ecs::Entity e)
{
    if (!physicsWorld_)
        return;
    auto *p = reg_.try_get<ecs::Physics>(e);
    if (p && p->body)
        physicsWorld_->addRigidBody(p->body.get());
}

ecs::Entity Scene::spawnCube(int size, const glm::mat4 &model, const glm::vec4 &color, float mass)
{
    return ecs::createCube(reg_, physicsWorld_, size, model, color, mass);
}

ecs::Entity Scene::spawnPlane(float length, float width, const glm::mat4 &model, const glm::vec4 &color, float mass)
{
    return ecs::createPlane(reg_, physicsWorld_, length, width, model, color, mass);
}

ecs::Entity Scene::createEmpty(const std::string &name, const glm::mat4 &model)
{
    return ecs::createEmptyEntity(reg_, name, model);
}

ecs::Entity Scene::createRemoteAvatar(const glm::vec4 &color)
{
    ecs::Entity e = reg_.create();
    ecs::Identity id;
    id.name = "RemotePlayer";
    reg_.emplace<ecs::Identity>(e, std::move(id));

    ecs::Transform t;
    ecs::initializeTransformFromModel(t, glm::mat4(1.0f));
    // Start collapsed to a point so the avatar is invisible until its first
    // snapshot positions it — avoids a one-frame flash at the origin.
    t.model = glm::mat4(0.0f);
    reg_.emplace<ecs::Transform>(e, std::move(t));

    // Other players are shown as a color-tinted cow that faces where their
    // camera is looking (the server encodes the look heading in the snapshot
    // rotation for player entities).
    ecs::Renderable rd;
    rd.mesh = AssetManager::instance().loadStaticMeshFromOBJ("models/cow.obj", "cow");
    if (!rd.mesh)
        rd.mesh = std::make_shared<CubeMesh>(1); // fallback if the model is missing
    rd.color = color;
    reg_.emplace<ecs::Renderable>(e, std::move(rd));
    return e;
}

ecs::Entity Scene::createNetProxy(int kind, const glm::vec4 &color)
{
    ecs::Entity e = reg_.create();
    ecs::Identity id;
    id.name = "NetProxy";
    reg_.emplace<ecs::Identity>(e, std::move(id));

    ecs::Transform t;
    ecs::initializeTransformFromModel(t, glm::mat4(1.0f));
    // Hidden until the first snapshot positions it (see createRemoteAvatar).
    t.model = glm::mat4(0.0f);
    reg_.emplace<ecs::Transform>(e, std::move(t));

    std::shared_ptr<Mesh> mesh;
    if (kind == 1) // cow
        mesh = AssetManager::instance().loadStaticMeshFromOBJ("models/cow.obj", "cow");
    else if (kind == 2) // plane
        mesh = std::make_shared<PlaneMesh>(10.0f, 10.0f, 2.0f, 2.0f);
    if (!mesh) // cube (default / fallback)
        mesh = std::make_shared<CubeMesh>(1);

    ecs::Renderable rd;
    rd.mesh = mesh;
    rd.color = color;
    reg_.emplace<ecs::Renderable>(e, std::move(rd));
    return e;
}

void Scene::attachNetCollider(ecs::Entity e, const glm::vec3 &scale)
{
    if (!physicsWorld_ || !reg_.valid(e) || reg_.any_of<ecs::Physics>(e))
        return;
    auto *rd = reg_.try_get<ecs::Renderable>(e);
    auto *t = reg_.try_get<ecs::Transform>(e);
    if (!rd || !rd->mesh || !t)
        return;

    // Half-extents from the mesh's local AABB, scaled to world size. Falls back
    // to a unit box if the mesh exposes no vertex data.
    glm::vec3 half(0.5f);
    const auto &verts = rd->mesh->getVertices();
    const int fpv = rd->mesh->getFloatsPerVertex();
    if (!verts.empty() && fpv >= 3)
    {
        glm::vec3 mn(std::numeric_limits<float>::max());
        glm::vec3 mx(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i + 2 < verts.size(); i += fpv)
        {
            glm::vec3 p(verts[i], verts[i + 1], verts[i + 2]);
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
        }
        half = (mx - mn) * 0.5f;
    }
    half *= glm::abs(scale);
    half = glm::max(half, glm::vec3(0.02f)); // never a degenerate shape

    auto shape = std::make_unique<btBoxShape>(btVector3(half.x, half.y, half.z));
    btTransform xf;
    xf.setFromOpenGLMatrix(glm::value_ptr(t->modelNoScale));
    auto motion = std::make_unique<btDefaultMotionState>(xf);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, motion.get(), shape.get(), btVector3(0, 0, 0));
    auto body = std::make_unique<btRigidBody>(rbInfo);
    // Kinematic: never simulated, but pushes the dynamic (predicted) player out.
    body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
    body->setActivationState(DISABLE_DEACTIVATION);

    ecs::Physics phys;
    phys.shape = std::move(shape);
    phys.motion = std::move(motion);
    phys.body = std::move(body);
    phys.mass = 0.0;
    reg_.emplace<ecs::Physics>(e, std::move(phys));
    physicsWorld_->addRigidBody(reg_.get<ecs::Physics>(e).body.get());
}

ecs::Entity Scene::spawnStaticFromAsset(const std::string &meshPath, const std::string &meshName,
                                        const glm::mat4 &model, const glm::vec4 &color, float mass)
{
    auto &am = AssetManager::instance();
    auto mesh = am.loadStaticMeshFromOBJ(meshPath, meshName.empty() ? meshPath : meshName);
    if (!mesh)
        return ecs::NullEntity;
    const auto &verts = mesh->getVertices();
    const auto &inds = mesh->getIndices();
    int stride = mesh->getFloatsPerVertex();
    auto e = ecs::createStaticObject(reg_, physicsWorld_, mesh, verts.data(),
                                     verts.size() / stride, inds.data(), inds.size(),
                                     stride, model, color, mass);
    if (e != ecs::NullEntity)
        reg_.get<ecs::Identity>(e).meshPath = meshPath;
    return e;
}

void Scene::setSelectedEntity(ecs::Entity e)
{
    if (selectedEntity_ != ecs::NullEntity && reg_.valid(selectedEntity_))
        reg_.remove<ecs::Selected>(selectedEntity_);
    selectedEntity_ = e;
    if (e != ecs::NullEntity && reg_.valid(e))
        reg_.emplace_or_replace<ecs::Selected>(e);
}

void Scene::setHoveredEntity(ecs::Entity e)
{
    if (hoveredEntity_ != ecs::NullEntity && reg_.valid(hoveredEntity_))
        reg_.remove<ecs::Hovered>(hoveredEntity_);
    hoveredEntity_ = e;
    if (e != ecs::NullEntity && reg_.valid(e))
        reg_.emplace_or_replace<ecs::Hovered>(e);
}

void Scene::addRigidBodiesToWorld(PhysicsWorld &physics)
{
    physicsWorld_ = &physics;
    auto view = reg_.view<ecs::Physics>();
    for (auto e : view)
    {
        auto &p = view.get<ecs::Physics>(e);
        if (p.body)
            physics.addRigidBody(p.body.get());
    }
}

void Scene::syncFromPhysics()
{
    ecs::physicsSyncSystem(reg_);
}

void Scene::destroyEntity(ecs::Entity e)
{
    if (!reg_.valid(e))
        return;
    if (physicsWorld_)
    {
        if (auto *p = reg_.try_get<ecs::Physics>(e); p && p->body)
            physicsWorld_->removeRigidBody(p->body.get());
    }
    if (selectedEntity_ == e) selectedEntity_ = ecs::NullEntity;
    if (hoveredEntity_ == e) hoveredEntity_ = ecs::NullEntity;
    if (playerEntity_ == e) playerEntity_ = ecs::NullEntity;
    reg_.destroy(e);
}

ecs::Entity Scene::raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance)
{
    if (!Scene::s_current || !Scene::s_current->physicsWorld_)
        return ecs::NullEntity;

    btVector3 from(origin.x, origin.y, origin.z);
    btVector3 to = from + btVector3(direction.x, direction.y, direction.z) * maxDistance;
    btCollisionWorld::ClosestRayResultCallback cb(from, to);
    Scene::s_current->physicsWorld_->rayTest(from, to, cb);
    if (!cb.hasHit())
        return ecs::NullEntity;
    const btCollisionObject *colObj = cb.m_collisionObject;
    if (!colObj)
        return ecs::NullEntity;
    return ecs::fromUserPointer(colObj->getUserPointer());
}

void Scene::explode(const glm::vec3 &pos, const PhysicsWorld::BlastParams &params)
{
    if (physicsWorld_)
        physicsWorld_->applyRadialBlast(btVector3(pos.x, pos.y, pos.z), params);

#if !defined(ENGINE_HEADLESS)
    // The shockwave ring. It carries no Identity and no ShapeMarker on purpose:
    // those are what put an entity in the hierarchy panel and in the saved
    // scene, and a blast is neither an object the user owns nor state worth
    // persisting. blastVfxSystem owns it from here and destroys it when spent.
    {
        // One mesh shared by every blast, built on first use so a headless-ish
        // client that never explodes anything never allocates a GL buffer.
        static std::shared_ptr<Mesh> s_shockwave;
        if (!s_shockwave)
            s_shockwave = std::make_shared<ShockwaveMesh>();

        ecs::Entity e = reg_.create();

        ecs::Transform t;
        // The ring mesh is unit radius, so modelNoScale is the blast's position
        // and blastVfxSystem animates size purely through the scale factor.
        t.position = glm::dvec3(pos);
        t.modelNoScale = glm::translate(glm::mat4(1.0f), pos);
        t.model = t.modelNoScale;
        reg_.emplace<ecs::Transform>(e, std::move(t));

        ecs::Renderable rd;
        rd.mesh = s_shockwave;
        rd.wireframe = true;
        reg_.emplace<ecs::Renderable>(e, std::move(rd));

        ecs::BlastVfx vfx;
        vfx.radius = params.radius;
        reg_.emplace<ecs::BlastVfx>(e, vfx);
    }
#endif

    // Last, so the observer sees a blast that has already been applied locally.
    if (onExplosion_)
        onExplosion_(pos, params);
}


#if !defined(ENGINE_HEADLESS)
void Scene::render(Window &window, Shader &shader)
{
    ecs::renderSystem(reg_, window, shader);
}

void Scene::renderTransparent(Window &window, Shader &shader)
{
    ecs::renderTransparentSystem(reg_, window, shader);
}

void Scene::renderFill(Window &window, Shader &shader)
{
    ecs::renderFillSystem(reg_, window, shader);
}
#endif // !ENGINE_HEADLESS

int Scene::loadScripts(ScriptHost &host)
{
    return ecs::loadScripts(reg_, host);
}

void Scene::resetScripts()
{
    ecs::resetScripts(reg_);
}

void Scene::startScripts(ScriptHost &host)
{
    ecs::startScripts(reg_, host);
}

void Scene::updateScripts(ScriptHost &host, float dt)
{
    ecs::updateScripts(reg_, host, dt);
}
