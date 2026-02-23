#include "window.h"
#include <stdexcept>

Window::Window(int width, int height, std::string_view title) {

	if (!glfwInit())
        throw std::runtime_error("glfwInit failed");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no OpenGL
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    _window = glfwCreateWindow(width, height, title.data(), nullptr, nullptr);
    if (!_window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
}

Window::~Window() {
    glfwDestroyWindow(_window);
    glfwTerminate();
}
