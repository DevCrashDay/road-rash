// Plan 28 Task 4: RaceScreen implementation (moved from run_race, split in two).
#include "race_screen.hpp"
#include <cstdio>
#include "offline_capture.hpp"
#include "race_caches.hpp"
#include "race_helpers.hpp"
#include "course_scene.hpp"
#include "display.hpp"
#include "road_draw.hpp"
#include "upscale_cache.hpp"
#include "view_state.hpp"
#include "pe_strings.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

std::string pref_directory(); // game_main.cpp; plan 20 Task 3 cache root

namespace rr {
// DisplayDriver methods (moved unchanged from game_main.cpp).
void DisplayDriver::apply(int logical_h) {
    config = display_config(*settings, logical_h, kMobileDisplay);
    apply_display(window, renderer, config);
    int dw = 0, dh = 0;
    SDL_GetWindowSize(window, &dw, &dh);
    render_scale = effective_render_scale(*settings, dw, dh, kMobileDisplay);
    config.supersampling = 640 * render_scale > dw || 480 * render_scale > dh;
    int cores = SDL_GetNumLogicalCPUCores();
#ifdef RR_WEB_BROWSER
    render_threads = 1; // Baseline browser build has no pthread workers.
#elif defined(__SWITCH__)
    (void)cores;
    render_threads = 3; // SDL2 reports 1 CPU; applications own cores 0..2
#else
    render_threads = std::max(1, std::min(8, cores - 1));
#endif
}
void DisplayDriver::toggle_fullscreen() {
    settings->display_mode =
        settings->display_mode == DisplayMode::fullscreen ? DisplayMode::windowed : DisplayMode::fullscreen;
    save_settings(settings_path, *settings);
    apply(config.logical_h);
}

RaceScreen::RaceScreen(Game& g, SDL_Renderer* r, const RaceOptions& opt, DisplayDriver& d)
    : g_(g), renderer_(r), opt_(opt), display_(d), lifecycle_(g.mixer) {
    if (opt_.online) {
        if (!opt_.assets) throw std::runtime_error("Online race needs local game assets");
        if (!opt_.online->start() || !opt_.online->setup())
            throw std::runtime_error("Online race has no Start");
        // Immutable course allocations retain their addresses when ownership
        // moves to Game. Client keeps the authoritative mutable instance.
        g_.instance = {};
        clear_rgba_mip_cache();
        g_.assets = std::move(opt_.online->setup()->assets);
        g_.upscaled_attached = false;
        const int seat = opt_.online->start()->your_seat == 0xff ? 0 : opt_.online->start()->your_seat;
        g_.present_online(*opt_.online->race(), seat);
        g_.config = net::race_config(*opt_.online->start());
        if (g_.exe_.empty()) g_.exe_ = opt_.assets->read("ROADRASH.EXE");
        const auto strings = frontend_strings(read_string_resources(
            opt_.assets->read(language_dll(opt_.settings ? opt_.settings->language : Language::english))));
        g_.player_name = opt_.online->start()->humans[size_t(seat)].alias;
        g_.course_name = g_.config.course;
        g_.bike = opt_.online->start()->humans[size_t(seat)].bike;
        g_.metric = opt_.settings ? opt_.settings->metric : g_.metric;
        g_.assets.hud = std::make_unique<Hud>(*opt_.assets, g_.exe_, g_.bike, g_.metric);
        g_.rider_names = race_rider_names(g_.exe_, g_.config, *opt_.online->race()->race, strings,
                                          opt_.online->start()->humans[0].alias);
        std::swap(g_.rider_names[0], g_.rider_names[size_t(seat)]);
        if (strings.size() > 429) {
            g_.quit_prompt = strings[316];
            announcements_ = {strings[427], strings[428], strings[429]};
        } else
            throw std::runtime_error("Online race strings missing");
        for (size_t slot = 0; slot < opt_.online->start()->humans.size(); ++slot) {
            const size_t row = slot == size_t(seat) ? 0 : slot == 0 ? size_t(seat) : slot;
            g_.rider_names[row] = opt_.online->start()->humans[slot].alias;
        }
        g_.quitting = false;
        g_.pause_message = false;
        g_.career = true; // no quick-race restart/results path from prediction
        g_.countdown = opt_.online->start()->countdown;
        if (g_.quit_prompt.empty()) g_.quit_prompt = "Quit this race? Press Escape again to quit.";
        network_overlay_ = std::make_unique<NetworkOverlay>(opt_.assets);
    }
#ifdef __SWITCH__
    gpu_ = std::make_shared<GpuRaceRenderer>();
#endif
    for (auto& c : canvases_) {
        c.scale = display_.config.texture_scale;
#ifdef __SWITCH__
        c.gpu = gpu_;
#endif
    }
    if (opt_.start_paused) lifecycle_.state.pause();
    touch_.set_left_handed(opt_.left_handed);
    touch_.set_phone_controls(kMobileDisplay);
    int num_pads = 0;
    SDL_JoystickID* jids = SDL_GetGamepads(&num_pads);
    for (int i = 0; i < 4; ++i) {
        if (jids && i < num_pads) pads_[i].reset(SDL_OpenGamepad(jids[i]));
    }
    if (jids) SDL_free(jids);
    if (opt_.menu) opt_.menu->set_pad_labels(pad_labels(pads_[0].get()));
    if (opt_.show_fps && opt_.assets) fps_ = std::make_unique<FpsOverlay>(*opt_.assets);
    // 0x004163d0: a fresh race (timer still at its start value) fades in.
    if (!opt_.capture && g_.countdown == countdown_ticks) fade_ = Fade::in;
    display_.frame = 0;
    display_.apply(480);
}

// Redraw the current presentation without advancing simulation. Phone pause
// captures exclude the controls so the menu does not bake in ghost buttons.
RgbImage RaceScreen::pause_frame() {
    if (kMobileDisplay) draw_scene(nullptr);
    return capture_race_frame(renderer_);
}

void RaceScreen::draw_scene(FrameProfile* prof) {
    const int humans = opt_.online ? 1 : std::clamp(g_.race().humans(), 1, 4);
    int dw = 0, dh = 0;
    if (display_.window) SDL_GetWindowSize(display_.window, &dw, &dh);
    if (dw <= 0) dw = 1280;
    if (dh <= 0) dh = 720;

    const int race_scale = (opt_.menu || opt_.render_scale == 0) && opt_.settings
                               ? effective_render_scale(*opt_.settings, dw, dh, kMobileDisplay)
                               : opt_.render_scale;
    if (opt_.settings) {
        g_.widescreen = opt_.settings->widescreen;
        g_.display_w = dw;
        g_.display_h = dh;
        g_.enhancements = opt_.settings->enhancements;
        g_.texture_detail = opt_.settings->texture_detail;
    }

    if (humans <= 1) {
        SDL_SetRenderDrawColor(renderer_, 15, 18, 22, 255);
        SDL_RenderClear(renderer_);
        const auto slices = project_road(g_.race().route(), g_.blend_camera_position(), g_.blend_camera(),
                                         g_.depth, g_.logical_width());
        if (g_.scene())
            canvases_[0].draw(renderer_, g_, slices, race_scale, display_.render_threads, true);
        else
            draw_road(renderer_, slices);
        ProfileScope sprites_scope(prof, FrameProfile::sprites_hud);
        draw_opponents(renderer_, g_, sprites_, slices);
        draw_bike(renderer_, g_, sprites_, slices);
        draw_hud(renderer_, g_);
        if (!g_.banner_message.empty() && (!g_.scene() || !g_.assets.hud))
            draw_quit_prompt(renderer_, g_.banner_message); // asset-less fallback
        return;
    }

    // MULTIPLAYER SPLITSCREEN: 2, 3, or 4 Players
    SDL_SetRenderLogicalPresentation(renderer_, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(renderer_, 10, 12, 16, 255);
    SDL_RenderClear(renderer_);

    struct Viewport { float x, y, w, h; };
    std::array<Viewport, 4> vps{};
    const bool vertical_split = opt_.settings && opt_.settings->split_layout == SplitLayout::vertical;
    const float half_w = float(dw) / 2.0f;
    const float half_h = float(dh) / 2.0f;
    if (humans == 2) {
        if (vertical_split) {
            // Vertical split: Player 1 left, Player 2 right
            vps[0] = { 0.0f, 0.0f, half_w - 1.0f, float(dh) };
            vps[1] = { half_w + 1.0f, 0.0f, half_w - 1.0f, float(dh) };
        } else {
            // Horizontal split: Player 1 top, Player 2 bottom
            vps[0] = { 0.0f, 0.0f, float(dw), half_h - 1.0f };
            vps[1] = { 0.0f, half_h + 1.0f, float(dw), half_h - 1.0f };
        }
    } else if (humans == 3) {
        if (vertical_split) {
            vps[0] = { 0.0f, 0.0f, half_w - 1.0f, float(dh) };
            vps[1] = { half_w + 1.0f, 0.0f, half_w - 1.0f, half_h - 1.0f };
            vps[2] = { half_w + 1.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
        } else {
            vps[0] = { 0.0f, 0.0f, float(dw), half_h - 1.0f };
            vps[1] = { 0.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
            vps[2] = { half_w + 1.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
        }
    } else {
        if (vertical_split) {
            vps[0] = { 0.0f, 0.0f, half_w - 1.0f, half_h - 1.0f };
            vps[1] = { 0.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
            vps[2] = { half_w + 1.0f, 0.0f, half_w - 1.0f, half_h - 1.0f };
            vps[3] = { half_w + 1.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
        } else {
            vps[0] = { 0.0f, 0.0f, half_w - 1.0f, half_h - 1.0f };
            vps[1] = { half_w + 1.0f, 0.0f, half_w - 1.0f, half_h - 1.0f };
            vps[2] = { 0.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
            vps[3] = { half_w + 1.0f, half_h + 1.0f, half_w - 1.0f, half_h - 1.0f };
        }
    }

    static const SDL_Color player_colors[4] = {
        { 40, 120, 240, 255 }, // P1: Blue
        { 240, 50, 50, 255 },   // P2: Red
        { 50, 200, 70, 255 },   // P3: Green
        { 240, 190, 40, 255 }   // P4: Yellow
    };

    const Widescreen saved_wide = g_.widescreen;
    const int saved_dw = g_.display_w, saved_dh = g_.display_h;
    for (int seat = 0; seat < humans; ++seat) {
        g_.display_w = std::max(1, int(vps[seat].w));
        g_.display_h = std::max(1, int(vps[seat].h));
        g_.widescreen = vps[seat].w <= vps[seat].h ? Widescreen::off : saved_wide;
        // Seats 1..3 draw from presentation_copy(seat): the seat's rider is
        // swapped into row zero. Everything the draw reads by rider index
        // (interpolated poses, animation states) and the interpolated camera
        // must follow that swap, or the seat's bike is drawn with rider 0's
        // pose and camera (drifting off centre, popping as the sample
        // membership and the drawn pose disagree).
        std::unique_ptr<Race> saved_race;
        const int rider = seat > 0 ? g_.race().human_rider(seat) : 0;
        const bool swapped = rider > 0 && size_t(rider) < g_.actor_states.size();
        const uint64_t copy_t0 = SDL_GetTicksNS(); // PERF-PROBE
        if (seat > 0) {
            Race view = g_.race().presentation_copy(seat);
            saved_race = std::move(g_.instance.race);
            g_.instance.race = std::make_unique<Race>(std::move(view));
            std::swap(g_.view_drawn, seat_drawn_[size_t(seat)]);
            if (swapped) std::swap(g_.actor_states[0], g_.actor_states[size_t(rider)]);
        }

        const auto slices = project_road(g_.race().route(), g_.blend_camera_position(), g_.blend_camera(),
                                         g_.depth, g_.logical_width());

        const uint64_t canvas_t0 = SDL_GetTicksNS(); // PERF-PROBE
        perf_copy_ns_ += canvas_t0 - copy_t0;
        // Render scene to canvas texture without auto-presenting
        canvases_[seat].draw(renderer_, g_, slices, 1, display_.render_threads, false);
        perf_canvas_ns_ += SDL_GetTicksNS() - canvas_t0;

        if (saved_race) {
            if (swapped) std::swap(g_.actor_states[0], g_.actor_states[size_t(rider)]);
            std::swap(g_.view_drawn, seat_drawn_[size_t(seat)]);
            g_.instance.race = std::move(saved_race);
        }

        if (canvases_[seat].texture) {
            SDL_FRect dst{ vps[seat].x, vps[seat].y, vps[seat].w, vps[seat].h };
            SDL_RenderTexture(renderer_, canvases_[seat].texture.get(), nullptr, &dst);

            // Draw player badge: e.g. "P1" in a rounded/filled badge box
            SDL_FRect badge{ vps[seat].x + 12.0f, vps[seat].y + 12.0f, 44.0f, 24.0f };
            const auto& col = player_colors[seat];
            SDL_SetRenderDrawColor(renderer_, col.r, col.g, col.b, 220);
            SDL_RenderFillRect(renderer_, &badge);

            char tag[16];
            SDL_snprintf(tag, sizeof(tag), "P%d", seat + 1);
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer_, vps[seat].x + 20.0f, vps[seat].y + 16.0f, tag);
        }
    }
    g_.widescreen = saved_wide;
    g_.display_w = saved_dw;
    g_.display_h = saved_dh;

    // Dividers between split screens
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    if (humans == 2) {
        if (vertical_split) {
            SDL_FRect v_line{ float(dw / 2 - 1), 0.0f, 2.0f, float(dh) };
            SDL_RenderFillRect(renderer_, &v_line);
        } else {
            SDL_FRect h_line{ 0.0f, float(dh / 2 - 1), float(dw), 2.0f };
            SDL_RenderFillRect(renderer_, &h_line);
        }
    } else if (humans == 3) {
        if (vertical_split) {
            SDL_FRect v_line{ float(dw / 2 - 1), 0.0f, 2.0f, float(dh) };
            SDL_FRect h_line{ half_w, float(dh / 2 - 1), half_w, 2.0f };
            SDL_RenderFillRect(renderer_, &v_line);
            SDL_RenderFillRect(renderer_, &h_line);
        } else {
            SDL_FRect h_line{ 0.0f, float(dh / 2 - 1), float(dw), 2.0f };
            SDL_FRect v_line{ float(dw / 2 - 1), half_h, 2.0f, half_h };
            SDL_RenderFillRect(renderer_, &h_line);
            SDL_RenderFillRect(renderer_, &v_line);
        }
    } else {
        SDL_FRect h_line{ 0.0f, float(dh / 2 - 1), float(dw), 2.0f };
        SDL_FRect v_line{ float(dw / 2 - 1), 0.0f, 2.0f, float(dh) };
        SDL_RenderFillRect(renderer_, &h_line);
        SDL_RenderFillRect(renderer_, &v_line);
    }
}

RaceActionContext RaceScreen::ctx_now() const {
    RaceActionContext c;
    c.demo = opt_.demo;
    c.career = g_.career;
    c.finished = g_.race().finished();
    c.paused = !opt_.online && lifecycle_.state.paused();
    c.quitting = g_.quitting;
    c.pause_message = g_.pause_message;
    c.has_menu = opt_.menu != nullptr;
    c.menu_open = opt_.menu && opt_.menu->pause_open();
    c.has_pad = pads_[0] != nullptr;
    c.pad = pads_[0] ? SDL_GetGamepadID(pads_[0].get()) : 0;
    return c;
}

void RaceScreen::end_as_quit() {
    if (!decided_) {
        result_.outcome = RaceOutcome::wrecked;
        result_.place = std::max(0, g_.race().place(0) - 1);
    }
}

void RaceScreen::update_fade(uint64_t now) {
    hold_frame_ = false;
    if (fade_ == Fade::none) {
        g_.fade_lut.reset();
        return;
    }
    if (!fade_start_) fade_start_ = now;
    // Callback k (1-based) fires at k * 14 ms; 63 upload, the 64th releases.
    const uint64_t callbacks = (now - fade_start_) / palette_fade_period_ns;
    if (callbacks <= uint64_t(palette_fade_uploads)) {
        // Until the first callback uploads, the previous screen (the loading
        // screen's palette) stays on display: present nothing new.
        hold_frame_ = fade_ == Fade::in && callbacks == 0;
        g_.fade_lut = fade_ == Fade::in                   ? fade_in_lut(int(callbacks))
                      : callbacks == 0 && fade_from_half_ ? half_palette_lut()
                                                          : fade_out_lut(int(callbacks));
        return;
    }
    if (fade_ == Fade::in) {
        g_.fade_lut.reset();
        reset_clock(now); // 0x004163b0 starts the loop clock after the fade
    } else {
        g_.fade_lut = fade_out_lut(palette_fade_uploads);
        running_ = false;
    }
    fade_ = Fade::none;
}

void RaceScreen::reset_clock(uint64_t now_ns) {
    clock_reset_ = true; // --bench pacing leaves this frame out
    touch_.clear();
    clock_.reset();
    last_ = now_ns;
    g_.reset_view_history();
}

void RaceScreen::event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_QUIT) {
        running_ = false;
        result_.quit_app = true;
        return;
    }
    if (opt_.online && e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key >= SDLK_F1 && e.key.key <= SDLK_F8) {
            const size_t slot = size_t(e.key.key - SDLK_F1);
            if (!opt_.chat_messages[slot].empty())
                opt_.online->send(net::Chat{uint8_t(slot), opt_.chat_messages[slot]});
            return;
        }
    }
    // Renderer resets must be handled before pause-menu routing so cached
    // textures and pause frames are rebuilt from the new device.
    if (e.type == SDL_EVENT_RENDER_DEVICE_RESET || e.type == SDL_EVENT_RENDER_TARGETS_RESET) {
        sprites_.clear();
        for (auto& c : canvases_) c.clear();
        pause_tex_.reset();
        pause_scale_w_ = 0;
        pause_img_ = RgbImage();
        if (fps_) fps_ = std::make_unique<FpsOverlay>(*opt_.assets);
        if (network_overlay_) network_overlay_->invalidate();
    }
    if (e.type == SDL_EVENT_RENDER_DEVICE_LOST) throw std::runtime_error("Render device lost");
    if (!lifecycle_event(e.type)) lifecycle_.state.handle(e);
    if (g_.mixer && !g_.mixer->handle(e)) SDL_Log("Audio reopen failed: %s", SDL_GetError());
    RaceActionContext act_ctx = ctx_now();
#ifdef __SWITCH__
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
        (e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
        for (int i = 0; i < 4; ++i) {
            if (pads_[i] && SDL_GetGamepadID(pads_[i].get()) == e.gbutton.which) {
                act_ctx.has_pad = true;
                act_ctx.pad = e.gbutton.which;
                break;
            }
        }
    }
#endif
    RaceAction act = race_action(e, act_ctx);
    if (kMobileDisplay && !act_ctx.demo && !act_ctx.menu_open && !act_ctx.paused && !act_ctx.pause_message &&
        e.type == SDL_EVENT_FINGER_DOWN) {
        for (const auto& button : phone_buttons(display_.settings->left_handed))
            if (button.action == TouchAction::pause && touch_contains(button, e.tfinger.x, e.tfinger.y))
                act = act_ctx.has_menu ? RaceAction::open_pause : RaceAction::toggle_pause;
    }
    if (act == RaceAction::demo_exit) {
        running_ = false;
        return;
    }
    if (act_ctx.menu_open) {
#ifdef __SWITCH__
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
            for (int i = 0; i < 4; ++i) {
                if (pads_[i] && SDL_GetGamepadID(pads_[i].get()) == e.gbutton.which) {
                    pause_event(opt_.menu, e, pads_[i], renderer_);
                    return;
                }
            }
        }
#endif
        pause_event(opt_.menu, e, pads_[0], renderer_);
        return;
    }
    switch (act) {
    case RaceAction::quit_app:
        running_ = false;
        result_.quit_app = true;
        return;
    case RaceAction::open_pause:
        touch_.clear();
        opt_.menu->open_pause(pause_frame());
        lifecycle_.state.resume();
        return;
    case RaceAction::toggle_pause:
        touch_.clear();
        if (!opt_.online) lifecycle_.state.paused() ? lifecycle_.state.resume() : lifecycle_.state.pause();
        return;
    case RaceAction::resume:
        lifecycle_.state.resume();
        return;
    // 0x00423378: Escape is ignored until the loop has run (countdown off
    // its start value) and during a fade (the original is blocked then).
    case RaceAction::request_quit:
        if (opt_.online || (g_.countdown != countdown_ticks && fade_ == Fade::none)) g_.quitting = true;
        return;
    case RaceAction::cancel_quit:
        g_.quitting = false;
        return;
    // 0x00423408: the pump returns 1, PostQuitMessage(0); 0x00442e10 ends
    // the race on WM_QUIT at once (no 0xb4 run-on), outcome 5 only while
    // still racing, then 0x004163d0 fades out from the half palette.
    case RaceAction::confirm_quit:
        if (opt_.online) opt_.online->send(net::RoomLeave{});
        g_.quitting = false;
        end_as_quit();
        fade_ = Fade::out;
        fade_start_ = 0;
        fade_from_half_ = true;
        return;
    case RaceAction::pause_message:
        if (!opt_.online && g_.countdown != countdown_ticks && fade_ == Fade::none) g_.pause_message = true;
        return;
    case RaceAction::resume_message:
        g_.pause_message = false;
        return;
    case RaceAction::restart:
        restart_ = true;
        return;
    case RaceAction::toggle_fullscreen:
        display_.toggle_fullscreen();
        return;
    case RaceAction::none:
    case RaceAction::demo_exit:
        break;
    }
    if (e.type == SDL_EVENT_GAMEPAD_ADDED || e.type == SDL_EVENT_GAMEPAD_REMOVED) {
        int num_pads = 0;
        SDL_JoystickID* jids = SDL_GetGamepads(&num_pads);
        for (int i = 0; i < 4; ++i) {
            if (jids && i < num_pads) {
                if (!pads_[i] || !SDL_GamepadConnected(pads_[i].get()))
                    pads_[i].reset(SDL_OpenGamepad(jids[i]));
            } else {
                pads_[i].reset();
            }
        }
        if (jids) SDL_free(jids);
        if (opt_.menu) opt_.menu->set_pad_labels(pad_labels(pads_[0].get()));
    }
    if (e.type == SDL_EVENT_FINGER_DOWN || e.type == SDL_EVENT_FINGER_MOTION ||
        e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) {
        if (act == RaceAction::none) {
            touch_.set_left_handed(display_.settings->left_handed);
            touch_.handle(e);
        }
    }
    // Consume lifecycle resets in iterate(), using its frame timestamp.
}

void RaceScreen::step_online(uint64_t now, uint32_t controls) {
    auto& client = *opt_.online;
    client.update(now / 1000000, [controls](int32_t) { return uint16_t(controls); });
    while (auto event = client.poll_event()) {
        if (opt_.lobby_event &&
            (std::holds_alternative<net::RoomState>(*event) ||
             std::holds_alternative<net::ChatRelay>(*event) ||
             std::holds_alternative<net::Official>(*event) || std::holds_alternative<net::Reject>(*event)))
            opt_.lobby_event(*event);
        if (auto chat = std::get_if<net::ChatRelay>(&*event)) {
            std::string alias = "Player";
            for (const auto& human : client.start()->humans)
                if (human.seat == chat->seat) alias = human.alias;
            network_overlay_->push(alias + ": " + chat->text);
        } else if (auto official = std::get_if<net::Official>(&*event)) {
            official_ = *official;
            result_.official = *official;
            for (const auto& row : official->rows)
                if (client.start()->your_seat < client.start()->humans.size() &&
                    row.seat == client.start()->humans[client.start()->your_seat].seat) {
                    result_.place = std::max(0, int(row.place) - 1);
                    result_.outcome = row.outcome == net::Outcome::won       ? RaceOutcome::won
                                      : row.outcome == net::Outcome::busted  ? RaceOutcome::busted
                                      : row.outcome == net::Outcome::wrecked ? RaceOutcome::wrecked
                                                                             : RaceOutcome::lost;
                }
            decided_ = true;
            if (opt_.menu) opt_.menu->close_pause();
            fade_ = Fade::out;
            fade_start_ = now;
        }
    }
    while (auto event = client.poll_outcome()) {
        const auto& alias = client.start()->humans[size_t(event->slot)].alias;
        const int code = event->outcome.code;
        if (code >= 2 && code <= 5) {
            std::string line = announcements_[code == 4 ? 2 : code == 5 ? 1 : 0];
            if (auto at = line.find("%s"); at != std::string::npos) line.replace(at, 2, alias);
            if (auto at = line.find("%d"); at != std::string::npos)
                line.replace(at, 2, std::to_string(event->outcome.place));
            network_overlay_->push(std::move(line));
        }
    }
    if (client.failed()) {
        if (opt_.menu) opt_.menu->close_pause();
        result_.outcome = RaceOutcome::lost;
        running_ = false;
        return;
    }
    const int seat = client.start()->your_seat == 0xff ? 0 : client.start()->your_seat;
    const auto before = g_.view_drawn;
    g_.present_online(*client.race(), seat);
    g_.countdown = std::max(0, int(client.start()->countdown) - client.rollback()->current_tick());
    capture_view(g_.view_cur, g_.race(), g_.traffic());
    g_.view_drawn = g_.view_cur;
    g_.view_prev = g_.view_cur;
    const bool smooth = opt_.settings ? opt_.settings->smooth_remote : opt_.smooth_remote;
    if (smooth && g_.view_ready && client.stats().rollback_depth > 0)
        remote_smoothing_.correction(before, now / 1000000);
    if (smooth)
        remote_smoothing_.apply(g_.view_drawn, g_.race().humans(), now / 1000000);
    else
        remote_smoothing_.reset();
    g_.view_ready = true;
    for (const auto& event : client.take_sounds())
        if (g_.sound) g_.sound->post(event);
    // SoundSystem refreshes rpm and skid/nitro/engine state. It receives only
    // the detached presentation race; it cannot mutate rollback snapshots.
    if (g_.sound)
        g_.sound->update(g_.race());
    else
        update_engine_rpm(g_.race().riders()[0].bike);
}

bool RaceScreen::iterate(uint64_t now_ns) {
    const uint64_t now = now_ns ? now_ns : SDL_GetTicksNS();
    if (!running_) return false;
    clock_reset_ = false;
    if (!last_) reset_clock(now);
    // Plan 33a Task 5 Step 6: after a system suspend the race waits in the
    // pause menu; controllers that re-enumerated on resume are picked up.
    const bool own_freeze = g_.quitting || g_.pause_message || (opt_.menu && opt_.menu->pause_open());
    if (!opt_.capture && !opt_.bench && !opt_.online && lifecycle_.state.frame(now, own_freeze)) {
        int num_pads = 0;
        SDL_JoystickID* jids = SDL_GetGamepads(&num_pads);
        for (int i = 0; i < 4; ++i) {
            if (jids && i < num_pads) {
                if (!pads_[i] || !SDL_GamepadConnected(pads_[i].get()))
                    pads_[i].reset(SDL_OpenGamepad(jids[i]));
            } else {
                pads_[i].reset();
            }
        }
        if (jids) SDL_free(jids);
    }
#ifdef __SWITCH__
    perf_.frame_ns.fill(0); // PERF-PROBE
    FrameProfile* prof = opt_.bench ? &opt_.bench->current : &perf_;
#else
    FrameProfile* prof = opt_.bench ? &opt_.bench->current : nullptr;
#endif
    for (auto& c : canvases_) c.profile = prof;
    if (restart_) {
        g_.restart(); // Enter / pad South / tap after a quick-race finish (README)
        restart_ = false;
        decided_ = false;
        end_ticks_ = 0;
        result_ = RaceEnd{};
        reset_clock(now);
        fade_ = Fade::in;
        fade_start_ = now;
    }
    // Plan 18 Task 8: a lifecycle freeze (background return, focus loss,
    // pad removal) opens the pause menu instead of the old PAUSED text.
    if (!opt_.online && opt_.menu && !opt_.menu->pause_open() && lifecycle_.state.paused() &&
        !lifecycle_.state.background()) {
        opt_.menu->open_pause(pause_frame());
        lifecycle_.state.resume();
    }
    // A menu input may close pause_open() while queuing resume/quit. Drain
    // those events even after the menu has closed so the clock is reset.
    if (opt_.menu) {
        FrontendEvent ev;
        while (opt_.menu->poll(ev)) {
            if (ev.kind == FrontendEvent::Kind::resume_race)
                lifecycle_.state.resume();
            else if (ev.kind == FrontendEvent::Kind::quit_race) {
                lifecycle_.state.resume();
                if (opt_.online) opt_.online->send(net::RoomLeave{});
                end_as_quit();
                running_ = false;
            } else if (ev.kind == FrontendEvent::Kind::apply_display)
                display_.apply(480);
            else if (ev.kind == FrontendEvent::Kind::apply_mixer && opt_.apply_settings)
                opt_.apply_settings();
            else if (ev.kind == FrontendEvent::Kind::configure_controllers) {
#ifdef __SWITCH__
                for (auto& p : pads_) p.reset();
                const int count = configure_switch_controllers(opt_.menu->settings().local_players);
                opt_.menu->set_local_players(count);
                int num_pads = 0;
                SDL_JoystickID* jids = SDL_GetGamepads(&num_pads);
                for (int i = 0; i < 4; ++i) {
                    if (jids && i < num_pads) pads_[i].reset(SDL_OpenGamepad(jids[i]));
                }
                if (jids) SDL_free(jids);
                opt_.menu->set_pad_labels(pad_labels(pads_[0].get()));
#endif
            }
        }
    }
    if (!running_) return false;
    // The pause menu's Configure Input edits the live table (the original has
    // one global 0x00465fb8), so a rebind applies on resume.
    const bool live_table = opt_.menu != nullptr;
    const auto& key_codes = live_table ? opt_.menu->settings().controls : opt_.key_codes;
    const bool custom_keys = live_table ? opt_.menu->settings().custom_controls : opt_.custom_keys;
    if (live_table && opt_.menu->pause_open() && pads_[0])
        opt_.menu->poll_capture_joystick(joystick_capture_code(joystick_state(pads_[0].get())));
    PlayerInput in = touch_.current();
    const bool* keys = SDL_GetKeyboardState(nullptr);
    // Plan 18 Task 7: custom tables are honored via bound_control_bits below;
    // defaults use the hardcoded keys (touch_/gamepad layer through `in` either way).
    if (!custom_keys) {
        in.accelerate |= keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W];
        in.brake |= keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S];
        in.left |= keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A];
        in.right |= keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D];
        in.punch |= keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_LCTRL];
        in.kick |= keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_LALT];
        in.backhand |= keys[SDL_SCANCODE_C];
    }
    const int zone = opt_.stick_deadzone * 32768 / 100;
    // 0x0044f7c0: a custom table holding a joystick code drives the pad itself
    // (0x0044f880); the native pad mapping below then stays off so a button
    // does not also do its native action.
    const bool joy_table = custom_keys && joystick_bound(key_codes);
    const int humans = opt_.online ? 1 : std::clamp(g_.race().humans(), 1, 4);
    std::vector<uint32_t> seat_controls(humans, 0);

    for (int seat = 0; seat < humans; ++seat) {
        PlayerInput seat_in;
        uint32_t seat_c = 0;
        SDL_Gamepad* cur_pad = pads_[seat].get();
        if (cur_pad && !joy_table) {
            const auto south = SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_SOUTH);
            const auto west = SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_WEST);
            const auto north = SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_NORTH);
            seat_in.accelerate |= south || SDL_GetGamepadAxis(cur_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
            seat_in.brake |= SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_EAST) ||
                             SDL_GetGamepadAxis(cur_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
            seat_in.left |= SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            seat_in.right |= SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            seat_in.steer_axis = SDL_GetGamepadAxis(cur_pad, SDL_GAMEPAD_AXIS_LEFTX);
            const bool punch_btn = opt_.swap_punch_kick ? north : west;
            const bool kick_btn = opt_.swap_punch_kick ? west : north;
            seat_in.punch |= punch_btn;
            seat_in.kick |= kick_btn;
            seat_in.backhand |= SDL_GetGamepadButton(cur_pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
            seat_c = controls_from(seat_in, zone);
        } else if (cur_pad && joy_table) {
            const auto& race = g_.race();
            const int human = race.humans() > seat ? race.human_rider(seat) : -1;
            const int32_t top = human >= 0 ? race.riders()[size_t(human)].bike.i32(0x260) : 0;
            seat_c = joystick_control_bits(key_codes, joystick_state(cur_pad), joy_analogs_[seat], top);
        }
        if (seat == 0) {
            if (opt_.auto_drive) seat_c = autopilot(g_.race(), g_.traffic());
            seat_c |= controls_from(in, zone);
            if (!opt_.auto_drive && custom_keys) {
                const auto& race = g_.race();
                const int human = race.humans() > 0 ? race.human_rider(0) : -1;
                const int32_t top = human >= 0 ? race.riders()[size_t(human)].bike.i32(0x260) : 0;
                seat_c |= bound_control_bits(key_codes, keys, joy_analogs_[0], top);
            }
        }
        seat_controls[seat] = seat_c;
    }
    g_.race().set_joystick_analog(!opt_.auto_drive && joy_table, joy_analogs_[0].throttle, joy_analogs_[0].steer);

    if (lifecycle_.state.take_clock_reset()) {
        reset_clock(now);
    }
    // Plan 18 Task 8: the race loop stops stepping the simulation while
    // pause_open(); the mixer pauses race sounds but the interface levels
    // stay live so the sound sliders apply (no preview click, as in Task 6).
    const bool menu_pause = opt_.menu && opt_.menu->pause_open();
    const bool frozen = !opt_.online && !opt_.capture &&
                        (lifecycle_.state.paused() || g_.quitting || g_.pause_message || menu_pause);
    if (g_.mixer) g_.mixer->pause(frozen || (!opt_.capture && lifecycle_.state.background()));
    if (g_.race_music && !frozen && !lifecycle_.state.background()) g_.race_music->update();
    update_fade(now);
    // DAT_004c5bd0 for this frame: quit prompt (316) or pause message (317,
    // also shown for a lifecycle pause when there is no native pause menu).
    g_.banner_message = g_.quitting ? g_.quit_prompt
                        : !opt_.online && (g_.pause_message || (lifecycle_.state.paused() && !opt_.menu))
                            ? g_.pause_prompt
                            : std::string();
    const int steps = opt_.capture ? 1 : clock_.advance(now - last_, frozen);
    last_ = now;
    std::optional<ProfileScope> sim_scope(std::in_place, prof, FrameProfile::sim);
    if (opt_.online) step_online(now, g_.quitting || lifecycle_.state.paused() || menu_pause ? 0 : seat_controls[0]);
    for (int step = 0; step < steps && !opt_.online && !frozen && fade_ == Fade::none && running_; ++step) {
        const bool had_view = g_.view_ready;
        if (had_view) g_.view_prev = g_.view_cur; // adjacent tick snapshot
        for (int seat = 1; seat < humans && had_view; ++seat) seat_prev_[size_t(seat)] = seat_cur_[size_t(seat)];
        g_.tick(seat_controls);
        // Capture every simulation tick (even during catch-up), keeping both
        // endpoints adjacent; first tick after a reset initializes both.
        capture_view(g_.view_cur, g_.race(), g_.traffic());
        for (int seat = 1; seat < humans; ++seat) {
            capture_seat_view(seat_cur_[size_t(seat)], g_.race(), g_.traffic(), seat);
            if (!had_view) seat_prev_[size_t(seat)] = seat_cur_[size_t(seat)];
        }
        if (!g_.view_ready) g_.view_prev = g_.view_cur;
        g_.view_ready = true;
        if (!g_.career && humans <= 1) continue;
        const bool race_done = (humans > 1) ? g_.race().all_humans_done()
                                            : (g_.race().finished() || g_.race().wrecked() || g_.race().busted());
        if (!decided_ && race_done) {
            decided_ = true;
            end_ticks_ = race_end_delay;
            result_.place = std::max(0, g_.race().place(0) - 1);
            // 0x00416010: finished becomes won for places 1-3 (0-based < 3), lost otherwise;
            // 0x00407f30 arrests with outcome 4 (busted) before the bike is used up.
            result_.outcome = g_.race().busted() ? RaceOutcome::busted
                              : g_.race().finished()
                                  ? (result_.place < 3 ? RaceOutcome::won : RaceOutcome::lost)
                                  : RaceOutcome::wrecked;
        } else if (decided_ && --end_ticks_ <= 0) {
            // 0x00442e10 returned true: 0x004163d0 fades out, then returns.
            fade_ = Fade::out;
            fade_start_ = now;
            fade_from_half_ = false;
            g_.fade_lut = fade_out_lut(0);
        }
    }
    sim_scope.reset();
    if (!opt_.online && g_.view_ready) {
        const uint32_t alpha = g_.enhancements.interpolate && !opt_.capture ? clock_.alpha_q16() : 65536;
        blend_view(g_.view_drawn, g_.view_prev, g_.view_cur, alpha);
        for (int seat = 1; seat < humans; ++seat)
            blend_view(seat_drawn_[size_t(seat)], seat_prev_[size_t(seat)], seat_cur_[size_t(seat)], alpha);
    }
    if (!opt_.capture && lifecycle_.state.background()) {
        SDL_Delay(20);
        return true;
    }
    if (hold_frame_) {
        SDL_Delay(1);
        return running_;
    }
    if (menu_pause && opt_.menu) {
        // Plan 18 Task 8: the pause menu draws over the captured frame.
        // Plan 19 Task 8: pause menu draws at the effective scale too.
        int dw = 0, dh = 0;
        if (display_.window) SDL_GetWindowSize(display_.window, &dw, &dh);
        opt_.menu->set_display_size(dw, dh);
        const int pause_lw = opt_.menu->menu_width(dw, dh);
        if (!SDL_SetRenderLogicalPresentation(renderer_, pause_lw, 480, display_.config.presentation))
            throw std::runtime_error(SDL_GetError());
        const int pause_scale = display_.render_scale;
        opt_.menu->draw(pause_img_, pause_scale);
        if (!pause_tex_ || pause_scale_w_ != pause_img_.width) {
            pause_scale_w_ = pause_img_.width;
            pause_tex_.reset(SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_XRGB8888,
                                               SDL_TEXTUREACCESS_STREAMING, pause_img_.width,
                                               pause_img_.height));
            if (!pause_tex_) throw std::runtime_error(SDL_GetError());
            apply_texture_scale(pause_tex_.get(), display_.config);
        }
        if (!SDL_UpdateTexture(pause_tex_.get(), nullptr, pause_img_.pixels.data(), pause_img_.width * 4))
            throw std::runtime_error(SDL_GetError());
        if (!SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255) || !SDL_RenderClear(renderer_) ||
            !SDL_RenderTexture(renderer_, pause_tex_.get(), nullptr, nullptr))
            throw std::runtime_error(SDL_GetError());
    } else {
        draw_scene(prof);
    }
    // Plan 18 Task 9: FPS + simulation ticks/sec, top-right, Futura Bold 24
    // #DEFFF0 with a 2-pixel black shadow like all original text, twice a second.
    // The second line locates the frame: course file, level, route sample
    // (position >> 8) and race tick.
    if (fps_) {
        std::string where;
        if (g_.instance.race)
            where = g_.config.course + " L" + std::to_string(g_.config.level + 1) + "  sample " +
                    std::to_string(g_.race().position() >> 8) + "  tick " + std::to_string(g_.race().ticks());
        fps_->frame(renderer_, now, steps, where);
    }
    if (network_overlay_)
        network_overlay_->frame(
            renderer_,
            (opt_.settings ? opt_.settings->network_hud : opt_.network_hud) ? &opt_.online->stats() : nullptr,
            g_.logical_width());
    if (kMobileDisplay && !opt_.demo && !opt_.capture && !menu_pause && !lifecycle_.state.paused() &&
        !g_.quitting && !g_.pause_message)
        draw_phone_controls(renderer_, touch_.current(), display_.settings->left_handed,
                            display_.settings->touch_opacity);
    if (!opt_.screenshot.empty()) {
        SurfacePtr capture(SDL_RenderReadPixels(renderer_, nullptr));
        if (!capture || !SDL_SaveBMP(capture.get(), opt_.screenshot.c_str()))
            throw std::runtime_error(SDL_GetError());
        running_ = false;
        result_.quit_app = true;
    }
    if (opt_.capture) {
        opt_.capture->frame(renderer_, g_.mixer);
        if (opt_.capture->done()) {
            running_ = false;
            result_.quit_app = true;
        }
    }
    {
        ProfileScope ps(prof, FrameProfile::present);
        if (!SDL_RenderPresent(renderer_)) throw std::runtime_error(SDL_GetError());
    }
    display_.maybe_toggle();
