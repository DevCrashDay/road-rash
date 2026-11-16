#include "switch_input.hpp"
#include <cmath>

namespace rr::switch_port {

void SwitchInputManager::initialize() {
    // Enable Standard handheld, Dual Joy-Con/Pro Controller, and Single Left/Right Joy-Con styles
    padConfigureInput(kMaxPlayers, HidNpadStyleSet_NpadStandard);

    // Initialize pad states for Player 1..4 (HidNpadIdType_No1 .. No4)
    for (int i = 0; i < kMaxPlayers; ++i) {
        padInitialize(&pads_[size_t(i)], static_cast<HidNpadIdType>(i));
        states_[size_t(i)] = {};
    }
}

void SwitchInputManager::update() {
    connected_count_ = 0;

    for (int i = 0; i < kMaxPlayers; ++i) {
        PadState& pad = pads_[size_t(i)];
        padUpdate(&pad);

        const uint64_t held = padGetButtons(&pad);
        const uint32_t style = padGetStyleSet(&pad);

        PlayerInputState& state = states_[size_t(i)];
        state = {};

        if (style != 0) {
            state.connected = true;
            connected_count_++;
        } else if (i == 0) {
            // Player 1 fallback (default connected)
            state.connected = true;
            connected_count_++;
        }

        uint32_t mask = 0;

        // Detect if controller is held as a single horizontal Joy-Con
        const bool is_single_joycon = (style & (HidNpadStyleTag_NpadJoyLeft | HidNpadStyleTag_NpadJoyRight)) != 0;

        if (is_single_joycon) {
            // Single Joy-Con horizontal mappings:
            // Top button: X or Up -> Punch
            // Right button: A or Right -> Throttle
            // Bottom button: B or Down -> Brake
            // Left button: Y or Left -> Kick
            // SL / SR -> Nitro / Rear Mirror
            // Plus / Minus -> Pause
            if (held & (HidNpadButton_Right | HidNpadButton_A)) {
                mask |= kCtrlThrottle;
                state.throttle_axis = 32767;
            }
            if (held & (HidNpadButton_Down | HidNpadButton_B)) {
                mask |= kCtrlBrake;
                state.brake_axis = 32767;
            }
            if (held & (HidNpadButton_Up | HidNpadButton_X)) {
                mask |= kCtrlPunch;
            }
            if (held & (HidNpadButton_Left | HidNpadButton_Y)) {
                mask |= kCtrlKick;
            }
            if (held & (HidNpadButton_AnySL)) {
                mask |= kCtrlNitro;
            }
            if (held & (HidNpadButton_AnySR)) {
                mask |= kCtrlMirror;
            }
            if (held & (HidNpadButton_Plus | HidNpadButton_Minus)) {
                mask |= kCtrlPause;
            }

            // Analog stick for steering
            const HidAnalogStickState stick = padGetStickPos(&pad, 0);
            if (stick.x < -8000) {
                mask |= kCtrlSteerLeft;
                state.steer_axis = stick.x;
            } else if (stick.x > 8000) {
                mask |= kCtrlSteerRight;
                state.steer_axis = stick.x;
            }
        } else {
            // Standard Dual Joy-Con, Handheld, or Pro Controller:
            if (held & (HidNpadButton_ZR | HidNpadButton_A)) {
                mask |= kCtrlThrottle;
                state.throttle_axis = 32767;
            }
            if (held & (HidNpadButton_ZL | HidNpadButton_B)) {
                mask |= kCtrlBrake;
                state.brake_axis = 32767;
            }
            if (held & HidNpadButton_X) {
                mask |= kCtrlPunch;
            }
            if (held & HidNpadButton_Y) {
                mask |= kCtrlKick;
            }
            if (held & HidNpadButton_R) {
                mask |= kCtrlNitro;
            }
            if (held & HidNpadButton_L) {
                mask |= kCtrlMirror;
            }
            if (held & (HidNpadButton_Plus | HidNpadButton_Minus)) {
                mask |= kCtrlPause;
            }

            // D-Pad steering
            if (held & HidNpadButton_Left) {
                mask |= kCtrlSteerLeft;
                state.steer_axis = -32767;
            }
            if (held & HidNpadButton_Right) {
                mask |= kCtrlSteerRight;
                state.steer_axis = 32767;
            }

            // Left Analog Stick
            const HidAnalogStickState stick = padGetStickPos(&pad, 0);
            if (stick.x < -8000) {
                mask |= kCtrlSteerLeft;
                state.steer_axis = stick.x;
            } else if (stick.x > 8000) {
                mask |= kCtrlSteerRight;
                state.steer_axis = stick.x;
            }
        }

        state.buttons = mask;
    }

    if (connected_count_ < 1) {
        connected_count_ = 1;
    }
}

const PlayerInputState& SwitchInputManager::player_state(int player_idx) const {
    if (player_idx < 0 || player_idx >= kMaxPlayers) {
        static PlayerInputState empty{};
        return empty;
    }
    return states_[size_t(player_idx)];
}

uint32_t SwitchInputManager::control_mask(int player_idx) const {
    return player_state(player_idx).buttons;
}

} // namespace rr::switch_port
