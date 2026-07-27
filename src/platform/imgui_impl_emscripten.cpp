// Emscripten platform backend for Dear ImGui
#include "platform/imgui_impl_emscripten.h"
#include <imgui.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstring>
#include <cctype>

static float getDevicePixelRatio()
{
    // emscripten_get_device_pixel_ratio() returns a double
    return static_cast<float>(emscripten_get_device_pixel_ratio());
}

static EM_BOOL mouse_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;
    // Compute canvas bounding rect and convert client coordinates to canvas-local CSS pixels
    double left = EM_ASM_DOUBLE({
        var c = Module['canvas'] || document.querySelector('#canvas');
        if (!c)
            return 0.0;
        return c.getBoundingClientRect().left;
    });
    double top = EM_ASM_DOUBLE({
        var c = Module['canvas'] || document.querySelector('#canvas');
        if (!c)
            return 0.0;
        return c.getBoundingClientRect().top;
    });
    float x = (float)((double)e->clientX - left);
    float y = (float)((double)e->clientY - top);
    io.AddMousePosEvent(x, y);
    return EM_TRUE;
}

static EM_BOOL mousedown_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;
    int button = e->button;
    if (button >= 0 && button < 3)
        io.AddMouseButtonEvent(button, true);
    return EM_TRUE;
}

static EM_BOOL mouseup_callback(int eventType, const EmscriptenMouseEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;
    int button = e->button;
    if (button >= 0 && button < 3)
        io.AddMouseButtonEvent(button, false);
    return EM_TRUE;
}

static EM_BOOL wheel_callback(int eventType, const EmscriptenWheelEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;

    float dx = (float)e->deltaX;
    float dy = (float)e->deltaY;

    // deltaMode 0 = DOM_DELTA_PIXEL: raw pixel distance, can be 100-300 per tick.
    // ImGui expects values roughly in "lines" (~1-3 per notch), so scale down.
    // deltaMode 1 = DOM_DELTA_LINE: already in line units — use as-is.
    // deltaMode 2 = DOM_DELTA_PAGE: scale up so one page feels right.
    if (e->deltaMode == 0)
    {
        dx /= 100.0f;
        dy /= 100.0f;
    }
    else if (e->deltaMode == 2)
    {
        dx *= 8.0f;
        dy *= 8.0f;
    }

    // Browser deltaY positive = scroll down; ImGui AddMouseWheelEvent positive y
    // = scroll up — negate to match.
    io.AddMouseWheelEvent(-dx, -dy);
    return EM_TRUE;
}

