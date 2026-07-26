#include "editor/panels/RuntimePanel.hpp"
#include "editor/Win95Widgets.hpp"

#include "core/Scene.hpp"
#include "ecs/Components.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include <sstream>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace editor
{
    void RuntimePanel::draw(Context &ctx)
    {
        ImGui::Begin("Runtime", &ctx.showRuntime);

        Scene *scene = ctx.scene;
        if (!scene)
        {
            ImGui::TextUnformatted("No scene loaded.");
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Mode");
        if (!ctx.testingMode)
        {
            if (ui95::Button("Start Testing"))
            {
                std::string savePath = "scenes/scene.json";
                if (scene->saveToJSON(savePath))
                {
                    ctx.lastSavePath = savePath;
                    ctx.addLog("Scene saved to " + savePath, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                }
                else
                {
                    ctx.addLog("Scene save failed for " + savePath, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
                }
                ctx.testingMode = true;
            }
        }
        else
        {
            if (ui95::Button("Stop Testing"))
                ctx.testingMode = false;
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Tools");
        if (ui95::Button("Spawn Empty Entity"))
        {
            ecs::Entity e = scene->createEmpty("Entity",
                                               glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)));
            ctx.setSelection(e);
            ctx.addLog("Spawned empty entity — use the Inspector's Add Component menu to attach mesh / physics / script.",
                       ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
        }
        if (ui95::Button("Spawn Cow"))
            addObjectToScene(ctx, "cow");
        if (ui95::Button("Spawn Cube"))
            addObjectToScene(ctx, "cube");
        if (ui95::Button("Spawn Plane"))
            addObjectToScene(ctx, "plane");
        if (ui95::Button("Spawn Eiffel Tower"))
            addObjectToScene(ctx, "tower");

        ImGui::Separator();
        ImGui::TextUnformatted("Input");
        if (ui95::SliderFloat("Mouse Sensitivity", &ctx.mouseSensitivity, 0.1f, 10.0f))
        {
#if defined(__EMSCRIPTEN__)
            std::ostringstream ss;
            ss << "Module.mouseSensitivity = " << ctx.mouseSensitivity;
            emscripten_run_script(ss.str().c_str());
#endif
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Camera Speed");
        ui95::DragFloat("##CameraSpeed", &ctx.cameraSpeed, 0.1f);

        ImGui::End();
    }
}
