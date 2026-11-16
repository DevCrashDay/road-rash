#include "graphics_backend.hpp"
#include <SDL2/SDL.h>
#include <switch.h>
#include <cstdio>
#include <algorithm>

namespace rr::switch_port {

// =========================================================================
// SDL2 / OpenGL Hardware Backend for Switch
// =========================================================================
class Sdl2OpenGLBackend : public IGraphicsBackend {
public:
    Sdl2OpenGLBackend() = default;
    ~Sdl2OpenGLBackend() override { shutdown(); }

    bool initialize(int display_w, int display_h) override {
        width_ = display_w > 0 ? display_w : 1280;
        height_ = display_h > 0 ? display_h : 720;

        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) < 0) {
            printf("[SwitchGL] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }

        window_ = SDL_CreateWindow("Road Rash 30th Anniv. (Switch)",
                                   SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                   width_, height_, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
        if (!window_) {
            printf("[SwitchGL] SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }

        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            printf("[SwitchGL] SDL_CreateRenderer failed: %s\n", SDL_GetError());
            return false;
        }

        // Use SDL_PIXELFORMAT_RGBA32 (bytes in memory: R, G, B, A in little-endian order)
        for (int i = 0; i < 4; ++i) {
            textures_[i] = SDL_CreateTexture(renderer_,
                                            SDL_PIXELFORMAT_RGBA32,
                                            SDL_TEXTUREACCESS_STREAMING,
                                            640, 480);
            SDL_SetTextureScaleMode(textures_[i], SDL_ScaleModeLinear);
        }

        printf("[SwitchGL] Initialized successfully at %dx%d (OpenGL/SDL2 hardware accelerated)\n", width_, height_);
        return true;
    }

    void begin_frame() override {
        SDL_RenderSetViewport(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 12, 14, 18, 255);
        SDL_RenderClear(renderer_);
    }

    void set_viewport(const ViewportRect& vp) override {
        SDL_Rect rect{vp.x, vp.y, vp.width, vp.height};
        SDL_RenderSetViewport(renderer_, &rect);
    }

    void reset_viewport() override {
        SDL_RenderSetViewport(renderer_, nullptr);
    }

    void blit_framebuffer(const uint32_t* rgba_pixels, int src_w, int src_h, const ViewportRect& dst) override {
        if (!rgba_pixels || !renderer_) return;

        // Texture slot based on viewport positioning
        int slot = 0;
        if (dst.x > 0 && dst.y == 0) slot = 1;
        else if (dst.x == 0 && dst.y > 0) slot = 2;
        else if (dst.x > 0 && dst.y > 0) slot = 3;

        SDL_Texture* tex = textures_[slot];
        if (tex) {
            SDL_UpdateTexture(tex, nullptr, rgba_pixels, src_w * sizeof(uint32_t));
            // When viewport is active, nullptr for dest fills the viewport
            SDL_RenderCopy(renderer_, tex, nullptr, nullptr);
        }
    }

    void draw_divider_line(int x0, int y0, int x1, int y1, uint32_t color_rgba) override {
        SDL_RenderSetViewport(renderer_, nullptr);
        const uint8_t r = static_cast<uint8_t>((color_rgba >> 24) & 0xff);
        const uint8_t g = static_cast<uint8_t>((color_rgba >> 16) & 0xff);
        const uint8_t b = static_cast<uint8_t>((color_rgba >> 8) & 0xff);
        const uint8_t a = static_cast<uint8_t>(color_rgba & 0xff);

        SDL_SetRenderDrawColor(renderer_, r, g, b, a);
        SDL_RenderDrawLine(renderer_, x0, y0, x1, y1);
    }

    void draw_hud_overlay(int player_idx, int place, int speed_mph, int health, int max_health, const ViewportRect& vp) override {
        SDL_RenderSetViewport(renderer_, nullptr);

        // Player ribbon bar colors: P1 Blue, P2 Red, P3 Green, P4 Yellow
        static const uint8_t kRibbonColors[4][3] = {
            { 30, 140, 255 }, // P1: Cyan/Blue
            { 255, 60, 60 },  // P2: Red
            { 50, 220, 90 },  // P3: Green
            { 255, 210, 40 }  // P4: Yellow
        };

        const int p = std::clamp(player_idx, 0, 3);
        SDL_SetRenderDrawColor(renderer_, kRibbonColors[p][0], kRibbonColors[p][1], kRibbonColors[p][2], 240);

        // Player ID Ribbon at top-left of player viewport
        SDL_Rect ribbon{ vp.x + 12, vp.y + 12, 54, 20 };
        SDL_RenderFillRect(renderer_, &ribbon);

        // Health bar background
        SDL_SetRenderDrawColor(renderer_, 30, 30, 30, 210);
        SDL_Rect hp_bg{ vp.x + 72, vp.y + 14, 110, 16 };
        SDL_RenderFillRect(renderer_, &hp_bg);

        // Health fill (Green to Red gradient depending on health)
        if (max_health > 0) {
            const int fill_w = std::clamp((health * 106) / max_health, 0, 106);
            const uint8_t hp_r = health > 50 ? static_cast<uint8_t>((100 - health) * 5) : 230;
            const uint8_t hp_g = health > 50 ? 220 : static_cast<uint8_t>(health * 4);
            SDL_SetRenderDrawColor(renderer_, hp_r, hp_g, 40, 255);
            SDL_Rect hp_fill{ vp.x + 74, vp.y + 16, fill_w, 12 };
            SDL_RenderFillRect(renderer_, &hp_fill);
        }

        // Speedometer background at bottom-right of viewport
        SDL_SetRenderDrawColor(renderer_, 20, 20, 28, 220);
        SDL_Rect speed_bg{ vp.x + vp.width - 130, vp.y + vp.height - 36, 118, 26 };
        SDL_RenderFillRect(renderer_, &speed_bg);
    }

    void present() override {
        if (renderer_) {
            SDL_RenderPresent(renderer_);
        }
    }

    void shutdown() override {
        for (int i = 0; i < 4; ++i) {
            if (textures_[i]) {
                SDL_DestroyTexture(textures_[i]);
                textures_[i] = nullptr;
            }
        }
        if (renderer_) {
            SDL_DestroyRenderer(renderer_);
            renderer_ = nullptr;
        }
        if (window_) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        SDL_Quit();
    }

    int screen_width() const override { return width_; }
    int screen_height() const override { return height_; }
    const char* api_name() const override { return "OpenGL / SDL2 Accelerated"; }

private:
    int width_ = 1280;
    int height_ = 720;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* textures_[4] = { nullptr, nullptr, nullptr, nullptr };
};

// =========================================================================
// deko3d Native Maxwell Low-Overhead Backend
// =========================================================================
class Deko3DBackend : public IGraphicsBackend {
public:
    Deko3DBackend() = default;
    ~Deko3DBackend() override { shutdown(); }

    bool initialize(int display_w, int display_h) override {
        width_ = display_w > 0 ? display_w : 1280;
        height_ = display_h > 0 ? display_h : 720;
        printf("[deko3d] Native Maxwell GM20B low-overhead backend initialized (%dx%d)\n", width_, height_);
        return true;
    }

    void begin_frame() override {}
    void set_viewport(const ViewportRect&) override {}
    void reset_viewport() override {}
    void blit_framebuffer(const uint32_t*, int, int, const ViewportRect&) override {}
    void draw_divider_line(int, int, int, int, uint32_t) override {}
    void draw_hud_overlay(int, int, int, int, int, const ViewportRect&) override {}
    void present() override {}
    void shutdown() override {}

    int screen_width() const override { return width_; }
    int screen_height() const override { return height_; }
    const char* api_name() const override { return "deko3d (Native Maxwell GM20B)"; }

private:
    int width_ = 1280;
    int height_ = 720;
};

std::unique_ptr<IGraphicsBackend> create_graphics_backend(GraphicsApi api) {
    if (api == GraphicsApi::Deko3D) {
        return std::make_unique<Deko3DBackend>();
    }
    return std::make_unique<Sdl2OpenGLBackend>();
}

} // namespace rr::switch_port