static ImGuiKey mapKeyStringToImGuiKey(const char *key)
{
    if (!key || key[0] == '\0')
        return ImGuiKey_None;
    if (std::strcmp(key, "Tab") == 0)
        return ImGuiKey_Tab;
    if (std::strcmp(key, "ArrowLeft") == 0)
        return ImGuiKey_LeftArrow;
    if (std::strcmp(key, "ArrowRight") == 0)
        return ImGuiKey_RightArrow;
    if (std::strcmp(key, "ArrowUp") == 0)
        return ImGuiKey_UpArrow;
    if (std::strcmp(key, "ArrowDown") == 0)
        return ImGuiKey_DownArrow;
    if (std::strcmp(key, "PageUp") == 0)
        return ImGuiKey_PageUp;
    if (std::strcmp(key, "PageDown") == 0)
        return ImGuiKey_PageDown;
    if (std::strcmp(key, "Home") == 0)
        return ImGuiKey_Home;
    if (std::strcmp(key, "End") == 0)
        return ImGuiKey_End;
    if (std::strcmp(key, "Insert") == 0)
        return ImGuiKey_Insert;
    if (std::strcmp(key, "Delete") == 0)
        return ImGuiKey_Delete;
    if (std::strcmp(key, "Backspace") == 0)
        return ImGuiKey_Backspace;
    if (std::strcmp(key, "Enter") == 0 || std::strcmp(key, "Return") == 0)
        return ImGuiKey_Enter;
    if (std::strcmp(key, "Escape") == 0)
        return ImGuiKey_Escape;
    if (std::strcmp(key, " ") == 0 || std::strcmp(key, "Space") == 0)
        return ImGuiKey_Space;
    if (std::strcmp(key, "Shift") == 0 || std::strcmp(key, "ShiftLeft") == 0)
        return ImGuiKey_LeftShift;
    if (std::strcmp(key, "ShiftRight") == 0)
        return ImGuiKey_RightShift;
    if (std::strcmp(key, "Control") == 0 || std::strcmp(key, "ControlLeft") == 0)
        return ImGuiKey_LeftCtrl;
    if (std::strcmp(key, "ControlRight") == 0)
        return ImGuiKey_RightCtrl;
    if (std::strcmp(key, "Alt") == 0 || std::strcmp(key, "AltLeft") == 0)
        return ImGuiKey_LeftAlt;
    if (std::strcmp(key, "AltRight") == 0)
        return ImGuiKey_RightAlt;
    // Single character keys (letters)
    if (key[1] == '\0')
    {
        char c = key[0];
        if (std::isalpha((unsigned char)c))
        {
            int idx = std::toupper((unsigned char)c) - 'A';
            return static_cast<ImGuiKey>(ImGuiKey_A + idx);
        }
        if (std::isdigit((unsigned char)c))
        {
            int idx = c - '0';
            return static_cast<ImGuiKey>(ImGuiKey_0 + idx);
        }
    }
    return ImGuiKey_None;
}

static EM_BOOL keydown_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;
    io.AddKeyEvent(ImGuiMod_Shift, e->shiftKey);
    io.AddKeyEvent(ImGuiMod_Ctrl, e->ctrlKey);
    io.AddKeyEvent(ImGuiMod_Alt, e->altKey);
    io.AddKeyEvent(ImGuiMod_Super, e->metaKey);
    ImGuiKey imguiKey = mapKeyStringToImGuiKey(e->key);
    if (imguiKey != ImGuiKey_None)
        io.AddKeyEvent(imguiKey, true);

    // Add character input for printable keys. Named special keys (Backspace, Enter, ArrowLeft, etc.)
    // are all ASCII strings starting with an uppercase letter and longer than one character.
    // Actual printable characters are either a single ASCII char or a multi-byte UTF-8 sequence
    // whose first byte has the high bit set (>= 0x80), so we can distinguish them.
    bool isNamedSpecialKey = std::isupper((unsigned char)e->key[0]) && e->key[1] != '\0';
    if (!isNamedSpecialKey && e->key[0] != '\0' && !(e->ctrlKey || e->metaKey))
        io.AddInputCharactersUTF8(e->key);
    return EM_TRUE;
}

static EM_BOOL keyup_callback(int eventType, const EmscriptenKeyboardEvent *e, void *userData)
{
    ImGuiIO &io = ImGui::GetIO();
    if (!e)
        return EM_FALSE;
    io.AddKeyEvent(ImGuiMod_Shift, e->shiftKey);
    io.AddKeyEvent(ImGuiMod_Ctrl, e->ctrlKey);
    io.AddKeyEvent(ImGuiMod_Alt, e->altKey);
    io.AddKeyEvent(ImGuiMod_Super, e->metaKey);
    ImGuiKey imguiKey = mapKeyStringToImGuiKey(e->key);
    if (imguiKey != ImGuiKey_None)
        io.AddKeyEvent(imguiKey, false);
    return EM_TRUE;
}

