#ifndef IMGUI_LAYER_HPP
#define IMGUI_LAYER_HPP
#include <memory>
#include <imgui.h>

struct Window;

class ImGuiLayer
{
public:
    // Windows 95, in daylight and after dark.
    enum class Theme
    {
        Win95,
        Win95Dark,
    };

    ImGuiLayer(Window *window);
    ~ImGuiLayer();

    void newFrame();
    void render();

    // Restyles the live context. Safe to call between frames at any time; both
    // themes reset every style field first, so switching never leaves a value
    // behind from the other one.
    static void applyTheme(Theme theme);
    static Theme currentTheme();

    // Heading fonts loaded at startup (null = use default font).
    static ImFont *fontH1;
    static ImFont *fontH2;
    static ImFont *fontH3;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif // IMGUI_LAYER_HPP