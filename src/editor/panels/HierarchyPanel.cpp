#include "editor/panels/HierarchyPanel.hpp"
#include "editor/Win95Widgets.hpp"

#include "core/Scene.hpp"
#include "core/PhysicsWorld.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentOps.hpp"
#include "ecs/Factories.hpp"

#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>

namespace editor
{
    namespace
    {
        const char *shapeKindName(ecs::ShapeKind k)
        {
            switch (k)
            {
            case ecs::ShapeKind::Cube:
                return "Cube";
            case ecs::ShapeKind::Plane:
                return "Plane";
            case ecs::ShapeKind::Static:
                return "StaticObject";
            case ecs::ShapeKind::Player:
                return "Player";
            }
            return "Entity";
        }
    }

    void HierarchyPanel::draw(Context &ctx)
    {
        Scene *scene = ctx.scene;
        ImGui::Begin("Scene Hierarchy", &ctx.showHierarchy);
        ctx.heiarchyInput = (ImGui::IsWindowHovered(ImGuiFocusedFlags_RootAndChildWindows) ||
                             ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) &&
                            ctx.showHierarchy;

        if (!scene)
        {
            ImGui::TextUnformatted("No scene loaded.");
            ImGui::End();
            return;
        }

        const std::string &path = scene->getScenePath();
        ImGui::Text("Scene: %s", path.empty() ? "<unsaved>" : path.c_str());
        if (ui95::Button("Reload"))
        {
            ctx.clearSelection();
            scene->forceReload();
            ctx.addLog("Scene reload requested.", ImVec4(0.9f, 0.8f, 0.4f, 1.0f));
        }
        ImGui::SameLine();
        if (ui95::Button("Save"))
        {
            std::string savePath = path.empty() ? "scenes/scene.json" : path;
            if (scene->saveToJSON(savePath))
            {
                ctx.lastSavePath = savePath;
                ctx.addLog("Scene saved to " + savePath, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
            }
            else
            {
                ctx.addLog("Scene save failed for " + savePath, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            }
        }

        hierarchyFilter.Draw("Filter");
        ImGui::Separator();

        auto spawnMenuItems = [&]()
        {
            if (ImGui::MenuItem("Empty Entity"))
            {
                ecs::Entity ne = scene->createEmpty("Entity",
                                                    glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)));
                ctx.setSelection(ne);
            }
            if (ImGui::MenuItem("Cube"))
                addObjectToScene(ctx, "cube");
            if (ImGui::MenuItem("Plane"))
                addObjectToScene(ctx, "plane");
            if (ImGui::MenuItem("Cow"))
                addObjectToScene(ctx, "cow");
            if (ImGui::MenuItem("Eiffel Tower"))
                addObjectToScene(ctx, "tower");
        };

        auto drawEntityRow = [&](ecs::Entity e)
        {
            auto &reg = scene->registry();
            auto *ident = reg.try_get<ecs::Identity>(e);
            auto *sm = reg.try_get<ecs::ShapeMarker>(e);
            std::string label = (ident ? ident->name : std::string("Entity")) +
                                " [" + (sm ? shapeKindName(sm->kind) : "Entity") + "]";
            if (!hierarchyFilter.PassFilter(label.c_str()))
                return;
            bool selected = ctx.selection.entity == e;
            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(e)));
            if (ImGui::Selectable(label.c_str(), selected))
                ctx.setSelection(e);