// Write to the real browser clipboard. Without this the web build silently has
// no clipboard at all: ImGui's default handler only fills its own in-memory
// buffer, so "Copy" buttons and Ctrl+C appear to work but nothing ever lands on
// the system clipboard. That is worse than a visibly broken button for the
// publish edit key, which is shown exactly once and is unrecoverable if lost.
EM_JS(void, cow_clipboard_write, (const char *text), {
    var s = UTF8ToString(text);
    // execCommand covers both an insecure context (navigator.clipboard is
    // undefined off https) and browsers that reject the async API.
    function fallback() {
        try {
            var ta = document.createElement('textarea');
            ta.value = s;
            ta.setAttribute('readonly', '');
            ta.style.position = 'fixed';
            ta.style.top = '-1000px';
            ta.style.opacity = '0';
            document.body.appendChild(ta);
            ta.select();
            ta.setSelectionRange(0, ta.value.length);
            document.execCommand('copy');
            document.body.removeChild(ta);
        } catch (e) { }
    }
    try {
        // Async writeText needs transient user activation, which a click still
        // carries when ImGui processes it on the next animation frame.
        if (navigator.clipboard && navigator.clipboard.writeText)
            navigator.clipboard.writeText(s)['catch'](fallback);
        else
            fallback();
    } catch (e) {
        fallback();
    }
});

// ImGui's stock handler, kept so it still mirrors into the internal buffer that
// Platform_GetClipboardTextFn reads — otherwise overriding the setter would
// leave in-app paste returning stale text.
static void (*s_prevSetClipboardTextFn)(ImGuiContext *, const char *) = nullptr;

static void cow_set_clipboard_text(ImGuiContext *ctx, const char *text)
{
    if (s_prevSetClipboardTextFn)
        s_prevSetClipboardTextFn(ctx, text);
    if (text)
        cow_clipboard_write(text);
}

void ImGui_ImplEmscripten_Init()
{
    ImGuiIO &io = ImGui::GetIO();
    io.BackendPlatformName = "imgui_impl_emscripten";

    ImGuiPlatformIO &platformIo = ImGui::GetPlatformIO();
    s_prevSetClipboardTextFn = platformIo.Platform_SetClipboardTextFn;
    platformIo.Platform_SetClipboardTextFn = cow_set_clipboard_text;
    emscripten_set_mousemove_callback("#canvas", NULL, EM_TRUE, mouse_callback);
    emscripten_set_mousedown_callback("#canvas", NULL, EM_TRUE, mousedown_callback);
    emscripten_set_mouseup_callback("#canvas", NULL, EM_TRUE, mouseup_callback);
    emscripten_set_wheel_callback("#canvas", NULL, EM_TRUE, wheel_callback);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, keydown_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, NULL, EM_TRUE, keyup_callback);
}

void ImGui_ImplEmscripten_Shutdown()
{
    // No persistent handles to clean up in this minimal backend
}

void ImGui_ImplEmscripten_NewFrame()
{
    ImGuiIO &io = ImGui::GetIO();
    int w, h;
    emscripten_get_canvas_element_size("canvas", &w, &h);
    // canvas.width/height are already physical (backing-store) pixels
    // (see resizeCanvas() in the HTML templates, which multiplies by
    // devicePixelRatio). ImGui expects DisplaySize in logical/CSS pixels
    // and derives the framebuffer size via DisplayFramebufferScale, so
    // divide dpr back out here to avoid scaling by it twice.
    float dpr = getDevicePixelRatio();
    if (dpr <= 0.0f)
        dpr = 1.0f;
    io.DisplaySize = ImVec2((float)w / dpr, (float)h / dpr);
    io.DisplayFramebufferScale = ImVec2(dpr, dpr);

    // ImGui needs a real wall-clock DeltaTime to time double-clicks, key
    // repeats, tooltips, etc. Without a backend setting it, it stays pinned
    // to the default-constructed 1/60s forever, so on any browser running
    // above 60fps (120/144Hz displays are routine now) ImGui's internal
    // clock outruns real time and the ~0.3s double-click window closes
    // before a real double-click lands -- e.g. drag/slider fields never
    // switch into text-entry mode.
    static double s_lastTime = 0.0;
    double now = emscripten_get_now() / 1000.0;
    io.DeltaTime = s_lastTime > 0.0 ? (float)(now - s_lastTime) : (float)(1.0 / 60.0);
    if (io.DeltaTime <= 0.0f)
        io.DeltaTime = 1.0f / 60.0f;
    s_lastTime = now;
}

#endif