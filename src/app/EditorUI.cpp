#define IMGUI_DEFINE_MATH_OPERATORS
#ifndef IMGUI_ENABLE_DOCKING
#define IMGUI_ENABLE_DOCKING
#endif
#include "app/EditorUI.hpp"

#include "app/CodeEditor.hpp"
#include "editor/panels/WorkspacePanel.hpp"
#include "editor/panels/HierarchyPanel.hpp"
#include "editor/panels/InspectorPanel.hpp"
#include "editor/panels/ConsolePanel.hpp"
#include "editor/panels/StatsPanel.hpp"
#include "editor/panels/RuntimePanel.hpp"
#include "editor/panels/FileBrowserPanel.hpp"
#include "editor/panels/VfxPanel.hpp"
#include "platform/ImGuiLayer.hpp"
#include "editor/Win95Widgets.hpp"

#if !defined(COWENGINE_GAME)
#include "app/GameBuilder.hpp"
#endif

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <cstdio>

EditorUI::EditorUI()
{
    workspacePanel = std::make_unique<editor::WorkspacePanel>();
    hierarchyPanel = std::make_unique<editor::HierarchyPanel>();
    inspectorPanel = std::make_unique<editor::InspectorPanel>();
    consolePanel = std::make_unique<editor::ConsolePanel>();
    statsPanel = std::make_unique<editor::StatsPanel>();
    runtimePanel = std::make_unique<editor::RuntimePanel>();
    fileBrowserPanel = std::make_unique<editor::FileBrowserPanel>();
    vfxPanel = std::make_unique<editor::VfxPanel>();

    ctx.codeEditor = workspacePanel->codeEditor();
    if (ctx.codeEditor)
    {
        ctx.codeEditor->setLogger([this](const std::string &line)
                                  { ctx.addLog(line, ImVec4(0.7f, 0.85f, 1.0f, 1.0f)); });
    }

    ctx.addLog("Editor UI ready. Type 'help' for commands.");

    // Restore VFX settings persisted in the browser's localStorage. No-op on
    // native builds — settings still reset to defaults each launch there.
    ctx.loadVfxFromLocalStorage();
}

EditorUI::~EditorUI() = default;

CodeEditor *EditorUI::getCodeEditor()
{
    return workspacePanel ? workspacePanel->codeEditor() : nullptr;
}

bool EditorUI::getGameViewport(float &x, float &y, float &w, float &h, float &scaleX, float &scaleY) const
{
    if (!ctx.hasGameViewport)
        return false;
    x = ctx.gameViewportX;
    y = ctx.gameViewportY;
    w = ctx.gameViewportW;
    h = ctx.gameViewportH;
    scaleX = ctx.framebufferScaleX;
    scaleY = ctx.framebufferScaleY;
    return true;
}

void EditorUI::setGameTexture(ImTextureID textureId, float width, float height)
{
    ctx.gameTextureId = textureId;
    ctx.gameTextureW = width;
    ctx.gameTextureH = height;
}

bool EditorUI::isMouseOverGizmo() const
{
    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

void EditorUI::render(Scene *scene, Window *window, PhysicsWorld *physics, float deltaSeconds, float fps)
{
    if (!showUI)
        return;

    ImGuizmo::BeginFrame();
    ctx.scene = scene;
    ctx.window = window;
    ctx.physics = physics;

    ImGuiIO &io = ImGui::GetIO();
    ctx.framebufferScaleX = io.DisplayFramebufferScale.x;
    ctx.framebufferScaleY = io.DisplayFramebufferScale.y;
    ctx.hasGameViewport = false;

    // Tool shortcuts — run every editor frame, not gated on selection or game viewport
    if (!ctx.testingMode && !io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_1))
            ctx.gizmoOp = editor::GizmoOp::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_2))
            ctx.gizmoOp = editor::GizmoOp::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_3))
            ctx.gizmoOp = editor::GizmoOp::Scale;
    }

    drawDockspace();
    drawMainMenu();
    drawPublishModal();

    if (ctx.testingMode)
    {
        workspacePanel->drawTestingOverlay(ctx);
        ctx.showGameView = true;
    }

    if (ctx.showGameView)
        workspacePanel->drawWorkspace(ctx);

    if (ctx.showGameView && !ctx.testingMode && ctx.activeTab == editor::SceneTab)
        workspacePanel->drawGizmoToolbar(ctx);

    if (!ctx.testingMode)
    {
        if (ctx.showHierarchy)
            hierarchyPanel->draw(ctx);
        if (ctx.showInspector)
            inspectorPanel->draw(ctx);
        if (ctx.showStats)
            statsPanel->draw(ctx, deltaSeconds, fps);
        if (ctx.showConsole)
            consolePanel->draw(ctx);
        if (ctx.showRuntime)
            runtimePanel->draw(ctx);
        if (ctx.showFiles)
            fileBrowserPanel->draw(ctx);
        if (ctx.showVfx)
            vfxPanel->draw(ctx);
    }

    // Persist VFX changes to browser localStorage. The save is dirty-checked
    // internally so unchanged frames are free.
    ctx.saveVfxToLocalStorage();
}

