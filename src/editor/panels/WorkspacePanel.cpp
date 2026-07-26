#define IMGUI_DEFINE_MATH_OPERATORS
#include "editor/panels/WorkspacePanel.hpp"
#include "editor/Win95Widgets.hpp"

#include "core/Camera.hpp"
#include "core/Scene.hpp"
#include "platform/Window.hpp"
#include "app/CodeEditor.hpp"
#include "platform/ImGuiLayer.hpp"
#include "ecs/Components.hpp"
#include "ecs/ComponentOps.hpp"
#include "script/CowScript.hpp"
#include "script/ScriptHost.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb/stb_image.h"
#include "../../../third_party/imgui_markdown/imgui_markdown.h"

namespace editor
{
    WorkspacePanel::WorkspacePanel()
    {
        codeEditor_ = std::make_unique<CodeEditor>();
    }

    WorkspacePanel::~WorkspacePanel() = default;

    // -----------------------------------------------------------------------
    // Help-tab markdown rendering helpers
    // -----------------------------------------------------------------------
    namespace
    {
        // Same palette as CodeEditor so highlighted blocks feel consistent.
        ImU32 helpTokenColor(cowscript::TokenKind k)
        {
            switch (k)
            {
            case cowscript::TokenKind::Comment:
                return IM_COL32(110, 145, 110, 255);
            case cowscript::TokenKind::Keyword:
                return IM_COL32(198, 120, 221, 255);
            case cowscript::TokenKind::Number:
                return IM_COL32(255, 175, 100, 255);
            case cowscript::TokenKind::String:
                return IM_COL32(152, 195, 121, 255);
            case cowscript::TokenKind::Builtin:
                return IM_COL32(86, 180, 233, 255);
            case cowscript::TokenKind::Operator:
                return IM_COL32(200, 200, 220, 255);
            case cowscript::TokenKind::Punctuation:
                return IM_COL32(180, 180, 200, 255);
            default:
                return IM_COL32(220, 220, 220, 255);
            }
        }

        void renderHelpCodeBlock(const std::string &code)
        {
            const char *src = code.c_str();
            size_t srcLen = code.size();
            if (srcLen && src[srcLen - 1] == '\n')
                --srcLen;

            float lineH = ImGui::GetTextLineHeightWithSpacing();
            float lineH2 = ImGui::GetTextLineHeight();

            int numLines = 1;
            for (size_t i = 0; i < srcLen; ++i)
                if (src[i] == '\n')
                    ++numLines;

            const float padX = 12.0f;
            const float padY = 7.0f;
            float availW = ImGui::GetContentRegionAvail().x;
            float blockH = numLines * lineH - (lineH - lineH2) + padY * 2.0f;

            ImVec2 tl = ImGui::GetCursorScreenPos();
            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImFont *fnt = ImGui::GetFont();
            float fs = ImGui::GetFontSize();

            dl->AddRectFilled(tl, ImVec2(tl.x + availW, tl.y + blockH),
                              IM_COL32(22, 22, 32, 255), 5.0f);
            dl->AddRectFilled(tl, ImVec2(tl.x + 3.0f, tl.y + blockH),
                              IM_COL32(86, 156, 214, 180), 2.5f);

            std::string trimmed(src, srcLen);
            auto tokens = cowscript::highlight(trimmed);

            float cx = tl.x + padX;
            float cy = tl.y + padY;

            for (auto &tok : tokens)
            {
                if (tok.length <= 0)
                    continue;
                ImU32 col = helpTokenColor(tok.kind);

                const char *p = trimmed.c_str() + tok.start;
                const char *end = p + tok.length;

                while (p < end)
                {
                    const char *nl = static_cast<const char *>(memchr(p, '\n', end - p));
                    if (!nl)
                        nl = end;

                    if (nl > p)
                    {
                        dl->AddText(fnt, fs, ImVec2(cx, cy), col, p, nl);
                        cx += fnt->CalcTextSizeA(fs, FLT_MAX, 0.0f, p, nl).x;
                    }

                    if (nl < end)
                    {
                        cy += lineH;
                        cx = tl.x + padX;
                        p = nl + 1;
                    }
                    else
                        break;
                }
            }

            ImGui::Dummy(ImVec2(availW, blockH + 6.0f));
        }

