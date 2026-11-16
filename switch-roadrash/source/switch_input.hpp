#pragma once
#include <cstdint>
#include <array>
#include <switch.h>

namespace rr::switch_port {

// Bitmask for Road Rash controls
constexpr uint32_t kCtrlThrottle   = 1u << 0;
constexpr uint32_t kCtrlBrake      = 1u << 1;
constexpr uint32_t kCtrlSteerLeft  = 1u << 2;
constexpr uint32_t kCtrlSteerRight = 1u << 3;
constexpr uint32_t kCtrlPunch      = 1u << 4;
constexpr uint32_t kCtrlKick       = 1u << 5;
constexpr uint32_t kCtrlNitro      = 1u << 6;
constexpr uint32_t kCtrlMirror     = 1u << 7;
constexpr uint32_t kCtrlPause      = 1u << 8;

enum class ControllerLayout {
    Auto,
    SingleJoyConHorizontal,
    DualJoyConOrPro
};

struct PlayerInputState {
    uint32_t buttons = 0;       // Mask of kCtrl*
    int16_t steer_axis = 0;     // -32768 (full left) to +32767 (full right)
    int16_t throttle_axis = 0;  // 0 to 32767
    int16_t brake_axis = 0;     // 0 to 32767
    bool connected = false;
};

class SwitchInputManager {
public:
    static constexpr int kMaxPlayers = 4;

    void initialize();
    void update();

    // Returns the bitmask and analog states for player 0..3
    const PlayerInputState& player_state(int player_idx) const;
    uint32_t control_mask(int player_idx) const;

    int connected_player_count() const { return connected_count_; }

private:
    std::array<PadState, kMaxPlayers> pads_{};
    std::array<PlayerInputState, kMaxPlayers> states_{};
    int connected_count_ = 1;
};

} // namespace rr::switch_port
