#include "../include/Window.hpp"

Window::Window(int width, int height, const std::string& title) {
    glfwInit();

    window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
    glfwMakeContextCurrent(window);

    glewInit();

    glfwGetFramebufferSize(window, &this->width, &this->height); // Store the initial framebuffer size
    glViewport(0, 0, this->width, this->height);
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
    glfwPollEvents();
}

int Window::getWidth() const {
    return this->width;
}

int Window::getHeight() const {
    return this->height;
}
