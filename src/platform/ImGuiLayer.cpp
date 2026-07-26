#include "platform/ImGuiLayer.hpp"
#include "platform/Window.hpp"
#include "editor/Win95Widgets.hpp"
#include <imgui.h>
#include <cstdio>

ImFont *ImGuiLayer::fontH1 = nullptr;
ImFont *ImGuiLayer::fontH2 = nullptr;
ImFont *ImGuiLayer::fontH3 = nullptr;

namespace
{
    // Try the absolute ASSET_ROOT path first (native only), then the relative CWD path.
    // On Emscripten, ASSET_ROOT is the native build machine's path and does not exist
    // in the WASM virtual filesystem, so we skip it and rely on the preloaded relative path.
    ImFont *loadFont(const char *relPath, float size)
    {
        ImGuiIO &io = ImGui::GetIO();
#if defined(ASSET_ROOT) && !defined(__EMSCRIPTEN__)
        {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s/%s", ASSET_ROOT, relPath);
            ImFont *f = io.Fonts->AddFontFromFileTTF(buf, size);
            if (f)
                return f;
        }
#endif
        return io.Fonts->AddFontFromFileTTF(relPath, size);
    }

    // Same two candidate paths, but only reports whether one is readable.
    // AddFontFromFileTTF() asserts on a missing file, so optional assets have
    // to be probed before they are handed to it.
    bool findAsset(const char *relPath, char *out, size_t outSize)
    {
#if defined(ASSET_ROOT) && !defined(__EMSCRIPTEN__)
        {
            std::snprintf(out, outSize, "%s/%s", ASSET_ROOT, relPath);
            if (std::FILE *f = std::fopen(out, "rb"))
            {
                std::fclose(f);
                return true;
            }
        }
#endif
        std::snprintf(out, outSize, "%s", relPath);
        if (std::FILE *f = std::fopen(out, "rb"))
        {
            std::fclose(f);
            return true;
        }
        return false;
    }

