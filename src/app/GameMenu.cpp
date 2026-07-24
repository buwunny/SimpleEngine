#include "app/GameMenu.hpp"

#include "platform/Window.hpp"
#include "render/TextRenderer.hpp"

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten.h>
#else
#include <glad/glad.h>
#endif

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace
{
    // Settings are a single packed int rather than a document. Everything the
    // menu owns is a bool or a three-way enum, so a blob format (and the JSON
    // dependency, and the string marshalling out of localStorage that comes
    // with it) would be pure overhead. One int also means the web and native
    // paths differ by four lines instead of a serializer each.
    //
    //   bits  0..15  feature flags
    //   bits 16..23  quality
    //   bits 24..31  format version
    constexpr int kSettingsVersion = 1;

    enum Flag : int
    {
        FlagSky = 1 << 0,
        FlagSun = 1 << 1,
        FlagGrid = 1 << 2,
        FlagFog = 1 << 3,
        FlagNeon = 1 << 4,
        FlagBloom = 1 << 5,
        FlagScanlines = 1 << 6,
        FlagExplosionLight = 1 << 7,
        FlagWireframeFill = 1 << 8,
    };

    int packSettings(const editor::VFX &v)
    {
        int flags = 0;
        if (v.skyEnabled) flags |= FlagSky;
        if (v.sunEnabled) flags |= FlagSun;
        if (v.gridEnabled) flags |= FlagGrid;
        if (v.fogEnabled) flags |= FlagFog;
        if (v.neonEnabled) flags |= FlagNeon;
        if (v.bloomEnabled) flags |= FlagBloom;
        if (v.scanlinesEnabled) flags |= FlagScanlines;
        if (v.explosionLightEnabled) flags |= FlagExplosionLight;
        if (v.wireframeFill) flags |= FlagWireframeFill;
        return (kSettingsVersion << 24) | (static_cast<int>(v.quality) << 16) | flags;
    }

    void unpackSettings(int packed, editor::VFX &v)
    {
        if (((packed >> 24) & 0xFF) != kSettingsVersion)
            return; // written by a different build: keep the defaults
        v.skyEnabled = (packed & FlagSky) != 0;
        v.sunEnabled = (packed & FlagSun) != 0;
        v.gridEnabled = (packed & FlagGrid) != 0;
        v.fogEnabled = (packed & FlagFog) != 0;
        v.neonEnabled = (packed & FlagNeon) != 0;
        v.bloomEnabled = (packed & FlagBloom) != 0;
        v.scanlinesEnabled = (packed & FlagScanlines) != 0;
        v.explosionLightEnabled = (packed & FlagExplosionLight) != 0;
        v.wireframeFill = (packed & FlagWireframeFill) != 0;

        int q = (packed >> 16) & 0xFF;
        v.quality = (q == 0) ? editor::Quality::Low
                             : (q == 1) ? editor::Quality::Medium
                                        : editor::Quality::High;

        // The neon boost is stored as a flag, not a value, so restore the
        // intensity the menu would have set (see GameMenu::activate).
        if (v.neonEnabled && v.neonIntensity <= 1.0f)
            v.neonIntensity = 1.6f;
    }

#if defined(__EMSCRIPTEN__)
    // Returns the stored value, or 0 when absent/unparseable — 0 can never be a
    // valid packed value because the version field is non-zero.
    EM_JS(int, em_settings_load, (), {
        try {
            var raw = localStorage.getItem('cowengine_vfx');
            if (raw == null) return 0;
            var n = parseInt(raw, 10);
            return isNaN(n) ? 0 : n;
        } catch (e) { return 0; }
    });

    EM_JS(void, em_settings_store, (int packed), {
        try { localStorage.setItem('cowengine_vfx', String(packed)); }
        catch (e) {}
    });
#else
    const char *kSettingsPath = "cowengine_settings.txt";
#endif

    const char *qualityName(editor::Quality q)
    {
        switch (q)
        {
        case editor::Quality::Low: return "Low";
        case editor::Quality::Medium: return "Medium";
        default: return "High";
        }
    }

    // Layout, derived from framebuffer height so the menu is legible on a phone
    // and not comically large on a 4K monitor.
    struct Layout
    {
        float scale;
        float panelX, panelY, panelW, panelH;
        float rowH;
        float firstRowY;
    };

    Layout layoutFor(int fbWidth, int fbHeight, size_t rowCount)
    {
        Layout L;
        L.scale = std::clamp(static_cast<float>(fbHeight) / 720.0f, 0.7f, 2.5f);
        L.rowH = 38.0f * L.scale;
        const float titleH = 64.0f * L.scale;
        const float padding = 24.0f * L.scale;

        L.panelW = std::min(560.0f * L.scale, static_cast<float>(fbWidth) * 0.88f);
        L.panelH = titleH + L.rowH * static_cast<float>(rowCount) + padding;
        L.panelX = (static_cast<float>(fbWidth) - L.panelW) * 0.5f;
        L.panelY = (static_cast<float>(fbHeight) - L.panelH) * 0.5f;
        L.firstRowY = L.panelY + titleH;
        return L;
    }

    unsigned int compile(unsigned int type, const char *src)
    {
        unsigned int s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            std::cerr << "GameMenu shader compile error: " << log << std::endl;
        }
        return s;
    }
}

