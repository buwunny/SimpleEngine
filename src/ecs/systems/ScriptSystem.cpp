#include "ecs/systems/ScriptSystem.hpp"
#include "ecs/Components.hpp"
#include "script/CowScript.hpp"
#include "script/ScriptHost.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace ecs
{
    int loadScripts(Registry &r, ScriptHost &host)
    {
        int count = 0;
        auto view = r.view<Identity>();
        for (auto e : view)
        {
            auto &ident = view.get<Identity>(e);
            if (ident.scriptPaths.empty())
                continue;
            // Already compiled? Skip. The editor removes the component to force a reload.
            if (r.all_of<ScriptComponent>(e) && !r.get<ScriptComponent>(e).scripts.empty())
                continue;

            std::vector<ScriptInstance> scripts;
            for (const auto &path : ident.scriptPaths)
            {
                if (path.empty())
                    continue;
                std::string foundPath;
                std::string source = cowscript::readScriptFile(path, &foundPath);
                if (source.empty())
                {
                    std::cerr << "ScriptSystem: failed to read '" << path << "'" << std::endl;
                    continue;
                }
                auto script = std::make_shared<cowscript::Script>();
                host.bindBuiltins(*script);
                // Before compile(), so the top-level statements are bounded too.
                script->setLimits(host.limits());
                std::string err = script->compile(source);
                if (!err.empty())
                {
                    std::cerr << "ScriptSystem: compile error in '" << path << "': " << err << std::endl;
                    continue;
                }
                scripts.push_back(ScriptInstance{path, std::move(script)});
                ++count;
            }
            if (!scripts.empty())
                r.emplace_or_replace<ScriptComponent>(e, ScriptComponent{std::move(scripts)});
        }
        return count;
    }

    void resetScripts(Registry &r)
    {
        r.clear<ScriptComponent>();
    }

    namespace
    {
        // Run one event over every scripted entity, tolerating scripts that change
        // the world while they run. A .cow script can spawn an object, attach a
        // script to one (attach_script) or destroy one (destroy/destroy_self), and
        // all three touch the ScriptComponent pool — which invalidates view
        // iterators and can reallocate the pool out from under a held reference.
        // So: snapshot the entities up front, and re-look-up the component (by
        // index, revalidating the entity) around every call.
        void dispatch(Registry &r, ScriptHost &host, const char *event,
                      const std::vector<cowscript::Value> &args, bool startOnly)
        {
            std::vector<Entity> pending;
            pending.reserve(r.view<ScriptComponent>().size());
            for (auto e : r.view<ScriptComponent>())
                pending.push_back(e);

            std::string path; // reused, so the copy below doesn't allocate per call
            for (auto e : pending)
            {
                if (!r.valid(e) || !r.all_of<ScriptComponent>(e))
                    continue; // destroyed, or unscripted, by an earlier script
                host.setSelf(e);
                for (size_t i = 0;; ++i)
                {
                    if (!r.valid(e))
                        break; // the script called destroy_self()
                    auto *sc = r.try_get<ScriptComponent>(e);
                    if (!sc || i >= sc->scripts.size())
                        break;
                    auto &inst = sc->scripts[i];
                    if (!inst.script || inst.disabled)
                        continue;

                    // A fresh instance always gets start() before its first
                    // update, wherever it was attached from.
                    const bool needsStart = !inst.started;
                    if (startOnly && !needsStart)
                        continue;
                    inst.started = true;
                    path = inst.path;
                    auto script = inst.script; // outlive a destroy during the call

                    // Re-look-up before writing `disabled`: the call may have
                    // reallocated the pool, so the earlier `inst` reference
                    // (and `sc`) can be dangling by the time we get back.
                    auto markDisabled = [&r, e, i]()
                    {
                        if (!r.valid(e))
                            return;
                        auto *cur = r.try_get<ScriptComponent>(e);
                        if (cur && i < cur->scripts.size())
                            cur->scripts[i].disabled = true;
                    };

                    if (needsStart)
                    {
                        std::string err = script->callEvent("start", {});
                        if (!err.empty())
                            std::cerr << "ScriptSystem: '" << path << "' on start: " << err << std::endl;
                        if (script->lastErrorWasLimit())
                        {
                            std::cerr << "ScriptSystem: disabling '" << path
                                      << "' (execution limit breached)" << std::endl;
                            markDisabled();
                            continue;
                        }
                        if (startOnly)
                            continue;
                        if (!r.valid(e))
                            break;
                    }
                    std::string err = script->callEvent(event, args);
                    if (!err.empty())
                        std::cerr << "ScriptSystem: '" << path << "' on " << event << ": " << err << std::endl;
                    if (script->lastErrorWasLimit())
                    {
                        std::cerr << "ScriptSystem: disabling '" << path
                                  << "' (execution limit breached)" << std::endl;
                        markDisabled();
                    }
                }
            }
            host.setSelf(NullEntity);
        }
    }

    void startScripts(Registry &r, ScriptHost &host)
    {
        dispatch(r, host, "start", {}, /*startOnly=*/true);
    }

    void updateScripts(Registry &r, ScriptHost &host, float dt)
    {
        std::vector<cowscript::Value> args;
        args.push_back(cowscript::Value::makeNumber(dt));
        dispatch(r, host, "update", args, /*startOnly=*/false);
    }
}
