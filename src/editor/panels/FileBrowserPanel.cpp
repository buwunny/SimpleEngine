#include "editor/panels/FileBrowserPanel.hpp"
#include "editor/Win95Widgets.hpp"

#include "core/Scene.hpp"
#include "app/CodeEditor.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <vector>

namespace editor
{
    namespace
    {
        namespace fs = std::filesystem;

        // Locate an asset directory by trying the relative path first, then
        // ASSET_ROOT/<rel> on native, then a couple of common parent dirs.
        fs::path resolveAssetDir(const std::string &rel)
        {
            std::vector<fs::path> candidates = {fs::path(rel)};
#if defined(ASSET_ROOT) && !defined(__EMSCRIPTEN__)
            candidates.emplace_back(fs::path(ASSET_ROOT) / rel);
#endif
            candidates.emplace_back(fs::path("./") / rel);
            candidates.emplace_back(fs::path("../") / rel);
            candidates.emplace_back(fs::path("/") / rel); // Emscripten preload root
            for (auto &c : candidates)
            {
                std::error_code ec;
                if (fs::exists(c, ec) && fs::is_directory(c, ec))
                    return c;
            }
            return {};
        }

        // Recursively scan `rootRel` for files with `ext` (e.g. ".cow") and return
        // paths formatted as "<rootRel>/<sub/path>" (forward slashes).
        std::vector<std::string> scanFiles(const std::string &rootRel, const std::string &ext)
        {
            std::vector<std::string> out;
            fs::path dir = resolveAssetDir(rootRel);
            if (dir.empty())
                return out;

            std::error_code ec;
            for (auto it = fs::recursive_directory_iterator(dir, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            {
                const fs::directory_entry &entry = *it;
                if (!entry.is_regular_file(ec))
                    continue;
                if (entry.path().extension() != ext)
                    continue;
                fs::path rel = fs::relative(entry.path(), dir, ec);
                if (ec)
                    continue;
                out.push_back(rootRel + "/" + rel.generic_string());
            }
            std::sort(out.begin(), out.end());
            return out;
        }
    }

    void FileBrowserPanel::refresh(Context &ctx)
    {
        ctx.fileBrowserScripts = scanFiles("scripts", ".cow");
        ctx.fileBrowserModels = scanFiles("models", ".obj");
        ctx.fileBrowserScenes = scanFiles("scenes", ".json");
        loaded = true;
    }

    void FileBrowserPanel::draw(Context &ctx)
    {
        ImGui::Begin("Files", &ctx.showFiles);

        if (!loaded)
            refresh(ctx);

        if (ui95::Button("Refresh"))
            refresh(ctx);
        ImGui::SameLine();
        filter.Draw("##fbfilter", 180.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("(double-click to open / spawn / load)");

        ImGui::Separator();

        auto drawSection = [&](const char *label, const std::vector<std::string> &entries,
                               const std::function<void(const std::string &)> &onActivate)
        {
            char header[64];
            std::snprintf(header, sizeof(header), "%s (%zu)", label, entries.size());
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
                return;
            ImGui::Indent();
            if (entries.empty())
                ImGui::TextDisabled("No files found.");
            for (const auto &path : entries)
            {
                if (!filter.PassFilter(path.c_str()))
                    continue;
                ImGui::PushID(path.c_str());
                ImGui::Selectable(path.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    onActivate(path);
                ImGui::PopID();
            }
            ImGui::Unindent();
        };

        drawSection("Scripts", ctx.fileBrowserScripts, [&](const std::string &path)
                    {
            if (ctx.codeEditor)
            {
                ctx.codeEditor->openFile(path);
                ctx.requestedTab = CodeTab;
            } });

        drawSection("Models", ctx.fileBrowserModels, [&](const std::string &path)
                    { spawnStaticObjectFromMesh(ctx, path);
                      ctx.requestedTab = SceneTab; });

        drawSection("Scenes", ctx.fileBrowserScenes, [&](const std::string &path)
                    {
            if (ctx.scene && ctx.scene->loadFromJSON(path))
            {
                ctx.clearSelection();
                ctx.addLog("Loaded scene " + path, ImVec4(0.7f, 0.95f, 0.7f, 1.0f));
                ctx.requestedTab = SceneTab;
            }
            else
            {
                ctx.addLog("Failed to load scene " + path, ImVec4(0.95f, 0.5f, 0.5f, 1.0f));
            } });

        ImGui::End();
    }
}