GameMenu::~GameMenu()
{
    if (rectProgram_)
        glDeleteProgram(rectProgram_);
    if (rectVao_)
        glDeleteVertexArrays(1, &rectVao_);
}

const std::vector<GameMenu::Row> &GameMenu::rows()
{
    static const std::vector<Row> r = {
        {"Sky Gradient", RowKind::Toggle, &editor::VFX::skyEnabled, "Vaporwave sunset backdrop"},
        {"Retro Sun", RowKind::Toggle, &editor::VFX::sunEnabled, "Striped sun on the horizon"},
        {"Neon Grid", RowKind::Toggle, &editor::VFX::gridEnabled, "Glowing floor grid"},
        {"Distance Fog", RowKind::Toggle, &editor::VFX::fogEnabled, "Haze on far geometry"},
        {"Neon Glow", RowKind::Toggle, &editor::VFX::neonEnabled, "Brighter wireframe lines"},
        {"Bloom", RowKind::Toggle, &editor::VFX::bloomEnabled, "Light bleeds past bright edges"},
        {"Scanlines", RowKind::Toggle, &editor::VFX::scanlinesEnabled, "CRT overlay"},
        {"Explosion Light", RowKind::Toggle, &editor::VFX::explosionLightEnabled,
         "Blasts light up the world around them"},
        {"Solid Fill", RowKind::Toggle, &editor::VFX::wireframeFill, "Opaque backing behind wireframes"},
        {"Quality", RowKind::Quality, nullptr, "Lower this if the game runs rough"},
        {"Resume", RowKind::Close, nullptr, nullptr},
        {"Quit", RowKind::Quit, nullptr, nullptr},
    };
    return r;
}

void GameMenu::setOpen(bool open)
{
    open_ = open;
    // Reset edge state so the keypress that opened the menu isn't also read as
    // the first selection, and vice versa on the way out.
    prevSelect_ = true;
    prevToggleKey_ = true;
    prevUp_ = prevDown_ = true;
    prevMouseDown_ = true;
    repeatTimer_ = 0.0f;
}

void GameMenu::loadSettings(editor::VFX &vfx)
{
    int packed = 0;
#if defined(__EMSCRIPTEN__)
    packed = em_settings_load();
#else
    std::ifstream in(kSettingsPath);
    if (in)
        in >> packed;
#endif
    if (packed != 0)
        unpackSettings(packed, vfx);
}

void GameMenu::saveSettings(const editor::VFX &vfx)
{
    const int packed = packSettings(vfx);
#if defined(__EMSCRIPTEN__)
    em_settings_store(packed);
#else
    std::ofstream out(kSettingsPath, std::ios::trunc);
    if (out)
        out << packed << "\n";
#endif
}

