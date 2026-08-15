#pragma once

#include <string>
#include <memory>
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>

namespace wowee {
namespace rendering { class VkContext; }

namespace core {

struct WindowConfig {
    std::string title = "Wowee Native";
    int width = 1920;
    int height = 1080;
    bool fullscreen = false;
    bool vsync = true;
    bool resizable = true;
};

class Window {
public:
    explicit Window(const WindowConfig& config);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool initialize();
    void shutdown();

private:
    /// Give the window its own icon rather than the toolkit's default.
    void setWindowIcon();

public:

    void swapBuffers() {} // No-op: Vulkan presents in Renderer::endFrame()

    bool shouldClose() const { return shouldCloseFlag; }
    void setShouldClose(bool value) { shouldCloseFlag = value; }

    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void setSize(int w, int h) { width = w; height = h; }
    float getAspectRatio() const { return static_cast<float>(width) / height; }
    bool isFullscreen() const { return fullscreen; }
    bool isVsyncEnabled() const { return vsync; }

    /// Frames per second the main loop paces itself to, or 0 for uncapped.
    ///
    /// Lives here beside vsync because it is the same question asked a
    /// different way - how often to present - and a client drawing a 2004 game
    /// will otherwise run as fast as the hardware allows, which on a laptop is
    /// heat and fan noise for frames nobody sees.
    void setFrameCap(int fps) { frameCapFps_ = (fps > 0) ? fps : 0; }
    int frameCap() const { return frameCapFps_; }
    void setFullscreen(bool enable);
    void setVsync(bool enable);
    void applyResolution(int w, int h);

    SDL_Window* getSDLWindow() const { return window; }

    // Vulkan context access
    rendering::VkContext* getVkContext() const { return vkContext.get(); }

private:
    WindowConfig config;
    SDL_Window* window = nullptr;
    std::unique_ptr<rendering::VkContext> vkContext;

    int width;
    int height;
    int windowedWidth = 0;
    int windowedHeight = 0;
    bool fullscreen = false;
    int frameCapFps_ = 0;   // 0 = uncapped
    bool vsync = true;
    bool shouldCloseFlag = false;
};

} // namespace core
} // namespace wowee
