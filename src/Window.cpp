#include "../include/Window.hpp"

Window::Window(int width, int height, const std::string& title) {
    glfwInit();

    window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
    glfwMakeContextCurrent(window);

    glewInit();

    glfwGetFramebufferSize(window, &this->width, &this->height);
    glViewport(0, 0, this->width, this->height);

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glfwSwapInterval(1);
}

Window::~Window() {
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window);
}

void Window::swapBuffers() const {
    glfwSwapBuffers(window);
    // glfwPollEvents();
}

int Window::getWidth() {
    glfwGetFramebufferSize(window, &this->width, &this->height);
    return width;
}

int Window::getHeight() {
    glfwGetFramebufferSize(window, &this->width, &this->height);
    return height;
}

void Window::getCursorPosition(double& xpos, double& ypos) {
    glfwGetCursorPos(window, &xpos, &ypos);
}

bool Window::isKeyPressed(int key) const {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

