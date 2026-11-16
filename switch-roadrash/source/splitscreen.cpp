#include "splitscreen.hpp"
#include <algorithm>

namespace rr::switch_port {

SplitscreenManager::SplitscreenManager() {
    set_player_count(1);
    update_viewports(1280, 720);
}

void SplitscreenManager::set_player_count(int count) {
    player_count_ = std::clamp(count, 1, kMaxPlayers);

    // Auto layout based on player count
    switch (player_count_) {
        case 1:
            layout_ = SplitscreenLayout::Single;
            break;
        case 2:
            layout_ = SplitscreenLayout::TwoPlayerHorizontal; // 1280x360 each (cinematic wide)
            break;
        case 3:
            layout_ = SplitscreenLayout::ThreePlayer;
            break;
        case 4:
            layout_ = SplitscreenLayout::FourPlayer;
            break;
    }

    update_viewports(screen_w_, screen_h_);
}

void SplitscreenManager::set_layout(SplitscreenLayout layout) {
    layout_ = layout;
    update_viewports(screen_w_, screen_h_);
}

void SplitscreenManager::update_viewports(int screen_w, int screen_h) {
    screen_w_ = screen_w > 0 ? screen_w : 1280;
    screen_h_ = screen_h > 0 ? screen_h : 720;

    const int half_w = screen_w_ / 2;
    const int half_h = screen_h_ / 2;

    switch (layout_) {
        case SplitscreenLayout::Single:
            viewports_[0] = { 0, 0, screen_w_, screen_h_ };
            break;

        case SplitscreenLayout::TwoPlayerHorizontal:
            // Top half
            viewports_[0] = { 0, 0, screen_w_, half_h - 1 };
            // Bottom half
            viewports_[1] = { 0, half_h + 1, screen_w_, half_h - 1 };
            break;

        case SplitscreenLayout::TwoPlayerVertical:
            // Left half
            viewports_[0] = { 0, 0, half_w - 1, screen_h_ };
            // Right half
            viewports_[1] = { half_w + 1, 0, half_w - 1, screen_h_ };
            break;

        case SplitscreenLayout::ThreePlayer:
            // Top Left
            viewports_[0] = { 0, 0, half_w - 1, half_h - 1 };
            // Top Right
            viewports_[1] = { half_w + 1, 0, half_w - 1, half_h - 1 };
            // Bottom Center (Half width, centered)
            viewports_[2] = { screen_w_ / 4, half_h + 1, half_w, half_h - 1 };
            break;

        case SplitscreenLayout::FourPlayer:
            // Top Left
            viewports_[0] = { 0, 0, half_w - 1, half_h - 1 };
            // Top Right
            viewports_[1] = { half_w + 1, 0, half_w - 1, half_h - 1 };
            // Bottom Left
            viewports_[2] = { 0, half_h + 1, half_w - 1, half_h - 1 };
            // Bottom Right
            viewports_[3] = { half_w + 1, half_h + 1, half_w - 1, half_h - 1 };
            break;
    }
}

const ViewportRect& SplitscreenManager::viewport(int player_idx) const {
    if (player_idx < 0 || player_idx >= kMaxPlayers) {
        return viewports_[0];
    }
    return viewports_[size_t(player_idx)];
}

void SplitscreenManager::update_player_status(int player_idx, const PlayerRaceStatus& status) {
    if (player_idx >= 0 && player_idx < kMaxPlayers) {
        statuses_[size_t(player_idx)] = status;
    }
}

const PlayerRaceStatus& SplitscreenManager::player_status(int player_idx) const {
    if (player_idx < 0 || player_idx >= kMaxPlayers) {
        static PlayerRaceStatus empty{};
        return empty;
    }
    return statuses_[size_t(player_idx)];
}

void SplitscreenManager::draw_separators(IGraphicsBackend& gfx) {
    // 2-pixel bright separator border between splitscreen views
    const uint32_t divider_color = 0x222222ff;
    const uint32_t highlight_color = 0x555555ff;

    const int half_w = screen_w_ / 2;
    const int half_h = screen_h_ / 2;

    if (layout_ == SplitscreenLayout::TwoPlayerHorizontal) {
        gfx.draw_divider_line(0, half_h - 1, screen_w_, half_h - 1, divider_color);
        gfx.draw_divider_line(0, half_h,     screen_w_, half_h,     highlight_color);
    } else if (layout_ == SplitscreenLayout::TwoPlayerVertical) {
        gfx.draw_divider_line(half_w - 1, 0, half_w - 1, screen_h_, divider_color);
        gfx.draw_divider_line(half_w,     0, half_w,     screen_h_, highlight_color);
    } else if (layout_ == SplitscreenLayout::ThreePlayer || layout_ == SplitscreenLayout::FourPlayer) {
        // Horizontal line
        gfx.draw_divider_line(0, half_h - 1, screen_w_, half_h - 1, divider_color);
        gfx.draw_divider_line(0, half_h,     screen_w_, half_h,     highlight_color);
        // Top vertical line
        gfx.draw_divider_line(half_w - 1, 0, half_w - 1, half_h, divider_color);
        gfx.draw_divider_line(half_w,     0, half_w,     half_h, highlight_color);

        if (layout_ == SplitscreenLayout::FourPlayer) {
            // Bottom vertical line
            gfx.draw_divider_line(half_w - 1, half_h, half_w - 1, screen_h_, divider_color);
            gfx.draw_divider_line(half_w,     half_h, half_w,     screen_h_, highlight_color);
        }
    }
}

void SplitscreenManager::draw_all_huds(IGraphicsBackend& gfx) {
    for (int i = 0; i < player_count_; ++i) {
        const auto& vp = viewports_[size_t(i)];
        const auto& st = statuses_[size_t(i)];
        gfx.draw_hud_overlay(i, st.place, st.speed_mph, st.health, st.max_health, vp);
    }
}

} // namespace rr::switch_port
