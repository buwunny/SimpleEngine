#include "editor/Win95Widgets.hpp"

namespace
{
    // Sensible defaults so the widgets draw something reasonable even if they
    // are used before ImGuiLayer has pushed a theme.
    ui95::Palette activePalette = {
        IM_COL32(255, 255, 255, 255),
        IM_COL32(223, 223, 223, 255),
        IM_COL32(128, 128, 128, 255),
        IM_COL32(10, 10, 10, 255),
    };

    // One pass of the bevel: `inset` 0 is the outermost pixel ring, 1 the next
    // one in. Drawn as 1px filled rects rather than lines so the result lands
    // on exact pixels regardless of the draw list's line settings.
    void edgeRing(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max,
                  float inset, ImU32 topLeft, ImU32 bottomRight)
    {
        const float x0 = min.x + inset;
        const float y0 = min.y + inset;
        const float x1 = max.x - inset;
        const float y1 = max.y - inset;
        if (x1 - x0 < 2.0f || y1 - y0 < 2.0f)
            return;

        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y0 + 1.0f), topLeft);         // top
        drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + 1.0f, y1), topLeft);         // left
        drawList->AddRectFilled(ImVec2(x0, y1 - 1.0f), ImVec2(x1, y1), bottomRight);     // bottom
        drawList->AddRectFilled(ImVec2(x1 - 1.0f, y0), ImVec2(x1, y1), bottomRight);     // right
    }
}

namespace ui95
{
    void setPalette(const Palette &palette)
    {
        activePalette = palette;
    }

    void bevel(ImDrawList *drawList, const ImVec2 &min, const ImVec2 &max, Bevel style)
    {
        const Palette &p = activePalette;
        switch (style)
        {
        case Bevel::Raised:
            edgeRing(drawList, min, max, 0.0f, p.hiOuter, p.loOuter);
            edgeRing(drawList, min, max, 1.0f, p.hiInner, p.loInner);
            break;
        case Bevel::Pressed:
            edgeRing(drawList, min, max, 0.0f, p.loOuter, p.hiOuter);
            edgeRing(drawList, min, max, 1.0f, p.loInner, p.hiInner);
            break;
        case Bevel::Sunken:
            edgeRing(drawList, min, max, 0.0f, p.loInner, p.hiOuter);
            edgeRing(drawList, min, max, 1.0f, p.loOuter, p.hiInner);
            break;
        }
    }

    bool Button(const char *label, const ImVec2 &size)
    {
        // The stock 1px border would sit underneath the bevel and darken its
        // light edge, so turn it off for the duration of the call.
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const bool clicked = ImGui::Button(label, size);
        ImGui::PopStyleVar();
        bevel(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
              ImGui::IsItemActive() ? Bevel::Pressed : Bevel::Raised);
        return clicked;
    }

    bool SmallButton(const char *label)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const bool clicked = ImGui::SmallButton(label);
        ImGui::PopStyleVar();
        bevel(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
              ImGui::IsItemActive() ? Bevel::Pressed : Bevel::Raised);
        return clicked;
    }

    bool Checkbox(const char *label, bool *value)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        const bool changed = ImGui::Checkbox(label, value);
        ImGui::PopStyleVar();

        // Only the tick box is recessed; the label sits on the window face.
        // ImGui draws the check inset from the box edge, so the bevel never
        // eats into it.
        const ImVec2 min = ImGui::GetItemRectMin();
        const float side = ImGui::GetFrameHeight();
        bevel(ImGui::GetWindowDrawList(), min, ImVec2(min.x + side, min.y + side), Bevel::Sunken);
        return changed;
    }

    namespace
    {
        // Every framed widget is submitted the same way: suppress the stock
        // border, run the real thing, then recess the frame it left behind.
        template <typename Fn>
        bool framed(Fn &&submit)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            const bool changed = submit();
            ImGui::PopStyleVar();
            sinkLastItem();
            return changed;
        }
    }

    bool InputText(const char *label, char *buf, size_t bufSize, ImGuiInputTextFlags flags,
                   ImGuiInputTextCallback callback, void *userData)
    {
        return framed([&] { return ImGui::InputText(label, buf, bufSize, flags, callback, userData); });
    }

    bool InputFloat(const char *label, float *v, float step, float stepFast,
                    const char *format, ImGuiInputTextFlags flags)
    {
        return framed([&] { return ImGui::InputFloat(label, v, step, stepFast, format, flags); });
    }

    bool DragFloat(const char *label, float *v, float speed, float min, float max,
                   const char *format, ImGuiSliderFlags flags)
    {
        return framed([&] { return ImGui::DragFloat(label, v, speed, min, max, format, flags); });
    }

    bool DragFloat3(const char *label, float v[3], float speed, float min, float max,
                    const char *format, ImGuiSliderFlags flags)
    {
        return framed([&] { return ImGui::DragFloat3(label, v, speed, min, max, format, flags); });
    }

    bool SliderFloat(const char *label, float *v, float min, float max,
                     const char *format, ImGuiSliderFlags flags)
    {
        return framed([&] { return ImGui::SliderFloat(label, v, min, max, format, flags); });
    }

    bool SliderFloat2(const char *label, float v[2], float min, float max,
                      const char *format, ImGuiSliderFlags flags)
    {
        return framed([&] { return ImGui::SliderFloat2(label, v, min, max, format, flags); });
    }

    bool SliderInt(const char *label, int *v, int min, int max,
                   const char *format, ImGuiSliderFlags flags)
    {
        return framed([&] { return ImGui::SliderInt(label, v, min, max, format, flags); });
    }

    bool Combo(const char *label, int *currentItem, const char *itemsSeparatedByZeros,
               int popupMaxHeightInItems)
    {
        return framed([&]
                      { return ImGui::Combo(label, currentItem, itemsSeparatedByZeros, popupMaxHeightInItems); });
    }

    void sinkLastItem()
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        // CalcItemWidth() is the width the widget just used for its frame; the
        // item rect additionally covers the label, which must stay unframed.
        const float frameRight = min.x + ImGui::CalcItemWidth();
        const ImVec2 max(frameRight < itemMax.x ? frameRight : itemMax.x, itemMax.y);
        bevel(ImGui::GetWindowDrawList(), min, max, Bevel::Sunken);
    }
}
