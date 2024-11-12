#ifndef INPUT_HANDLER_H
#define INPUT_HANDLER_H

#include "Window.hpp"
#include "Camera.hpp"
#include <GLFW/glfw3.h>

class InputHandler {
public:
    InputHandler(Window& window, Camera& camera);

    void update();
    
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