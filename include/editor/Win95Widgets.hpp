#ifndef WIN95_WIDGETS_HPP
#define WIN95_WIDGETS_HPP
#include <imgui.h>

// Windows 95 chrome for ImGui widgets.
//
// ImGui draws a widget frame as one fill plus an optional single-colour 1px
// border, so the defining feature of the 95 look -- a 2px bevel that is light
// on the top/left and dark on the bottom/right -- can't be expressed through
// ImGuiStyle. These wrappers call the stock widget (so layout, IDs, keyboard
// nav and every other behaviour stay ImGui's) and then paint the bevel over
// the edges of the rect it just occupied.
//
// Colours come from the active theme via setPalette(), which ImGuiLayer calls
// whenever the theme changes.
namespace ui95
{
    struct Palette
    {
        ImU32 hiOuter;   // brightest edge, outermost pixel of a raised widget
        ImU32 hiInner;   // second pixel in
        ImU32 loInner;   // second pixel in on the shadowed side
        ImU32 loOuter;   // darkest edge
    };

    enum class Bevel
    {
        Raised,  // buttons at rest
        Pressed, // buttons while held: the bevel inverts
        Sunken,  // text fields, checkboxes, anything recessed into the face
    };

    void setPalette(const Palette &palette);

    // Paints a 2px bevel just inside the given rect.
    void bevel(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max, Bevel style);

    // Stock widgets, beveled. Signatures mirror the ImGui originals exactly, so
    // a call site only changes namespace -- including the common
    // `if (ui95::InputText(...))` form, which a post-call decorator couldn't
    // have handled without restructuring the surrounding code.
    bool Button(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f));
    bool SmallButton(const char *label);
    bool Checkbox(const char *label, bool *value);

    bool InputText(const char *label, char *buf, size_t bufSize, ImGuiInputTextFlags flags = 0,
                   ImGuiInputTextCallback callback = nullptr, void *userData = nullptr);
    bool InputFloat(const char *label, float *v, float step = 0.0f, float stepFast = 0.0f,
                    const char *format = "%.3f", ImGuiInputTextFlags flags = 0);
    bool DragFloat(const char *label, float *v, float speed = 1.0f, float min = 0.0f, float max = 0.0f,
                   const char *format = "%.3f", ImGuiSliderFlags flags = 0);
    bool DragFloat3(const char *label, float v[3], float speed = 1.0f, float min = 0.0f, float max = 0.0f,
                    const char *format = "%.3f", ImGuiSliderFlags flags = 0);
    bool SliderFloat(const char *label, float *v, float min, float max,
                     const char *format = "%.3f", ImGuiSliderFlags flags = 0);
    bool SliderFloat2(const char *label, float v[2], float min, float max,
                      const char *format = "%.3f", ImGuiSliderFlags flags = 0);
    bool SliderInt(const char *label, int *v, int min, int max,
                   const char *format = "%d", ImGuiSliderFlags flags = 0);
    bool Combo(const char *label, int *currentItem, const char *itemsSeparatedByZeros,
               int popupMaxHeightInItems = -1);

    // Recesses the framed widget that was just submitted. The wrappers above
    // use this; call it directly for any framed widget they don't cover. Only
    // the frame is beveled, never the label ImGui draws beside it.
    void sinkLastItem();
}

#endif // WIN95_WIDGETS_HPP