            // Right-click selects (so the action targets what the user pointed at)
            // and opens a per-entity context menu.
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                ctx.setSelection(e);
            if (ImGui::BeginPopupContextItem("##entity_ctx"))
            {
                bool isPlayer = (e == scene->getPlayerEntity());
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, !isPlayer))
                {
                    ecs::Entity dup = duplicateEntity(ctx, e);
                    if (dup != ecs::NullEntity)
                        ctx.setSelection(dup);
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, !isPlayer))
                    copyEntityToClipboard(ctx, e);
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, entityClipboard.valid))
                {
                    ecs::Entity p = pasteEntityFromClipboard(ctx);
                    if (p != ecs::NullEntity)
                        ctx.setSelection(p);
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Add New"))
                {
                    spawnMenuItems();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Delete", "Del", false, !isPlayer))
                {
                    if (ctx.selection.entity == e)
                        ctx.clearSelection();
                    scene->destroyEntity(e);
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        };

        if (scene->hasPlayer())
            drawEntityRow(scene->getPlayerEntity());

        scene->forEachEntity([&](ecs::Entity e)
                             { drawEntityRow(e); });

        if (ImGui::BeginPopupContextWindow("##hierarchy_bg_ctx",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, entityClipboard.valid))
            {
                ecs::Entity p = pasteEntityFromClipboard(ctx);
                if (p != ecs::NullEntity)
                    ctx.setSelection(p);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Add New"))
            {
                spawnMenuItems();
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void HierarchyPanel::copyEntityToClipboard(Context &ctx, ecs::Entity e)
    {
        Scene *scene = ctx.scene;
        if (!scene || e == ecs::NullEntity || !scene->registry().valid(e))
            return;
        if (e == scene->getPlayerEntity())
        {
            ctx.addLog("Cannot copy the player entity.", ImVec4(0.9f, 0.7f, 0.4f, 1.0f));
            return;
        }
        auto &reg = scene->registry();
        EntityClipboard c;
        c.valid = true;
        if (auto *id = reg.try_get<ecs::Identity>(e))
        {
            c.name = id->name;
            c.scriptPaths = id->scriptPaths;
            c.meshPath = id->meshPath;
        }
        if (auto *t = reg.try_get<ecs::Transform>(e))
        {
            c.position = glm::vec3(t->position);
            c.rotation = glm::vec3(t->rotation);
            c.scale = glm::vec3(t->scale);
        }
        if (auto *rd = reg.try_get<ecs::Renderable>(e))
        {
            c.hasRenderable = true;
            c.color = rd->color;
        }
        if (auto *sm = reg.try_get<ecs::ShapeMarker>(e))
        {
            c.hasShapeMarker = true;
            c.kind = sm->kind;
            c.cubeSize = sm->cubeSize;
            c.planeLength = sm->planeLength;
            c.planeWidth = sm->planeWidth;
        }
        if (auto *p = reg.try_get<ecs::Physics>(e); p && p->body)
        {
            c.hasPhysics = true;
            float invMass = p->body->getInvMass();
            c.mass = (invMass > 0.f) ? 1.f / invMass : 0.f;
        }
        entityClipboard = std::move(c);
        ctx.addLog("Copied entity \"" + entityClipboard.name + "\" to clipboard.",
                   ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
    }

    ecs::Entity HierarchyPanel::pasteEntityFromClipboard(Context &ctx, const glm::vec3 &positionOffset)
    {
        Scene *scene = ctx.scene;
        if (!scene || !entityClipboard.valid)
            return ecs::NullEntity;
        const auto &c = entityClipboard;
        glm::vec3 newPos = c.position + positionOffset;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), newPos);

        ecs::Entity e = ecs::NullEntity;
        if (c.hasShapeMarker && c.hasRenderable)
        {
            switch (c.kind)
            {
            case ecs::ShapeKind::Cube:
                e = scene->spawnCube(c.cubeSize > 0 ? c.cubeSize : 1, model, c.color, c.mass);
                break;
            case ecs::ShapeKind::Plane:
                e = scene->spawnPlane(c.planeLength, c.planeWidth, model, c.color, c.mass);
                break;
            case ecs::ShapeKind::Static:
                if (!c.meshPath.empty())
                {
                    std::string key = std::filesystem::path(c.meshPath).stem().string();
                    e = scene->spawnStaticFromAsset(c.meshPath, key, model, c.color, c.mass);
                }
                break;
            case ecs::ShapeKind::Player:
                break;
            }
        }
        if (e == ecs::NullEntity)
            e = scene->createEmpty(c.name.empty() ? "Entity" : c.name, model);

        if (e == ecs::NullEntity)
        {
            ctx.addLog("Paste failed.", ImVec4(0.95f, 0.5f, 0.5f, 1.0f));
            return ecs::NullEntity;
        }

        if (auto *id = scene->registry().try_get<ecs::Identity>(e))
        {
            if (!c.name.empty())
                id->name = c.name;
            id->scriptPaths = c.scriptPaths;
        }

        ecs::applyTransform(scene->registry(), e, newPos, c.rotation, c.scale);
        if (ctx.physics)
        {
            if (auto *p = scene->registry().try_get<ecs::Physics>(e); p && p->body)
                ctx.physics->updateSingleAabb(p->body.get());
        }
        ctx.addLog("Pasted entity \"" + c.name + "\".", ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
        return e;
    }

    ecs::Entity HierarchyPanel::duplicateEntity(Context &ctx, ecs::Entity e)
    {
        if (!ctx.scene || e == ecs::NullEntity)
            return ecs::NullEntity;
        EntityClipboard saved = entityClipboard;
        copyEntityToClipboard(ctx, e);
        ecs::Entity created = pasteEntityFromClipboard(ctx);
        entityClipboard = saved;
        return created;
    }
}
