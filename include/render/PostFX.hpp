#ifndef POSTFX_HPP
#define POSTFX_HPP

#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#else
#include <glad/glad.h>
#endif

#include <glm/glm.hpp>

#include "render/VfxSettings.hpp"

class Shader;

// Vaporwave post-processing pipeline:
//   1. Draws a sky-gradient + sun + perspective-grid background into the scene FBO.
//   2. Caller draws the wireframe scene on top.
//   3. Bloom (extract → ping-pong gaussian → composite) is applied while
//      copying the final image into the target FBO.
//
// One PostFX instance is shared across the editor's game view and the
// standalone game build. It owns its FBOs and shaders; on resize it rebuilds
// the attachments. Width/height of 0 means "use the previously sized FBOs".
class PostFX
{
public:
    PostFX();
    ~PostFX();

    // Lazily compile shaders and create FBOs sized to (width, height). Safe
    // to call every frame — work is only done when the size, or the quality
    // level the shaders were compiled for, has changed. Returns true on success.
    bool ensure(int width, int height, editor::Quality quality = editor::Quality::High);

    // Bind the internal scene FBO and clear color/depth. Caller then draws
    // sky (drawBackground), scene geometry, etc., into this FBO.
    void beginSceneCapture();

    // One frame's worth of explosion lights, gathered by the caller from the
    // live BlastVfx entities (see ecs::collectBlastLights). PostFX only uploads
    // them; the arrays stay owned by the caller and need only outlive the call.
    struct BlastLights
    {
        const glm::vec4 *posRadius = nullptr;      // xyz = position, w = radius
        const glm::vec4 *colorIntensity = nullptr; // rgb = colour, a = brightness
        int count = 0;
    };

    // Render the vaporwave sky+sun+grid full-screen pass into the currently
    // bound framebuffer. Expects the scene FBO bound and depth-write off; the
    // shader writes vec4(color, 1.0) at far plane. `lights` illuminates the grid
    // floor and is ignored when the grid is off; pass a default-constructed
    // BlastLights for none.
    void drawBackground(const glm::mat4 &view, const glm::mat4 &projection,
                        const glm::vec3 &camPos,
                        const editor::VFX &vfx,
                        const BlastLights &lights);

    // After the scene is in the internal FBO, run bloom and composite into
    // the target framebuffer at the given viewport. Pass targetFbo = 0 for
    // the default framebuffer.
    void compositeTo(unsigned int targetFbo, int targetX, int targetY,
                     int targetW, int targetH,
                     const editor::VFX &vfx,
                     float timeSeconds);

    unsigned int sceneColor() const { return mSceneColor; }
    int width() const { return mWidth; }
    int height() const { return mHeight; }

private:
    void destroyResources();
    bool createShaders();
    unsigned int compileProgram(const char *vertSrc, const char *fragSrc);
    void drawFullscreenTriangle();

    int mWidth = 0;
    int mHeight = 0;

    // Scene capture (color + depth).
    unsigned int mSceneFbo = 0;
    unsigned int mSceneColor = 0;
    unsigned int mSceneDepth = 0;

    // Bloom chain (half-res for performance).
    int mBloomW = 0;
    int mBloomH = 0;
    unsigned int mBloomFboA = 0;
    unsigned int mBloomColorA = 0;
    unsigned int mBloomFboB = 0;
    unsigned int mBloomColorB = 0;

    // Dummy VAO required by core GL — drives the gl_VertexID fullscreen triangle.
    unsigned int mQuadVao = 0;

    unsigned int mSkyProgram = 0;
    unsigned int mExtractProgram = 0;
    unsigned int mBlurProgram = 0;
    unsigned int mCompositeProgram = 0;

    bool mShadersReady = false;
    // The level the current programs and bloom buffers were built for. Changing
    // quality means recompiling every post-process shader and resizing the
    // bloom chain, so it is tracked rather than read per frame.
    editor::Quality mQuality = editor::Quality::High;
};

#endif // POSTFX_HPP
