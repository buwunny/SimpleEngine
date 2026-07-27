#ifndef EDITOR_UI_HPP
#define EDITOR_UI_HPP

#include <memory>
#include <string>
#include <imgui.h>

#include "ecs/Entity.hpp"
#include "editor/EditorContext.hpp"

class Scene;
class Window;
class PhysicsWorld;
class Camera;
class CodeEditor;
class ScriptHost;

namespace editor
{
    class WorkspacePanel;
    class HierarchyPanel;
    class InspectorPanel;
    class ConsolePanel;
    class StatsPanel;
    class RuntimePanel;
    class FileBrowserPanel;
    class VfxPanel;
}

// Thin coordinator that owns the editor's panels and shared Context. Most of
// the editor's behavior lives in individual panels under include/editor/panels.
class EditorUI
{
public:
    using GizmoOp = editor::GizmoOp;
    enum WorkspaceTab
    {
        None = editor::TabNone,
        SceneTab = editor::SceneTab,
        CodeTab = editor::CodeTab,
        HelpTab = editor::HelpTab,
    };

    EditorUI();
    ~EditorUI();

    void render(Scene *scene, Window *window, PhysicsWorld *physics, float deltaSeconds, float fps);

    void setScriptHost(ScriptHost *host) { ctx.scriptHost = host; }
    CodeEditor *getCodeEditor();

    void addLog(const std::string &text, const ImVec4 &color = ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
    {
        ctx.addLog(text, color);
    }

    bool getGameViewport(float &x, float &y, float &w, float &h, float &scaleX, float &scaleY) const;
    bool isTestingMode() const { return ctx.testingMode; }
    bool isGameViewInputEnabled() const { return ctx.gameViewInput; }
    bool isHeiarchyInputEnabled() const { return ctx.heiarchyInput; }
    bool isColliderVisualizationEnabled() const { return ctx.showColliders; }
    float getCameraSpeed() const { return ctx.cameraSpeed; }
    const editor::Context::VFX &getVFX() const { return ctx.vfx; }

    void setGameTexture(ImTextureID textureId, float width, float height);
    void setSelection(ecs::Entity entity) { ctx.setSelection(entity); }
    void clearSelection() { ctx.clearSelection(); }
    void setRequestedTab(WorkspaceTab tab) { ctx.requestedTab = static_cast<editor::WorkspaceTab>(tab); }
    void setVisible(bool visible) { showUI = visible; }
    bool isVisible() const { return showUI; }

    void setCamera(Camera *cam) { ctx.camera = cam; }
    GizmoOp getGizmoOp() const { return ctx.gizmoOp; }
    bool isMouseOverGizmo() const;

    void openScriptInCodeEditor(const std::string &path) { ctx.openScriptInCodeEditor(path); }

private:
    void drawMainMenu();
    void drawDockspace();
    // Publish-to-server modal. Drawn every frame while open so an in-flight
    // upload can be polled — the web build has no Asyncify, so the fetch cannot
    // be awaited and progress has to be picked up from the frame loop.
    void drawPublishModal();

    editor::Context ctx;
    bool showUI = true;
    bool dockLayoutBuilt = false;

    bool publishModalOpen = false;
    bool publishModalJustOpened = false;
    char publishTitle[80] = {};
    char publishDescription[240] = {};

    std::unique_ptr<editor::WorkspacePanel> workspacePanel;
    std::unique_ptr<editor::HierarchyPanel> hierarchyPanel;
    std::unique_ptr<editor::InspectorPanel> inspectorPanel;
    std::unique_ptr<editor::ConsolePanel> consolePanel;
    std::unique_ptr<editor::StatsPanel> statsPanel;
    std::unique_ptr<editor::RuntimePanel> runtimePanel;
    std::unique_ptr<editor::FileBrowserPanel> fileBrowserPanel;
    std::unique_ptr<editor::VfxPanel> vfxPanel;
};

#endif // EDITOR_UI_HPP