    // Loads a pixel font if the user has dropped one in, else returns null so
    // the caller can fall back. Oversampling off and PixelSnapH on are what
    // keep a bitmap-style face crisp instead of smeared.
    ImFont *loadPixelFont(const char *relPath, float size)
    {
        char path[512];
        if (!findAsset(relPath, path, sizeof(path)))
            return nullptr;

        ImFontConfig cfg;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &cfg);
    }

    ImGuiLayer::Theme activeTheme = ImGuiLayer::Theme::Win95;

    ImVec4 rgb(int r, int g, int b, float a = 1.0f)
    {
        return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
    }

    // Every colour a 95 theme needs. Both variants are the same UI with a
    // different set of these, so the mapping onto ImGui's ~60 style colours is
    // written once in applySkin() below.
    struct Skin
    {
        ImVec4 face;        // window and button face
        ImVec4 hiOuter;     // brightest bevel edge
        ImVec4 hiInner;     // second bevel pixel, light side
        ImVec4 loInner;     // second bevel pixel, dark side
        ImVec4 loOuter;     // darkest bevel edge
        ImVec4 pressed;     // button face while held
        ImVec4 field;       // interior of an editable field
        ImVec4 text;
        ImVec4 textDim;
        ImVec4 titleActive; // focused title bar / dock tab strip
        ImVec4 titleIdle;
        ImVec4 select;      // selected row, active header
        ImVec4 selectHot;
        ImVec4 track;       // scrollbar trough
    };

    Skin lightSkin()
    {
        Skin s;
        s.face = rgb(192, 192, 192);
        s.hiOuter = rgb(255, 255, 255);
        s.hiInner = rgb(223, 223, 223);
        s.loInner = rgb(128, 128, 128);
        s.loOuter = rgb(10, 10, 10);
        s.pressed = rgb(176, 176, 176);
        s.field = rgb(255, 255, 255);
        s.text = rgb(0, 0, 0);
        s.textDim = rgb(128, 128, 128);
        s.titleActive = rgb(0, 0, 128);
        s.titleIdle = rgb(223, 223, 223);
        // Authentic selection is navy under white text, but ImGui has a single
        // global text colour and this theme's text is black. This is the
        // lightest blue that still reads as "selected" beneath it.
        s.select = rgb(166, 184, 216);
        s.selectHot = rgb(146, 166, 202);
        s.track = rgb(223, 223, 223);
        return s;
    }

    Skin darkSkin()
    {
        Skin s;
        s.face = rgb(60, 60, 60);
        s.hiOuter = rgb(106, 106, 106);
        s.hiInner = rgb(78, 78, 78);
        s.loInner = rgb(38, 38, 38);
        s.loOuter = rgb(10, 10, 10);
        s.pressed = rgb(48, 48, 48);
        s.field = rgb(28, 28, 28);
        s.text = rgb(232, 232, 232);
        s.textDim = rgb(144, 144, 144);
        s.titleActive = rgb(38, 65, 143);
        s.titleIdle = rgb(48, 48, 48);
        // Light text means the dark variant can afford the real navy selection.
        s.select = rgb(38, 65, 143);
        s.selectHot = rgb(52, 84, 173);
        s.track = rgb(38, 38, 38);
        return s;
    }

    void applySkin(ImGuiStyle &style, const Skin &s)
    {
        const ImVec4 clear = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        style.WindowRounding = 0.0f;
        style.ChildRounding = 0.0f;
        style.FrameRounding = 0.0f;
        style.PopupRounding = 0.0f;
        style.GrabRounding = 0.0f;
        style.TabRounding = 0.0f;
        style.ScrollbarRounding = 0.0f;

        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.TabBarBorderSize = 1.0f;

        style.WindowPadding = ImVec2(6.0f, 6.0f);
        style.FramePadding = ImVec2(4.0f, 3.0f);
        style.ItemSpacing = ImVec2(6.0f, 4.0f);
        style.ItemInnerSpacing = ImVec2(4.0f, 3.0f);
        style.ScrollbarSize = 16.0f;
        style.GrabMinSize = 16.0f;
        style.WindowMenuButtonPosition = ImGuiDir_Left;

        // Hard pixel edges. Without this the 1px borders and the bevels drawn
        // by ui95 land on half-pixels and go soft, which is the one thing this
        // look cannot have.
        style.AntiAliasedLines = false;
        style.AntiAliasedLinesUseTex = false;
        style.AntiAliasedFill = false;

        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = s.text;
        colors[ImGuiCol_TextDisabled] = s.textDim;
        colors[ImGuiCol_WindowBg] = s.face;
        colors[ImGuiCol_ChildBg] = s.face;
        colors[ImGuiCol_PopupBg] = s.face;
        // ui95 paints the real two-tone bevel; the stock border is left as a
        // single quiet outline for the widgets that aren't wrapped.
        colors[ImGuiCol_Border] = s.loInner;
        // BorderShadow would add a second outline offset by (1,1), which reads
        // as a groove rather than a bevel. Leave it off.
        colors[ImGuiCol_BorderShadow] = clear;

        colors[ImGuiCol_FrameBg] = s.field;
        colors[ImGuiCol_FrameBgHovered] = s.field;
        colors[ImGuiCol_FrameBgActive] = s.field;

        // TitleBg also paints the strip behind a dock node's tabs, and the
        // editor is docked wall to wall -- a dark strip there reads as a heavy
        // band between every panel, so keep it quiet and let TitleBgActive be
        // the thing that marks focus.
        colors[ImGuiCol_TitleBg] = s.titleIdle;
        colors[ImGuiCol_TitleBgActive] = s.titleActive;
        colors[ImGuiCol_TitleBgCollapsed] = s.titleIdle;
        colors[ImGuiCol_MenuBarBg] = s.face;

        colors[ImGuiCol_ScrollbarBg] = s.track;
        colors[ImGuiCol_ScrollbarGrab] = s.face;
        colors[ImGuiCol_ScrollbarGrabHovered] = s.face;
        colors[ImGuiCol_ScrollbarGrabActive] = s.pressed;

        colors[ImGuiCol_CheckMark] = s.text;
        colors[ImGuiCol_SliderGrab] = s.face;
        colors[ImGuiCol_SliderGrabActive] = s.pressed;

        colors[ImGuiCol_Button] = s.face;
        colors[ImGuiCol_ButtonHovered] = s.face;
        colors[ImGuiCol_ButtonActive] = s.pressed;

        colors[ImGuiCol_Header] = s.select;
        colors[ImGuiCol_HeaderHovered] = s.select;
        colors[ImGuiCol_HeaderActive] = s.selectHot;

        colors[ImGuiCol_Separator] = s.loInner;
        colors[ImGuiCol_SeparatorHovered] = s.loInner;
        colors[ImGuiCol_SeparatorActive] = s.loOuter;

        colors[ImGuiCol_ResizeGrip] = s.face;
        colors[ImGuiCol_ResizeGripHovered] = s.hiInner;
        colors[ImGuiCol_ResizeGripActive] = s.pressed;

        // Unselected tabs sit below the panel face; the selected one matches it
        // so the tab reads as continuous with its page.
        colors[ImGuiCol_Tab] = s.pressed;
        colors[ImGuiCol_TabHovered] = s.hiInner;
        colors[ImGuiCol_TabSelected] = s.face;
        colors[ImGuiCol_TabDimmed] = s.pressed;
        colors[ImGuiCol_TabDimmedSelected] = s.face;
        colors[ImGuiCol_TabSelectedOverline] = clear;
        colors[ImGuiCol_TabDimmedSelectedOverline] = clear;

        colors[ImGuiCol_DockingPreview] = ImVec4(s.titleActive.x, s.titleActive.y, s.titleActive.z, 0.45f);
        colors[ImGuiCol_DockingEmptyBg] = s.loInner;

        colors[ImGuiCol_PlotLines] = s.titleActive;
        colors[ImGuiCol_PlotLinesHovered] = s.text;
        colors[ImGuiCol_PlotHistogram] = s.titleActive;
        colors[ImGuiCol_PlotHistogramHovered] = s.text;

        colors[ImGuiCol_TableHeaderBg] = s.face;
        colors[ImGuiCol_TableBorderStrong] = s.loInner;
        colors[ImGuiCol_TableBorderLight] = s.hiInner;
        colors[ImGuiCol_TableRowBg] = clear;
        colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.0f, 0.0f, 0.0f, 0.04f);

        colors[ImGuiCol_TextSelectedBg] = s.select;
        colors[ImGuiCol_InputTextCursor] = s.text;
        colors[ImGuiCol_NavCursor] = s.titleActive;
        colors[ImGuiCol_DragDropTarget] = s.titleActive;
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);

        ui95::Palette bevels;
        bevels.hiOuter = ImGui::GetColorU32(s.hiOuter);
        bevels.hiInner = ImGui::GetColorU32(s.hiInner);
        bevels.loInner = ImGui::GetColorU32(s.loInner);
        bevels.loOuter = ImGui::GetColorU32(s.loOuter);
        ui95::setPalette(bevels);
    }
}
#if defined(__EMSCRIPTEN__)
#include "platform/imgui_impl_emscripten.h"
#include "imgui_impl_opengl3.h"
#else
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#endif