void EditorUI::drawMainMenu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Mode"))
        {
            if (!ctx.testingMode && ImGui::MenuItem("Start Testing"))
                ctx.testingMode = true;
            if (ctx.testingMode && ImGui::MenuItem("Stop Testing"))
                ctx.testingMode = false;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem("Workspace", nullptr, &ctx.showGameView);
            ImGui::MenuItem("Scene Hierarchy", nullptr, &ctx.showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &ctx.showInspector);
            ImGui::MenuItem("Debug Console", nullptr, &ctx.showConsole);
            ImGui::MenuItem("Stats", nullptr, &ctx.showStats);
            ImGui::MenuItem("Runtime", nullptr, &ctx.showRuntime);
            ImGui::MenuItem("Files", nullptr, &ctx.showFiles);
            ImGui::MenuItem("VFX", nullptr, &ctx.showVfx);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Scene Tab"))
                ctx.requestedTab = editor::SceneTab;
            if (ImGui::MenuItem("Code Tab"))
                ctx.requestedTab = editor::CodeTab;
            if (ImGui::MenuItem("Help Tab"))
                ctx.requestedTab = editor::HelpTab;
            ImGui::Separator();
            if (ImGui::BeginMenu("Theme"))
            {
                const ImGuiLayer::Theme theme = ImGuiLayer::currentTheme();
                if (ImGui::MenuItem("Windows 95", nullptr, theme == ImGuiLayer::Theme::Win95))
                    ImGuiLayer::applyTheme(ImGuiLayer::Theme::Win95);
                if (ImGui::MenuItem("Windows 95 (Dark)", nullptr, theme == ImGuiLayer::Theme::Win95Dark))
                    ImGuiLayer::applyTheme(ImGuiLayer::Theme::Win95Dark);
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
#if !defined(COWENGINE_GAME)
        if (ImGui::BeginMenu("Build"))
        {
            auto runBuild = [&](GameBuilder::Target target)
            {
                if (!ctx.scene)
                {
                    ctx.addLog("No active scene to build.", ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
                    return;
                }
                GameBuilder::Result result = GameBuilder::build(
                    target,
                    ctx.scene,
                    [this](const std::string &line)
                    { ctx.addLog(line, ImVec4(0.7f, 0.85f, 1.0f, 1.0f)); });
                if (result.ok)
                    ctx.addLog(result.message, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
                else
                    ctx.addLog(result.message, ImVec4(1.0f, 0.55f, 0.55f, 1.0f));
            };

            const GameBuilder::Target targets[] = {
                GameBuilder::Target::Linux,
                GameBuilder::Target::Windows,
                GameBuilder::Target::Web,
            };

            for (GameBuilder::Target target : targets)
            {
                bool available = GameBuilder::isTargetAvailable(target);
                if (ImGui::MenuItem(GameBuilder::targetLabel(target), nullptr, false, available))
                    runBuild(target);
            }

            ImGui::Separator();
            const bool canPublish = GameBuilder::publishAvailable();
            if (ImGui::MenuItem("Publish to CowEngine...", nullptr, false, canPublish))
            {
                publishModalOpen = true;
                publishModalJustOpened = true;
            }
            if (!canPublish && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("No CowEngine server configured.\n"
                                  "Web: ?api=<url>   Native: COWENGINE_API=<url>");
            ImGui::EndMenu();
        }
#endif
        ImGui::EndMainMenuBar();
    }
}

void EditorUI::drawDockspace()
{
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    float menuBarHeight = ImGui::GetFrameHeight();
    ImVec2 dockPos(viewport->Pos.x, viewport->Pos.y + menuBarHeight);
    ImVec2 dockSize(viewport->Size.x, viewport->Size.y - menuBarHeight);
    ImGui::SetNextWindowPos(dockPos);
    ImGui::SetNextWindowSize(dockSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoBackground;

    ImGui::Begin("DockSpaceHost", nullptr, flags);
    ImGuiID dockspaceId = ImGui::GetID("DockSpace");
    ImGuiDockNodeFlags dockFlags = ImGuiDockNodeFlags_PassthruCentralNode;
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), dockFlags);

    if (!dockLayoutBuilt)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspaceId, dockSize);

        ImGuiID dockMain = dockspaceId;
        ImGuiID dockLeft = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, nullptr, &dockMain);
        ImGuiID dockRight = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.26f, nullptr, &dockMain);
        ImGuiID dockBottom = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.28f, nullptr, &dockMain);

        ImGuiID dockRightBottom = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Down, 0.45f, nullptr, &dockRight);

        ImGui::DockBuilderDockWindow("Workspace", dockMain);
        ImGui::DockBuilderDockWindow("Scene Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("VFX", dockRightBottom);
        ImGui::DockBuilderDockWindow("Runtime", dockRightBottom);
        ImGui::DockBuilderDockWindow("Debug Console", dockBottom);
        ImGui::DockBuilderDockWindow("Stats", dockBottom);
        ImGui::DockBuilderDockWindow("Files", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
        dockLayoutBuilt = true;
    }
    ImGui::End();
}