bool GameMenu::ensureShader()
{
    if (rectProgram_)
        return true;

#if defined(__EMSCRIPTEN__)
    const char *kVersion = "#version 300 es\nprecision mediump float;\n";
#else
    const char *kVersion = "#version 330 core\n";
#endif

    // Position comes from gl_VertexID against a rect uniform, so there is no
    // vertex buffer to manage — the whole overlay is a handful of these.
    const std::string vs = std::string(kVersion) +
        "uniform vec4 uRect;\n" // NDC: xy = corner, zw = size
        "void main() {\n"
        "    vec2 c = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
        "    gl_Position = vec4(uRect.xy + c * uRect.zw, 0.0, 1.0);\n"
        "}\n";
    const std::string fs = std::string(kVersion) +
        "uniform vec4 uColor;\n"
        "out vec4 FragColor;\n"
        "void main() { FragColor = uColor; }\n";

    unsigned int v = compile(GL_VERTEX_SHADER, vs.c_str());
    unsigned int f = compile(GL_FRAGMENT_SHADER, fs.c_str());
    rectProgram_ = glCreateProgram();
    glAttachShader(rectProgram_, v);
    glAttachShader(rectProgram_, f);
    glLinkProgram(rectProgram_);
    int ok = 0;
    glGetProgramiv(rectProgram_, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[512];
        glGetProgramInfoLog(rectProgram_, sizeof(log), nullptr, log);
        std::cerr << "GameMenu program link error: " << log << std::endl;
    }
    glDeleteShader(v);
    glDeleteShader(f);

    if (rectVao_ == 0)
        glGenVertexArrays(1, &rectVao_);
    return rectProgram_ != 0;
}

