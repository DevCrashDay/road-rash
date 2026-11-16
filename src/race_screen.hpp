// Plan 28 Task 4: RaceScreen interface (plan 23 Task 3's AppScreen signatures).
#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include "display.hpp"
#include "framebuffer.hpp"
#include "game_types.hpp"
#include "input.hpp"
#include "lifecycle.hpp"
#include "race_actions.hpp"
#include "race_caches.hpp"
#include "race_overlay.hpp"
#include "sdl_handles.hpp"
#include "session.hpp"

namespace rr {
constexpr int race_end_delay = 0xb4; // 0x00416010: ticks the race keeps running after it is decided

// Loop helpers shared between the screen and game_main.cpp's driver.
// SpriteCache/CourseCanvas stay in game_main.cpp (used by menus/video too);
// the screen holds pointers borrowed from the driver.
class RaceScreen {
  public:
    RaceScreen(Game&, SDL_Renderer*, const RaceOptions&, DisplayDriver&);
    void event(const SDL_Event&);
    bool iterate(uint64_t now_ns); // false when the race loop would have exited
    RaceEnd result() const { return result_; }
    const NetworkOverlay* network_overlay() const { return network_overlay_.get(); }

  private:
    Game& g_;
    SDL_Renderer* renderer_;
    const RaceOptions& opt_;
    DisplayDriver& display_;
    RaceEnd result_;
    LifecycleWatch lifecycle_;
    TouchInput touch_;
    std::array<JoystickAnalog, 4> joy_analogs_{};
    TickClock clock_;
    SpriteCache sprites_;
    std::array<CourseCanvas, 4> canvases_{};
#ifdef __SWITCH__
    std::shared_ptr<GpuRaceRenderer> gpu_;
#endif
    // Splitscreen seats 1..3: interpolated views in presentation_copy(seat)
    // row order (seat 0 uses g_.view_*). Captured with g_.view_cur.
    std::array<ViewState, 4> seat_prev_{}, seat_cur_{}, seat_drawn_{};
    // PERF-PROBE (Switch): phase averages logged every 120 frames.
    FrameProfile perf_;
    std::array<uint64_t, FrameProfile::count> perf_sum_{};
    uint64_t perf_frames_ = 0, perf_total_ns_ = 0, perf_copy_ns_ = 0, perf_canvas_ns_ = 0, perf_steps_ = 0;
    std::array<GamepadPtr, 4> pads_{};
    TexturePtr pause_tex_;
    int pause_scale_w_ = 0;
    RgbImage pause_img_;
    std::unique_ptr<FpsOverlay> fps_;
    std::unique_ptr<NetworkOverlay> network_overlay_;
    RemoteSmoothing remote_smoothing_;
    std::optional<net::Official> official_;
    std::array<std::string, 3> announcements_{};
    void step_online(uint64_t now, uint32_t controls);
    bool running_ = true, decided_ = false;
    int end_ticks_ = 0;
    uint64_t last_ = 0;
    bool restart_ = false;
    // 0x00415f70 palette fades: race start (0x00415d90) blocks input and the
    // simulation until it ends, then the loop clock starts; race end
    // (0x00415e90) runs after the decided race's 0xb4 ticks. Wall-clock
    // driven at 14 ms per upload; presentation only (Game::fade_lut).
    enum class Fade { none, in, out };
    Fade fade_ = Fade::none;
    uint64_t fade_start_ = 0;     // 0: starts on the next iterate
    bool fade_from_half_ = false; // quit-confirm leaves the half palette up
    bool hold_frame_ = false;     // fade-in before its first upload: keep the old screen
    void update_fade(uint64_t now);

    RaceActionContext ctx_now() const;
    void end_as_quit();
    void reset_clock(uint64_t now_ns);
    bool clock_reset_ = false; // reset_clock ran this iterate (bench pacing)
    void step_sim(uint64_t now);
    void render(uint64_t now);
    void draw_scene(FrameProfile*);
    RgbImage pause_frame();
};
} // namespace rr
