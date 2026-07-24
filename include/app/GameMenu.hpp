#ifndef APP_GAME_MENU_HPP
#define APP_GAME_MENU_HPP

#include "render/VfxSettings.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

class TextRenderer;
class Window;

// In-game settings overlay for standalone builds.
//
// The editor configures VFX through its ImGui panel, but a shipped game links
// no ImGui at all, so this draws itself: a dimmed panel and a list of rows, via
// TextRenderer plus one small solid-colour shader of its own. That also means a
// single implementation covers desktop, web and touch instead of one menu per
// platform.
//
// Input deliberately reuses what the game already has rather than adding a
// path: up/down on the arrow keys *or* W/S, select with Space/Enter, close with
// Escape. The web shell's virtual stick presses W/S and its JUMP button presses
// Space, so touch devices drive the menu with the controls already on screen —
// no second input bridge, and nothing to keep in sync. Mouse clicks work too
// wherever a cursor is available.
class GameMenu
{
public:
    GameMenu() = default;
    ~GameMenu();
    GameMenu(const GameMenu &) = delete;
    GameMenu &operator=(const GameMenu &) = delete;

    bool isOpen() const { return open_; }
    void toggle() { setOpen(!open_); }
    void setOpen(bool open);

    // True once the player has chosen Quit; the caller closes the window.
    bool quitRequested() const { return quitRequested_; }

    // Sample input and advance the menu. Returns true if the menu consumed
    // this frame's input, in which case the caller must not also feed it to
    // gameplay — otherwise navigating the menu walks the player around.
    // `vfx` is edited in place, and persisted whenever a row changes it.
    bool update(Window &window, editor::VFX &vfx, float dt);

    // Draw the overlay into the currently bound framebuffer. Call after the
    // scene composite, with the framebuffer's pixel size.
    void render(TextRenderer &text, const editor::VFX &vfx, int fbWidth, int fbHeight);

    // Load previously saved settings into `vfx`. Missing or unreadable settings
    // leave it untouched, so a first run gets the defaults.
    static void loadSettings(editor::VFX &vfx);
    static void saveSettings(const editor::VFX &vfx);

private:
    // What a row does when selected. Toggles flip a bool; Quality cycles the
    // preset; Close and Quit act on the menu itself.
    enum class RowKind
    {
        Toggle,
        Quality,
        Close,
        Quit,
    };

    struct Row
    {
        const char *label;
        RowKind kind;
        // Which VFX field a Toggle row owns. Member pointers rather than an
        // index or a switch, so a row and its setting cannot drift apart.
        bool editor::VFX::*flag = nullptr;
        const char *help = nullptr;
    };

    static const std::vector<Row> &rows();

    void activate(size_t index, editor::VFX &vfx);
    bool ensureShader();

    // Solid colour quad, for the dim and the panel. TextRenderer draws glyphs
    // and nothing else, and the menu needs to be readable over whatever the
    // scene happens to be showing.
    void drawRect(float x, float y, float w, float h, const glm::vec4 &color,
                  int fbWidth, int fbHeight);

    bool open_ = false;
    bool quitRequested_ = false;
    size_t selected_ = 0;

    // Edge state, so a held key steps one row rather than sprinting down the
    // list. `repeatTimer_` gives a held direction a slow auto-repeat, which is
    // what makes the touch stick usable — it is held, not tapped.
    bool prevUp_ = false;
    bool prevDown_ = false;
    bool prevSelect_ = false;
    bool prevToggleKey_ = false;
    bool prevMouseDown_ = false;
    float repeatTimer_ = 0.0f;

    // Cursor state to restore when the menu closes, so opening the menu
    // mid-game doesn't leave the player looking in a new direction.
    bool restoreCursorDisabled_ = true;

    // Framebuffer size the last render laid the menu out for. update() needs it
    // to hit-test the mouse, and runs before render() — so it works from the
    // previous frame's size, which is stale only on a resize frame.
    int lastFbWidth_ = 0;
    int lastFbHeight_ = 0;

    // Last cursor position, so hovering only claims the selection when the
    // mouse has genuinely moved rather than every frame it happens to rest
    // over a row.
    float lastMouseX_ = -1.0f;
    float lastMouseY_ = -1.0f;

    unsigned int rectProgram_ = 0;
    unsigned int rectVao_ = 0;
};

#endif // APP_GAME_MENU_HPP