void GameMenu::drawRect(float x, float y, float w, float h, const glm::vec4 &color,
                        int fbWidth, int fbHeight)
{
    if (!ensureShader() || fbWidth <= 0 || fbHeight <= 0)
        return;

    // Pixel space (y down from the top-left) to NDC.
    const float ndcX = (x / static_cast<float>(fbWidth)) * 2.0f - 1.0f;
    const float ndcY = 1.0f - (y / static_cast<float>(fbHeight)) * 2.0f;
    const float ndcW = (w / static_cast<float>(fbWidth)) * 2.0f;
    const float ndcH = -(h / static_cast<float>(fbHeight)) * 2.0f;

    // Blend state is set per rect rather than once around the whole overlay,
    // because TextRenderer::submit() ends by disabling GL_BLEND. Set once up
    // front, the row highlights — which are drawn after the title text — came
    // out fully opaque and buried their own labels.
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(rectProgram_);
    glUniform4f(glGetUniformLocation(rectProgram_, "uRect"), ndcX, ndcY, ndcW, ndcH);
    glUniform4f(glGetUniformLocation(rectProgram_, "uColor"), color.r, color.g, color.b, color.a);
#if !defined(__EMSCRIPTEN__)
    // The scene is drawn wireframe, which leaves desktop GL in GL_LINE mode —
    // the same trap PostFX documents. Without this the panel is an outline.
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
    glBindVertexArray(rectVao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
}

void GameMenu::activate(size_t index, editor::VFX &vfx)
{
    const auto &r = rows();
    if (index >= r.size())
        return;

    switch (r[index].kind)
    {
    case RowKind::Toggle:
        if (r[index].flag)
        {
            bool &field = vfx.*(r[index].flag);
            field = !field;
            // Neon's strength is a separate float that ships at 1.0, which is
            // exactly pass-through — so flipping the flag alone would appear to
            // do nothing at all. Give it a visible value the first time it is
            // switched on, and leave any value the user already has.
            if (r[index].flag == &editor::VFX::neonEnabled && field && vfx.neonIntensity <= 1.0f)
                vfx.neonIntensity = 1.6f;
        }
        break;
    case RowKind::Quality:
        vfx.quality = (vfx.quality == editor::Quality::Low)      ? editor::Quality::Medium
                      : (vfx.quality == editor::Quality::Medium) ? editor::Quality::High
                                                                 : editor::Quality::Low;
        break;
    case RowKind::Close:
        setOpen(false);
        break;
    case RowKind::Quit:
        quitRequested_ = true;
        break;
    }

    saveSettings(vfx);
}

bool GameMenu::update(Window &window, editor::VFX &vfx, float dt)
{
    GLFWwindow *w = window.getWindow();

    // Escape is the only key read while the menu is closed, so gameplay is
    // untouched until it opens.
    const bool toggleKey = window.isKeyPressed(GLFW_KEY_ESCAPE);
    if (toggleKey && !prevToggleKey_)
    {
        const bool wasOpen = open_;
        setOpen(!wasOpen);
        if (open_)
        {
            // Release the cursor so the menu is clickable, and remember what to
            // put back — reopening into a locked cursor would otherwise leave
            // the player unable to look around after closing.
            restoreCursorDisabled_ = window.isCursorDisabled();
            window.setCursorDisabled(false);
        }
        else
        {
            window.setCursorDisabled(restoreCursorDisabled_);
        }
        prevToggleKey_ = true;
        return open_;
    }
    prevToggleKey_ = toggleKey;

    if (!open_)
        return false;

    const auto &r = rows();

    // Up/down on either the arrows or WS — WS because that is what the web
    // shell's virtual stick presses, which is what makes this work on a phone.
    const bool up = window.isKeyPressed(GLFW_KEY_UP) || window.isKeyPressed(GLFW_KEY_W);
    const bool down = window.isKeyPressed(GLFW_KEY_DOWN) || window.isKeyPressed(GLFW_KEY_S);

    // A held direction repeats slowly after a delay. The touch stick is held
    // rather than tapped, so without this it would be almost impossible to
    // move exactly one row; with it, a tap is one row and a hold scrolls.
    bool stepUp = up && !prevUp_;
    bool stepDown = down && !prevDown_;
    if (up || down)
    {
        repeatTimer_ -= dt;
        if (repeatTimer_ <= 0.0f)
        {
            if (!stepUp && !stepDown)
            {
                stepUp = up;
                stepDown = down && !up;
            }
            repeatTimer_ = (stepUp || stepDown) && (prevUp_ || prevDown_) ? 0.12f : 0.40f;
        }
    }
    else
    {
        repeatTimer_ = 0.0f;
    }
    prevUp_ = up;
    prevDown_ = down;

    if (stepUp)
        selected_ = (selected_ == 0) ? r.size() - 1 : selected_ - 1;
    else if (stepDown)
        selected_ = (selected_ + 1) % r.size();

    // Mouse: hover highlights, click activates. Hit-tested against the layout
    // the last render used, which is a frame stale only on the frame the window
    // is resized.
    if (lastFbWidth_ > 0 && lastFbHeight_ > 0)
    {
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(w, &mx, &my);
        // Cursor position is in window coordinates; the layout is in
        // framebuffer pixels, and those differ on a HiDPI display.
        const float dpr = getDevicePixelRatioFor(w);
        const float px = static_cast<float>(mx) * dpr;
        const float py = static_cast<float>(my) * dpr;

        // Hovering only takes over the selection once the mouse has actually
        // moved. Otherwise a cursor left sitting anywhere over the panel
        // reasserts its row every frame, and the keyboard cannot move at all —
        // each press is undone before it is ever drawn.
        const bool mouseMoved = std::abs(px - lastMouseX_) > 1.0f ||
                                std::abs(py - lastMouseY_) > 1.0f;
        lastMouseX_ = px;
        lastMouseY_ = py;

        const Layout L = layoutFor(lastFbWidth_, lastFbHeight_, r.size());
        if (px >= L.panelX && px <= L.panelX + L.panelW)
        {
            const int row = static_cast<int>((py - L.firstRowY) / L.rowH);
            if (row >= 0 && row < static_cast<int>(r.size()))
            {
                if (mouseMoved)
                    selected_ = static_cast<size_t>(row);
                const bool mouseDown = glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
                if (mouseDown && !prevMouseDown_)
                {
                    // A click acts on the row under the cursor, moved there or
                    // not — clicking a row you can see must always work.
                    selected_ = static_cast<size_t>(row);
                    activate(selected_, vfx);
                    prevMouseDown_ = true;
                    return true;
                }
                prevMouseDown_ = mouseDown;
            }
        }
    }
    if (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS)
        prevMouseDown_ = false;

    // Select. Space is here because the web shell's JUMP button presses it.
    const bool select = window.isKeyPressed(GLFW_KEY_SPACE) ||
                        window.isKeyPressed(GLFW_KEY_ENTER) ||
                        window.isKeyPressed(GLFW_KEY_KP_ENTER);
    if (select && !prevSelect_)
        activate(selected_, vfx);
    prevSelect_ = select;

    if (!open_)
        window.setCursorDisabled(restoreCursorDisabled_);

    return true;
}

void GameMenu::render(TextRenderer &text, const editor::VFX &vfx, int fbWidth, int fbHeight)
{
    lastFbWidth_ = fbWidth;
    lastFbHeight_ = fbHeight;
    if (!open_ || fbWidth <= 0 || fbHeight <= 0)
        return;

    const auto &r = rows();
    const Layout L = layoutFor(fbWidth, fbHeight, r.size());

    // No depth for an overlay. Saved and restored because this draws in the
    // middle of the frame the caller is still composing. (Blending is handled
    // per rect — see drawRect.)
    const GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    // Dim the whole frame, then lay the panel over it.
    drawRect(0, 0, static_cast<float>(fbWidth), static_cast<float>(fbHeight),
             glm::vec4(0.0f, 0.0f, 0.0f, 0.55f), fbWidth, fbHeight);
    drawRect(L.panelX, L.panelY, L.panelW, L.panelH,
             glm::vec4(0.05f, 0.04f, 0.09f, 0.92f), fbWidth, fbHeight);

    const float titleSize = 26.0f * L.scale;
    const float rowSize = 19.0f * L.scale;
    const float padX = 22.0f * L.scale;

    text.drawScreen("SETTINGS", L.panelX + padX, L.panelY + 18.0f * L.scale,
                    titleSize, glm::vec4(1.0f, 0.45f, 0.85f, 1.0f), fbWidth, fbHeight);

    for (size_t i = 0; i < r.size(); ++i)
    {
        const float rowY = L.firstRowY + L.rowH * static_cast<float>(i);
        const bool sel = (i == selected_);

        if (sel)
            drawRect(L.panelX + padX * 0.4f, rowY, L.panelW - padX * 0.8f, L.rowH,
                     glm::vec4(1.0f, 0.35f, 0.75f, 0.28f), fbWidth, fbHeight);

        const glm::vec4 labelCol = sel ? glm::vec4(1.0f, 0.95f, 1.0f, 1.0f)
                                       : glm::vec4(0.78f, 0.76f, 0.85f, 1.0f);
        const float textY = rowY + (L.rowH - rowSize) * 0.5f;
        text.drawScreen(r[i].label, L.panelX + padX, textY, rowSize, labelCol, fbWidth, fbHeight);

        // Right-aligned value, so the states line up in a column and the menu
        // can be read at a glance rather than word by word.
        std::string value;
        glm::vec4 valueCol(0.55f, 0.55f, 0.62f, 1.0f);
        if (r[i].kind == RowKind::Toggle && r[i].flag)
        {
            const bool on = vfx.*(r[i].flag);
            value = on ? "ON" : "OFF";
            valueCol = on ? glm::vec4(0.45f, 1.0f, 0.72f, 1.0f) : glm::vec4(0.45f, 0.45f, 0.52f, 1.0f);
        }
        else if (r[i].kind == RowKind::Quality)
        {
            value = qualityName(vfx.quality);
            valueCol = glm::vec4(1.0f, 0.82f, 0.35f, 1.0f);
        }

        if (!value.empty())
        {
            const float vw = text.measure(value) * rowSize;
            text.drawScreen(value, L.panelX + L.panelW - padX - vw, textY, rowSize,
                            valueCol, fbWidth, fbHeight);
        }
    }

    // Footer: what to press. Worth the line — the menu is driven by the same
    // keys as the game, which is not something a player would guess.
    const float hintSize = 14.0f * L.scale;
    const char *hint = "W/S or arrows to move   Space to change   Esc to close";
    const float hw = text.measure(hint) * hintSize;
    text.drawScreen(hint, (static_cast<float>(fbWidth) - hw) * 0.5f,
                    L.panelY + L.panelH + 14.0f * L.scale, hintSize,
                    glm::vec4(0.6f, 0.6f, 0.68f, 1.0f), fbWidth, fbHeight);

    if (!blendWasOn)
        glDisable(GL_BLEND);
    if (depthWasOn)
        glEnable(GL_DEPTH_TEST);

}
