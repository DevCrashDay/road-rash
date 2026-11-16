#pragma once
#include "graphics_backend.hpp"
#include <vector>
#include <array>
#include <cstdint>

namespace rr::switch_port {

enum class SplitscreenLayout {
    Single,
    TwoPlayerHorizontal,
    TwoPlayerVertical,
    ThreePlayer,
    FourPlayer
};

struct PlayerRaceStatus {
    int seat_idx = 0;
    int rider_idx = 0;
    int speed_mph = 0;
    int place = 1;
    int health = 100;
    int max_health = 100;
    int nitro_remaining = 0;
    bool finished = false;
    bool busted = false;
};

class SplitscreenManager {
public:
    static constexpr int kMaxPlayers = 4;

    SplitscreenManager();

    void set_player_count(int count);
    int player_count() const { return player_count_; }

    void set_layout(SplitscreenLayout layout);
    SplitscreenLayout layout() const { return layout_; }

    void update_viewports(int screen_w, int screen_h);
    const ViewportRect& viewport(int player_idx) const;

    void update_player_status(int player_idx, const PlayerRaceStatus& status);
    const PlayerRaceStatus& player_status(int player_idx) const;

    // Renders the viewport divider lines on screen
    void draw_separators(IGraphicsBackend& gfx);

    // Renders HUD overlays for all active viewports
    void draw_all_huds(IGraphicsBackend& gfx);

private:
    int player_count_ = 1;
    SplitscreenLayout layout_ = SplitscreenLayout::Single;
    std::array<ViewportRect, kMaxPlayers> viewports_{};
    std::array<PlayerRaceStatus, kMaxPlayers> statuses_{};
    int screen_w_ = 1280;
    int screen_h_ = 720;
};

} // namespace rr::switch_port
