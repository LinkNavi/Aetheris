#pragma once
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string_view>

class Window {
public:
    Window(int width, int height, std::string_view title);
    ~Window();

    bool        shouldClose() const { return glfwWindowShouldClose(_window); }
    void        poll()        const { glfwPollEvents(); }
    GLFWwindow* handle()      const { return _window; }
    void        getSize(int& w, int& h) const { glfwGetFramebufferSize(_window, &w, &h); }

private:
    GLFWwindow* _window = nullptr;
};
