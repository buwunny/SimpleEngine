#include "editor/panels/ConsolePanel.hpp"
#include "editor/Win95Widgets.hpp"

#include "core/Scene.hpp"

#include <algorithm>
#include <cstring>

namespace editor
{
    ConsolePanel::ConsolePanel() = default;

    namespace
    {
        // Trampoline state used to bridge ImGui's C-style InputText callback
        // through to ConsolePanel's history-navigation logic.
        struct CallbackBridge
        {
            ConsolePanel *panel;
            std::vector<std::string> *history;
            int *historyPos;
        };
    }

    void ConsolePanel::draw(Context &ctx)
    {
        ImGui::Begin("Debug Console", &ctx.showConsole);

        if (ui95::Button("Clear"))
            ctx.consoleLines.clear();
        ImGui::SameLine();
        ui95::Checkbox("Auto-scroll", &autoScroll);

        ImGui::Separator();
        ImGui::BeginChild("ConsoleScroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

        auto copyAllLogs = [&]()
        {
            std::string all;
            all.reserve(ctx.consoleLines.size() * 32);
            for (const auto &l : ctx.consoleLines)
            {
                all += l.text;
                all.push_back('\n');
            }
            ImGui::SetClipboardText(all.c_str());
        };

        for (size_t i = 0; i < ctx.consoleLines.size(); ++i)
        {
            const auto &line = ctx.consoleLines[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleColor(ImGuiCol_Text, line.color);
            ImGui::Selectable(line.text.c_str(), false, ImGuiSelectableFlags_AllowOverlap);
            ImGui::PopStyleColor();
            if (ImGui::BeginPopupContextItem("##log_line_ctx"))
            {
                if (ImGui::MenuItem("Copy Log"))
                    ImGui::SetClipboardText(line.text.c_str());
                if (ImGui::MenuItem("Copy All Logs"))
                    copyAllLogs();
                ImGui::Separator();
                if (ImGui::MenuItem("Clear Console"))
                    ctx.consoleLines.clear();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }

        if (ImGui::BeginPopupContextWindow("##console_bg_ctx",
                                           ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (ImGui::MenuItem("Copy All Logs", nullptr, false, !ctx.consoleLines.empty()))
                copyAllLogs();
            if (ImGui::MenuItem("Clear Console", nullptr, false, !ctx.consoleLines.empty()))
                ctx.consoleLines.clear();
            ImGui::EndPopup();
        }

        if (ctx.scrollToBottom || (autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
            ImGui::SetScrollHereY(1.0f);
        ctx.scrollToBottom = false;

        ImGui::EndChild();

        bool reclaimFocus = false;
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory;
        CallbackBridge bridge{this, &consoleHistory, &historyPos};
        if (ui95::InputText("Command", consoleInput, sizeof(consoleInput), flags,
                             &ConsolePanel::textEditCallback, &bridge))
        {
            std::string commandLine(consoleInput);
            if (!commandLine.empty())
                execCommand(ctx, commandLine);
            std::fill(std::begin(consoleInput), std::end(consoleInput), 0);
            reclaimFocus = true;
        }

        ImGui::SetItemDefaultFocus();
        if (reclaimFocus)
            ImGui::SetKeyboardFocusHere(-1);

        ImGui::End();
    }

    void ConsolePanel::execCommand(Context &ctx, const std::string &commandLine)
    {
        ctx.addLog("> " + commandLine, ImVec4(0.7f, 0.8f, 1.0f, 1.0f));

        consoleHistory.push_back(commandLine);
        historyPos = -1;

        Scene *scene = ctx.scene;
        if (!scene)
        {
            ctx.addLog("No scene loaded.", ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            return;
        }

        if (commandLine == "help")
        {
            ctx.addLog("Commands: help, clear, reload, save, save_as <path>, spawn_cow");
            return;
        }

        if (commandLine == "clear")
        {
            ctx.consoleLines.clear();
            return;
        }

        if (commandLine == "reload")
        {
            ctx.clearSelection();
            scene->forceReload();
            ctx.addLog("Scene reload requested.");
            return;
        }

        if (commandLine == "spawn_cow")
        {
            addObjectToScene(ctx, "cow");
            return;
        }

        if (commandLine == "save")
        {
            std::string savePath = ctx.lastSavePath;
            if (savePath.empty())
                savePath = scene->getScenePath().empty() ? "scenes/scene.json" : scene->getScenePath();
            if (scene->saveToJSON(savePath))
            {
                ctx.lastSavePath = savePath;
                ctx.addLog("Scene saved to " + savePath, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
            }
            else
            {
                ctx.addLog("Scene save failed for " + savePath, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            }
            return;
        }

        const std::string prefix = "save_as ";
        if (commandLine.rfind(prefix, 0) == 0)
        {
            std::string path = commandLine.substr(prefix.size());
            if (path.empty())
            {
                ctx.addLog("save_as requires a path.", ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
                return;
            }
            if (scene->saveToJSON(path))
            {
                ctx.lastSavePath = path;
                ctx.addLog("Scene saved to " + path, ImVec4(0.6f, 0.9f, 0.6f, 1.0f));
            }
            else
            {
                ctx.addLog("Scene save failed for " + path, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
            }
            return;
        }

        ctx.addLog("Unknown command: " + commandLine, ImVec4(0.9f, 0.5f, 0.5f, 1.0f));
    }

    int ConsolePanel::textEditCallback(ImGuiInputTextCallbackData *data)
    {
        auto *bridge = static_cast<CallbackBridge *>(data->UserData);
        if (!bridge)
            return 0;

        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            auto &history = *bridge->history;
            int &historyPos = *bridge->historyPos;
            const int prevHistoryPos = historyPos;
            if (data->EventKey == ImGuiKey_UpArrow)
            {
                if (historyPos == -1)
                    historyPos = static_cast<int>(history.size()) - 1;
                else if (historyPos > 0)
                    historyPos--;
            }
            else if (data->EventKey == ImGuiKey_DownArrow)
            {
                if (historyPos != -1)
                {
                    if (++historyPos >= static_cast<int>(history.size()))
                        historyPos = -1;
                }
            }

            if (prevHistoryPos != historyPos)
            {
                const char *historyStr = (historyPos >= 0) ? history[historyPos].c_str() : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, historyStr);
            }
        }

        return 0;
    }
}