struct ImGuiLayer::Impl
{
    Window *window;
#if defined(__EMSCRIPTEN__)
    // nothing else needed
#else
    GLFWwindow *glfwWindow;
#endif
};

ImGuiLayer::ImGuiLayer(Window *window) : impl(new Impl())
{
    impl->window = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Optional pixel UI font for the 95 themes: grab "Pixelify Sans" from
    // Google Fonts and drop the TTFs at these paths. A monospace face is the
    // biggest tell that this isn't really Windows, so it's worth having -- but
    // it stays optional, and the editor falls back to JetBrains Mono without
    // it. Sizes are whole numbers because pixel faces only look right on the
    // integer grid.
    const char *pixel = "engine_assets/fonts/PixelifySans-Regular.ttf";
    const char *pixelBold = "engine_assets/fonts/PixelifySans-Bold.ttf";

    // JetBrains Mono, the fallback. The first font added becomes the default.
    const char *reg = "engine_assets/fonts/JetBrainsMono-2.304/fonts/ttf/JetBrainsMono-Regular.ttf";
    const char *bold = "engine_assets/fonts/JetBrainsMono-2.304/fonts/ttf/JetBrainsMono-Bold.ttf";
    const char *semi = "engine_assets/fonts/JetBrainsMono-2.304/fonts/ttf/JetBrainsMono-SemiBold.ttf";

    if (!loadPixelFont(pixel, 20.0f))       // index 0 → default UI font
        loadFont(reg, 20.0f);
    fontH1 = loadPixelFont(pixelBold, 28.0f);
    if (!fontH1)
        fontH1 = loadFont(bold, 28.0f);
    fontH2 = loadPixelFont(pixelBold, 24.0f);
    if (!fontH2)
        fontH2 = loadFont(bold, 24.0f);
    fontH3 = loadPixelFont(pixel, 18.0f);
    if (!fontH3)
        fontH3 = loadFont(semi, 18.0f);

    applyTheme(Theme::Win95);
#if defined(__EMSCRIPTEN__)
    ImGui_ImplEmscripten_Init();
    ImGui_ImplOpenGL3_Init("#version 300 es");
#else
    impl->glfwWindow = window->getWindow();
    ImGui_ImplGlfw_InitForOpenGL(impl->glfwWindow, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
}

void ImGuiLayer::applyTheme(Theme theme)
{
    ImGuiStyle &style = ImGui::GetStyle();
    // Start from stock defaults so nothing carries over from the other theme --
    // padding, border sizes and the antialiasing flags all differ between them.
    style = ImGuiStyle();
    applySkin(style, theme == Theme::Win95Dark ? darkSkin() : lightSkin());
    activeTheme = theme;
}

ImGuiLayer::Theme ImGuiLayer::currentTheme()
{
    return activeTheme;
}

ImGuiLayer::~ImGuiLayer()
{
#if defined(__EMSCRIPTEN__)
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplEmscripten_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
#endif
    ImGui::DestroyContext();
}

void ImGuiLayer::newFrame()
{
#if defined(__EMSCRIPTEN__)
    ImGui_ImplEmscripten_NewFrame();
    ImGui_ImplOpenGL3_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
#endif
    ImGui::NewFrame();
}

void ImGuiLayer::render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