#if !defined(COWENGINE_GAME)
void EditorUI::drawPublishModal()
{
    if (!publishModalOpen)
        return;

    if (publishModalJustOpened)
    {
        ImGui::OpenPopup("Publish to CowEngine");
        publishModalJustOpened = false;
        if (publishTitle[0] == '\0')
            std::snprintf(publishTitle, sizeof(publishTitle), "My CowEngine Game");
    }

    ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Publish to CowEngine", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        publishModalOpen = false;
        return;
    }

    // Poll every frame: on the web the upload is a fetch that completes on some
    // later frame, so this is the only place the result can arrive.
    const GameBuilder::PublishStatus status = GameBuilder::publishPoll();
    const bool busy = status.state == GameBuilder::PublishState::Pending;
    const std::string existing = GameBuilder::lastPublishedId();

    if (existing.empty())
        ImGui::TextWrapped("Upload this scene, its scripts and its models to the CowEngine "
                           "server. It will appear in the game browser for anyone to play.");
    else
        ImGui::TextWrapped("This will publish a new version of \"%s\". Players already in a "
                           "running world stay on the old version until it empties.",
                           existing.c_str());

    ImGui::Separator();
    ImGui::BeginDisabled(busy);
    ui95::InputText("Title", publishTitle, sizeof(publishTitle));
    ui95::InputText("Description", publishDescription, sizeof(publishDescription));
    ImGui::EndDisabled();

    ImGui::Spacing();

    switch (status.state)
    {
    case GameBuilder::PublishState::Pending:
        ImGui::TextColored(ImVec4(0.7f, 0.85f, 1.0f, 1.0f), "%s", status.message.c_str());
        break;
    case GameBuilder::PublishState::Failed:
        ImGui::PushTextWrapPos(440.0f);
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.55f, 1.0f), "%s", status.message.c_str());
        ImGui::PopTextWrapPos();
        break;
    case GameBuilder::PublishState::Success:
    {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", status.message.c_str());
        if (!status.editKey.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                               "Save this edit key — it is the only way to update or");
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                               "remove this game, and it is shown only once:");
            // Read-only rather than plain text so it can be selected and copied;
            // the key is unrecoverable, so making it awkward to copy would be a
            // genuine way to lose someone's game.
            char keyBuf[80];
            std::snprintf(keyBuf, sizeof(keyBuf), "%s", status.editKey.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##editkey", keyBuf, sizeof(keyBuf),
                             ImGuiInputTextFlags_ReadOnly);
            if (ui95::Button("Copy key"))
                ImGui::SetClipboardText(status.editKey.c_str());
            ImGui::SameLine();
        }
        break;
    }
    case GameBuilder::PublishState::Idle:
        break;
    }

    ImGui::Separator();

    ImGui::BeginDisabled(busy);
    if (ui95::Button(existing.empty() ? "Publish" : "Publish update"))
    {
        GameBuilder::publishStart(
            ctx.scene, publishTitle, publishDescription,
            [this](const std::string &line)
            { ctx.addLog(line, ImVec4(0.7f, 0.85f, 1.0f, 1.0f)); });
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    if (ui95::Button("Close"))
    {
        publishModalOpen = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
#else
void EditorUI::drawPublishModal() {}
#endif
