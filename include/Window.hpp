#ifndef WINDOW_HPP
#define WINDOW_HPP

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <iostream>

class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();

    GLFWwindow* getWindow() { return window; };
    bool shouldClose() { return glfwWindowShouldClose(window); };
    void update();
private:
    GLFWwindow* window;
};
#endif // WINDOW_HPP