#ifdef __SWITCH__
    { // PERF-PROBE
        for (size_t i = 0; i < perf_sum_.size(); ++i) perf_sum_[i] += perf_.frame_ns[i];
        perf_total_ns_ += SDL_GetTicksNS() - now;
        perf_steps_ += uint64_t(steps);
        if (++perf_frames_ == 120) {
            const double n = double(perf_frames_) * 1e6;
            std::printf("[perf] humans=%d threads=%d frame=%.2fms ticks/frame=%.2f copy=%.2f canvas=%.2f |",
                        g_.race().humans(), display_.render_threads, perf_total_ns_ / n,
                        double(perf_steps_) / double(perf_frames_), perf_copy_ns_ / n, perf_canvas_ns_ / n);
            for (int i = 0; i < FrameProfile::count; ++i)
                std::printf(" %s=%.2f", FrameProfile::names[i], perf_sum_[size_t(i)] / n);
            std::printf("\n");
            std::fflush(stdout);
            perf_sum_.fill(0);
            perf_frames_ = perf_total_ns_ = perf_copy_ns_ = perf_canvas_ns_ = perf_steps_ = 0;
        }
    }
#endif
    if (opt_.bench) {
        // Fade-in frames are skipped by the warm-up; a whole frame is
        // iterate entry to after present.
        opt_.bench->finish_frame(SDL_GetTicksNS() - now, steps, now, clock_.alpha_q16(), clock_reset_);
        if (opt_.bench->done()) {
            running_ = false;
            result_.quit_app = true;
        }
    }
    return running_;
}
} // namespace rr
