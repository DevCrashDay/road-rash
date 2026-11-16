#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include <string>

namespace rr::switch_port {

enum class GraphicsApi {
    SDL2_OpenGL,
    OpenGL_Mesa,
    Deko3D
};

struct ViewportRect {
    int x = 0;
    int y = 0;
    int width = 1280;
    int height = 720;
};

class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;

    virtual bool initialize(int display_w, int display_h) = 0;
    virtual void begin_frame() = 0;
    virtual void set_viewport(const ViewportRect& vp) = 0;
    virtual void reset_viewport() = 0;
    virtual void blit_framebuffer(const uint32_t* rgba_pixels, int src_w, int src_h, const ViewportRect& dst) = 0;
    virtual void draw_divider_line(int x0, int y0, int x1, int y1, uint32_t color_rgba) = 0;
    virtual void draw_hud_overlay(int player_idx, int place, int speed_mph, int health, int max_health, const ViewportRect& vp) = 0;
    virtual void present() = 0;
    virtual void shutdown() = 0;

    virtual int screen_width() const = 0;
    virtual int screen_height() const = 0;
    virtual const char* api_name() const = 0;
};

std::unique_ptr<IGraphicsBackend> create_graphics_backend(GraphicsApi api);

} // namespace rr::switch_port
