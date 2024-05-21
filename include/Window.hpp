#ifndef WINDOW_H
#define WINDOW_H

#include <string>
#include <GL/glew.h>
#include <GLFW/glfw3.h>

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool shouldClose() const;
    void swapBuffers() const;
    int getWidth() const;
    int getHeight() const;

private:
    GLFWwindow* window;
    int width;
    int height;
};

#endif // WINDOW_H