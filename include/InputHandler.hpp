#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "Window.hpp"
#include "Camera.hpp"
#include <GLFW/glfw3.h>

class InputHandler {
public:
    InputHandler(Window& window, Camera& camera);

    void update();

    bool isKeyPressed(int key) const;
    bool isMouseButtonPressed(int button) const;
    void getMousePosition(double& xPos, double& yPos) const;

private:
    Window& window;
    Camera& camera;
    double mouseX;
    double mouseY;
    double previousTime;
    double lastMouseX;
    double lastMouseY;
};

#endif // INPUT_HANDLER_H