        void helpFormatCallback(const ImGui::MarkdownFormatInfo &info, bool start)
        {
            ImGui::defaultMarkdownFormatCallback(info, start);
            if (info.type != ImGui::MarkdownFormatType::HEADING)
                return;
            if (start)
            {
                switch (info.level)
                {
                case 1:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.85f, 0.40f, 1.0f));
                    break;
                case 2:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.83f, 1.00f, 1.0f));
                    break;
                case 3:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 1.00f, 0.72f, 1.0f));
                    break;
                default:
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
                    break;
                }
            }
            else
            {
                ImGui::PopStyleColor();
            }
        }

        bool isSeparatorRow(const std::string &line)
        {
            bool hasDash = false;
            for (char c : line)
            {
                if (c == '-')
                {
                    hasDash = true;
                    continue;
                }
                if (c == '|' || c == ':' || c == ' ' || c == '\t')
                    continue;
                return false;
            }
            return hasDash;
        }

        std::vector<std::string> parseTableRow(const std::string &line)
        {
            std::vector<std::string> cells;
            size_t pos = (!line.empty() && line[0] == '|') ? 1 : 0;
            while (pos <= line.size())
            {
                size_t next = line.find('|', pos);
                if (next == std::string::npos)
                    next = line.size();
                std::string cell = line.substr(pos, next - pos);
                auto s = cell.find_first_not_of(" \t");
                auto e = cell.find_last_not_of(" \t");
                cells.push_back(s == std::string::npos ? "" : cell.substr(s, e - s + 1));
                pos = next + 1;
            }
            if (!cells.empty() && cells.back().empty())
                cells.pop_back();
            return cells;
        }

        void renderHelpTable(const std::string &content)
        {
            using namespace std;
            vector<vector<string>> rows;
            size_t pos = 0;
            while (pos <= content.size())
            {
                size_t nl = content.find('\n', pos);
                if (nl == string::npos)
                    nl = content.size();
                if (nl > pos)
                {
                    string line = content.substr(pos, nl - pos);
                    if (!isSeparatorRow(line))
                        rows.push_back(parseTableRow(line));
                }
                pos = nl + 1;
            }
            if (rows.empty() || rows[0].empty())
                return;

            int cols = (int)rows[0].size();
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_SizingStretchProp |
                                    ImGuiTableFlags_NoHostExtendX;

            ImGui::PushID(content.c_str());
            if (ImGui::BeginTable("##t", cols, flags))
            {
                for (auto &cell : rows[0])
                    ImGui::TableSetupColumn(cell.c_str());
                ImGui::TableHeadersRow();

                for (size_t r = 1; r < rows.size(); ++r)
                {
                    ImGui::TableNextRow();
                    for (int c = 0; c < cols; ++c)
                    {
                        ImGui::TableSetColumnIndex(c);
                        const string &cell = (c < (int)rows[r].size()) ? rows[r][c] : "";
                        string text;
                        text.reserve(cell.size());
                        for (char ch : cell)
                            if (ch != '`')
                                text += ch;
                        ImGui::TextWrapped("%s", text.c_str());
                    }
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
            ImGui::Spacing();
        }

        std::vector<WorkspacePanel::HelpSection> splitHelpMarkdown(const std::string &md)
        {
            using Kind = WorkspacePanel::HelpSection::Kind;
            std::vector<WorkspacePanel::HelpSection> out;

            enum class Mode
            {
                Text,
                Code,
                Table
            } mode = Mode::Text;
            std::string buf;

            auto flush = [&](Kind k)
            {
                if (!buf.empty())
                {
                    out.push_back({k, std::move(buf)});
                    buf.clear();
                }
            };

            size_t pos = 0;
            while (pos <= md.size())
            {
                size_t nl = md.find('\n', pos);
                bool hasNl = (nl != std::string::npos);
                if (!hasNl)
                    nl = md.size();

                std::string line = md.substr(pos, nl - pos);
                pos = hasNl ? nl + 1 : md.size() + 1;

                if (mode == Mode::Code)
                {
                    if (line.substr(0, 3) == "```")
                    {
                        flush(Kind::Code);
                        mode = Mode::Text;
                    }
                    else
                        buf += line + "\n";
                }
                else if (!line.empty() && line[0] == '|')
                {
                    if (mode == Mode::Text)
                        flush(Kind::Text);
                    mode = Mode::Table;
                    buf += line + "\n";
                }
                else if (line.substr(0, 3) == "```")
                {
                    if (mode == Mode::Table)
                        flush(Kind::Table);
                    else
                        flush(Kind::Text);
                    mode = Mode::Code;
                }
                else
                {
                    if (mode == Mode::Table)
                        flush(Kind::Table);
                    mode = Mode::Text;
                    buf += (line == "---" ? "***" : line) + "\n";
                }
            }
            if (mode == Mode::Text)
                flush(Kind::Text);
            else if (mode == Mode::Code)
                flush(Kind::Code);
            else
                flush(Kind::Table);

            return out;
        }
    }

    // -----------------------------------------------------------------------
    // Workspace tab bar
    // -----------------------------------------------------------------------

    void WorkspacePanel::drawWorkspace(Context &ctx)
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
        ImGui::SetNextWindowSize(ImVec2(900.0f, 600.0f), ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.06f, 0.08f, 0.05f));
        ImGui::Begin("Workspace", &ctx.showGameView, flags);

        if (ImGui::BeginTabBar("##WorkspaceTabs", ImGuiTabBarFlags_None))
        {
            ImGuiTabItemFlags sceneFlags = (ctx.requestedTab == SceneTab) ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags codeFlags = (ctx.requestedTab == CodeTab) ? ImGuiTabItemFlags_SetSelected : 0;
            ImGuiTabItemFlags helpFlags = (ctx.requestedTab == HelpTab) ? ImGuiTabItemFlags_SetSelected : 0;
            ctx.requestedTab = TabNone;

            if (ImGui::BeginTabItem("Scene", nullptr, sceneFlags))
            {
                ctx.activeTab = SceneTab;
                ctx.showGameView = true;
                drawSceneTab(ctx);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Code", nullptr, codeFlags))
            {
                ctx.activeTab = CodeTab;
                if (ctx.lastDrawnWorkspaceTab != CodeTab && codeEditor_)
                    codeEditor_->requestEditorFocus();
                drawCodeTab(ctx);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Help", nullptr, helpFlags))
            {
                ctx.activeTab = HelpTab;
                drawHelpTab(ctx);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
        ctx.lastDrawnWorkspaceTab = ctx.activeTab;

        ImGui::End();
        ImGui::PopStyleColor();
    }

    void WorkspacePanel::drawSceneTab(Context &ctx)
    {
        ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
        ImVec2 contentSize = ImGui::GetContentRegionAvail();

        ctx.gameViewportX = cursorScreen.x;
        ctx.gameViewportY = cursorScreen.y;
        ctx.gameViewportW = contentSize.x;
        ctx.gameViewportH = contentSize.y;
        ctx.hasGameViewport = (ctx.gameViewportW > 1.0f && ctx.gameViewportH > 1.0f);

        ctx.gameViewInput = (ImGui::IsWindowHovered(ImGuiFocusedFlags_RootAndChildWindows) ||
                             ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) &&
                            ctx.hasGameViewport;

        if (ctx.gameTextureId && ctx.hasGameViewport)
        {
            ImGui::Image(ctx.gameTextureId, contentSize, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        }
        else
        {
            ImGui::TextUnformatted("Game View (render target not ready)");
        }

        if (!ctx.testingMode && ctx.selection.entity != ecs::NullEntity && ctx.camera && ctx.hasGameViewport)
        {
            if (!ctx.selection.hasCache)
                ctx.refreshSelectionCache();

            glm::mat4 view = glm::lookAt(ctx.camera->getPosition(),
                                         ctx.camera->getPosition() + ctx.camera->getFront(),
                                         ctx.camera->getUp());
            glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                              ctx.gameViewportW / ctx.gameViewportH, 0.1f, 1000.0f);

            float viewF[16], projF[16], modelF[16];
            memcpy(viewF, glm::value_ptr(view), sizeof(viewF));
            memcpy(projF, glm::value_ptr(proj), sizeof(projF));

            glm::mat4 model = glm::translate(glm::mat4(1.0f), ctx.selection.position);
            model = glm::rotate(model, glm::radians(ctx.selection.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            model = glm::rotate(model, glm::radians(ctx.selection.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            model = glm::rotate(model, glm::radians(ctx.selection.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            model = glm::scale(model, ctx.selection.scale);
            memcpy(modelF, glm::value_ptr(model), sizeof(modelF));

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(ctx.gameViewportX, ctx.gameViewportY, ctx.gameViewportW, ctx.gameViewportH);

            ImGuizmo::OPERATION op = ImGuizmo::TRANSLATE;
            if (ctx.gizmoOp == GizmoOp::Rotate)
                op = ImGuizmo::ROTATE;
            else if (ctx.gizmoOp == GizmoOp::Scale)
                op = ImGuizmo::SCALE;
            ImGuizmo::MODE mode = ctx.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

            if (ImGuizmo::Manipulate(viewF, projF, op, mode, modelF))
            {
                float pos[3], rot[3], sc[3];
                ImGuizmo::DecomposeMatrixToComponents(modelF, pos, rot, sc);

                if (ctx.gizmoOp == GizmoOp::Rotate)
                {
                    // DecomposeMatrixToComponents always returns Y in [-90°, 90°].
                    // For Rz*Ry*Rx, every rotation has a complementary representation:
                    //   (X, Y, Z)  ↔  (X+180°, 180°-Y, Z+180°)
                    // Pick whichever is closer to the previously stored angles so the
                    // sequence stays continuous and doesn't flip at the ±90° singularity.
                    auto norm180 = [](float a) -> float
                    {
                        while (a > 180.f)
                            a -= 360.f;
                        while (a < -180.f)
                            a += 360.f;
                        return a;
                    };
                    float cX = norm180(rot[0] + 180.f);
                    float cY = (rot[1] >= 0.f ? 180.f : -180.f) - rot[1];
                    float cZ = norm180(rot[2] + 180.f);
                    float d1 = std::abs(norm180(rot[0] - ctx.selection.rotation.x)) +
                               std::abs(norm180(rot[1] - ctx.selection.rotation.y)) +
                               std::abs(norm180(rot[2] - ctx.selection.rotation.z));
                    float d2 = std::abs(norm180(cX - ctx.selection.rotation.x)) +
                               std::abs(norm180(cY - ctx.selection.rotation.y)) +
                               std::abs(norm180(cZ - ctx.selection.rotation.z));
                    if (d2 < d1)
                    {
                        rot[0] = cX;
                        rot[1] = cY;
                        rot[2] = cZ;
                    }
                }

                ctx.selection.position = glm::vec3(pos[0], pos[1], pos[2]);
                ctx.selection.rotation = glm::vec3(rot[0], rot[1], rot[2]);
                ctx.selection.scale = glm::vec3(sc[0], sc[1], sc[2]);
                ctx.applySelectionTransform();
            }
        }

        if (ctx.gameViewInput)
        {
            if (!ctx.testingMode)
            {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
                {
                    if (ctx.window)
                        ctx.window->setCursorDisabled(true);
                }
                else
                {
                    if (ctx.window)
                        ctx.window->setCursorDisabled(false);
                }
            }
            else
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (ctx.window)
                        ctx.window->setCursorDisabled(true);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    if (ctx.window)
                        ctx.window->setCursorDisabled(false);
                }
            }
        }
        else
        {
            if (ctx.window)
                ctx.window->setCursorDisabled(false);
        }
    }

    void WorkspacePanel::drawCodeTab(Context &ctx)
    {
        if (!codeEditor_)
            return;

        if (ui95::Button("New"))
            ImGui::OpenPopup("##NewScript");
        ImGui::SameLine();
        if (ui95::Button("Open from selection"))
        {
            ecs::Identity *ident = (ctx.scene && ctx.selection.entity != ecs::NullEntity)
                                       ? ctx.scene->registry().try_get<ecs::Identity>(ctx.selection.entity)
                                       : nullptr;
            if (ident && !ident->scriptPaths.empty())
            {
                codeEditor_->openFile(ident->scriptPaths.front());
            }
            else
            {
                ctx.addLog("Select an object with a script first (or attach one in the Inspector).",
                           ImVec4(0.9f, 0.7f, 0.4f, 1.0f));
            }
        }
        ImGui::SameLine();
        if (ui95::Button("Save"))
            codeEditor_->saveActive();
        ImGui::SameLine();
        if (ui95::Button("Apply (recompile)"))
        {
            if (ctx.scene && ctx.scriptHost && codeEditor_->hasActiveBuffer())
            {
                codeEditor_->saveActive();
                ctx.scene->resetScripts();
                int n = ctx.scene->loadScripts(*ctx.scriptHost);
                ctx.addLog("Recompiled " + std::to_string(n) + " script(s).",
                           ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
            }
        }
        ImGui::SameLine();
        if (ui95::Button("Attach to selection"))
        {
            if (ctx.scene && ctx.selection.entity != ecs::NullEntity)
            {
                if (codeEditor_->hasActiveBuffer())
                {
                    std::string path = codeEditor_->activePath();
                    auto &ident = ctx.scene->registry().get<ecs::Identity>(ctx.selection.entity);
                    // Append (dedup) so attaching adds to any scripts already present.
                    ecs::addScript(ctx.scene->registry(), ctx.selection.entity, path);
                    ctx.addLog("Attached script to " + ident.name + ": " + path,
                               ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
                }
                else
                {
                    ctx.addLog("Open a script in the editor first to attach it.",
                               ImVec4(0.9f, 0.7f, 0.4f, 1.0f));
                }
            }
            else
            {
                ctx.addLog("Select an object in the Scene tab first to attach the script to.",
                           ImVec4(0.9f, 0.7f, 0.4f, 1.0f));
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Ctrl+S to save");

        if (ImGui::BeginPopup("##NewScript"))
        {
            ImGui::Text("Path for new .cow file:");
            ImGui::SetNextItemWidth(360.0f);
            ui95::InputText("##newScriptPath", newScriptName, sizeof(newScriptName));
            if (ui95::Button("Create"))
            {
                try
                {
                    std::filesystem::path p(newScriptName);
                    if (p.has_parent_path())
                        std::filesystem::create_directories(p.parent_path());
                }
                catch (...)
                {
                }
                codeEditor_->openFile(newScriptName);
                codeEditor_->saveActive();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui95::Button("Cancel"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::Separator();
        codeEditor_->render();
    }

    void WorkspacePanel::drawHelpTab(Context &)
    {
        if (!helpMarkdownLoaded)
        {
            std::string raw;
            std::ifstream in("src/templates/help.md");
            if (!in)
            {
                raw = "Help file not found: src/templates/help.md";
            }
            else
            {
                std::ostringstream ss;
                ss << in.rdbuf();
                raw = ss.str();
                if (raw.empty())
                    raw = "(No help content available.)";
            }
            helpSections = splitHelpMarkdown(raw);
            helpMarkdownLoaded = true;
        }

        ImGui::MarkdownConfig mdConfig{};
        mdConfig.formatCallback = helpFormatCallback;
        mdConfig.formatFlags = ImGuiMarkdownFormatFlags_GithubStyle;
        mdConfig.headingFormats[0] = {ImGuiLayer::fontH1, true};
        mdConfig.headingFormats[1] = {ImGuiLayer::fontH2, true};
        mdConfig.headingFormats[2] = {ImGuiLayer::fontH3, false};

        ImGui::BeginChild("HelpRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (auto &section : helpSections)
        {
            switch (section.kind)
            {
            case HelpSection::Kind::Code:
                renderHelpCodeBlock(section.content);
                break;
            case HelpSection::Kind::Table:
                renderHelpTable(section.content);
                break;
            default:
                ImGui::Markdown(section.content.c_str(), section.content.size(), mdConfig);
                break;
            }
        }
        ImGui::EndChild();
    }

    // -----------------------------------------------------------------------
    // Gizmo toolbar overlay
    // -----------------------------------------------------------------------

    void WorkspacePanel::drawGizmoToolbar(Context &ctx)
    {
        if (!ctx.hasGameViewport)
            return;

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar;

        ImGui::SetNextWindowPos(ImVec2(ctx.gameViewportX + 8.0f, ctx.gameViewportY + 8.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.78f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(0.0f, 0.0f));

        if (ImGui::Begin("##GizmoToolbar", nullptr, flags))
        {
            const ImVec2 btnSize(26.0f, 26.0f);
            const ImVec4 activeBg(0.30f, 0.45f, 0.72f, 1.0f);
            const ImVec4 inactiveBg(0.0f, 0.0f, 0.0f, 0.0f);
            const ImVec4 tintOn(1.0f, 1.0f, 1.0f, 1.0f);
            const ImVec4 tintOff(0.60f, 0.60f, 0.60f, 0.75f);
            static bool iconsLoaded = false;
            static GLuint texTranslate = 0, texRotate = 0, texScale = 0;

            auto generateIconPixels = [](int size, unsigned char r, unsigned char g, unsigned char b)
            {
                std::vector<unsigned char> pixels(size * size * 4);
                int cx = size / 2;
                int cy = size / 2;
                float radius = size * 0.42f;
                for (int y = 0; y < size; ++y)
                {
                    for (int x = 0; x < size; ++x)
                    {
                        int idx = (y * size + x) * 4;
                        float dx = x - cx + 0.5f;
                        float dy = y - cy + 0.5f;
                        float d = std::sqrt(dx * dx + dy * dy);
                        if (d <= radius)
                        {
                            pixels[idx + 0] = r;
                            pixels[idx + 1] = g;
                            pixels[idx + 2] = b;
                            pixels[idx + 3] = 0xFF;
                        }
                        else
                        {
                            pixels[idx + 0] = 0x00;
                            pixels[idx + 1] = 0x00;
                            pixels[idx + 2] = 0x00;
                            pixels[idx + 3] = 0x00;
                        }
                    }
                }
                return pixels;
            };

            auto loadPNGTexture = [&](const std::string &path) -> GLuint
            {
                int w = 0, h = 0, comp = 0;
                unsigned char *data = stbi_load(path.c_str(), &w, &h, &comp, 4);
                if (!data)
                    return 0;
                GLuint tex = 0;
                glGenTextures(1, &tex);
                glBindTexture(GL_TEXTURE_2D, tex);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
                stbi_image_free(data);
                return tex;
            };

            auto ensureIcons = [&]()
            {
                if (iconsLoaded)
                    return;
                const int ICON_SIZE = 32;
                std::string base = std::string("engine_assets/icons/png/");
                GLuint t1 = loadPNGTexture(base + "up-down-left-right-solid-full.png");
                GLuint t2 = loadPNGTexture(base + "rotate-solid-full.png");
                GLuint t3 = loadPNGTexture(base + "expand-solid-full.png");

                auto createTex = [&](const std::vector<unsigned char> &px)
                {
                    GLuint tex = 0;
                    glGenTextures(1, &tex);
                    glBindTexture(GL_TEXTURE_2D, tex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ICON_SIZE, ICON_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glBindTexture(GL_TEXTURE_2D, 0);
                    return tex;
                };

                if (t1 && t2 && t3)
                {
                    texTranslate = t1;
                    texRotate = t2;
                    texScale = t3;
                }
                else
                {
                    texTranslate = createTex(generateIconPixels(ICON_SIZE, 0x33, 0x99, 0xFF));
                    texRotate = createTex(generateIconPixels(ICON_SIZE, 0x33, 0xCC, 0x66));
                    texScale = createTex(generateIconPixels(ICON_SIZE, 0xFF, 0x99, 0x33));
                }

                iconsLoaded = true;
            };

            ensureIcons();

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 3.0f));

            auto toolBtnTex = [&](const char *id, GLuint tex, const char *tooltip, GizmoOp op)
            {
                bool active = (ctx.gizmoOp == op);
                if (ImGui::ImageButton(id, (ImTextureID)(uintptr_t)tex, btnSize,
                                       ImVec2(0, 0), ImVec2(1, 1),
                                       active ? activeBg : inactiveBg,
                                       active ? tintOn : tintOff))
                    ctx.gizmoOp = op;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tooltip);
            };

            toolBtnTex("##gizmo_translate", texTranslate, "Translate (1)", GizmoOp::Translate);
            ImGui::SameLine();
            toolBtnTex("##gizmo_rotate", texRotate, "Rotate (2)", GizmoOp::Rotate);
            ImGui::SameLine();
            toolBtnTex("##gizmo_scale", texScale, "Scale (3)", GizmoOp::Scale);

            ImGui::PopStyleVar();
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }

    void WorkspacePanel::drawTestingOverlay(Context &ctx)
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        flags |= ImGuiWindowFlags_NoDocking;
        ImGuiViewport *vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 20.0f, vp->Pos.y + 20.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.35f);
        ImGui::Begin("TestingOverlay", nullptr, flags);
        ImGui::TextUnformatted("Testing Mode");
        if (ui95::Button("Stop Testing"))
            ctx.testingMode = false;
        ImGui::End();
    }
}
