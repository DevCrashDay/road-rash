#pragma once
#ifdef __SWITCH__
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "frame_profile.hpp"
#include "game_types.hpp"
#include "road.hpp"
#include "sdl_handles.hpp"

namespace rr {
// GLES2 GPU race renderer for Nintendo Switch.
// Renders horizon, textured/flat world quads, palette animation/fades, fog,
// and dashboard (art, side panels, meters, needles, cached text slots) directly
// into each viewport's SDL_TEXTUREACCESS_TARGET texture, eliminating CPU
// rasterization, dash_mix, palette remap, and full-frame texture uploads.
class GpuRaceRenderer {
  public:
    GpuRaceRenderer();
    ~GpuRaceRenderer();
    GpuRaceRenderer(const GpuRaceRenderer&) = delete;
    GpuRaceRenderer& operator=(const GpuRaceRenderer&) = delete;

    // Returns false if GLES2 interop is unavailable (caller falls back to CPU).
    bool draw(SDL_Renderer* renderer, const void* seat_key, const Game& game,
              const std::vector<RoadSlice>& slices, int s, int wide, SDL_ScaleMode scale_mode,
              TexturePtr& target_texture, FrameProfile* profile, bool present);

    void reset_scene_caches();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace rr
#endif
