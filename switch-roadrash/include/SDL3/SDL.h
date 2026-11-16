#pragma once
#ifndef RR_SWITCH_SDL3_COMPAT_H
#define RR_SWITCH_SDL3_COMPAT_H

#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

#define SDL_Event SDL2_Native_Event
#define SDL_Vertex SDL2_Native_Vertex
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#undef SDL_Event
#undef SDL_Vertex

inline bool SDL3_SaveBMP(SDL_Surface* surface, const char* file) {
    return SDL_SaveBMP_RW(surface, SDL_RWFromFile(file, "wb"), 1) == 0;
}
#undef SDL_SaveBMP
#define SDL_SaveBMP SDL3_SaveBMP

typedef struct SDL_FColor {
    float r, g, b, a;
} SDL_FColor;

typedef struct SDL_Vertex {
    SDL_FPoint position;
    SDL_FColor color;
    SDL_FPoint tex_coord;
} SDL_Vertex;

inline bool SDL3_RenderGeometry(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_Vertex* vertices, int num_vertices, const int* indices, int num_indices) {
    if (!vertices || num_vertices <= 0) return false;
    std::vector<SDL2_Native_Vertex> v2(num_vertices);
    for (int i = 0; i < num_vertices; ++i) {
        v2[i].position = vertices[i].position;
        v2[i].color.r = (Uint8)std::clamp(vertices[i].color.r * 255.0f, 0.0f, 255.0f);
        v2[i].color.g = (Uint8)std::clamp(vertices[i].color.g * 255.0f, 0.0f, 255.0f);
        v2[i].color.b = (Uint8)std::clamp(vertices[i].color.b * 255.0f, 0.0f, 255.0f);
        v2[i].color.a = (Uint8)std::clamp(vertices[i].color.a * 255.0f, 0.0f, 255.0f);
        v2[i].tex_coord = vertices[i].tex_coord;
    }
    return SDL_RenderGeometry(renderer, texture, v2.data(), num_vertices, indices, num_indices) == 0;
}
#define SDL_RenderGeometry SDL3_RenderGeometry

union SDL_Event {
    Uint32 type;
    SDL_CommonEvent common;
    SDL_DisplayEvent display;
    SDL_WindowEvent window;
    struct {
        Uint32 type;
        Uint32 timestamp;
        Uint32 windowID;
        Uint8 state;
        Uint8 repeat;
        Uint8 padding2;
        Uint8 padding3;
        union {
            SDL_Keysym keysym;
            struct {
                SDL_Scancode scancode;
                SDL_Keycode key;
                Uint16 mod;
            };
        };
    } key;
    SDL_TextEditingEvent edit;
    SDL_TextEditingExtEvent editExt;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyBallEvent jball;
    SDL_JoyHatEvent jhat;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyDeviceEvent jdevice;
    SDL_ControllerAxisEvent caxis;
    SDL_ControllerButtonEvent cbutton;
    SDL_ControllerDeviceEvent cdevice;
    SDL_ControllerTouchpadEvent ctouchpad;
    SDL_ControllerSensorEvent csensor;
    SDL_AudioDeviceEvent adevice;
    SDL_SensorEvent sensor;
    SDL_QuitEvent quit;
    SDL_UserEvent user;
    SDL_SysWMEvent syswm;
    SDL_TouchFingerEvent tfinger;
    SDL_MultiGestureEvent mgesture;
    SDL_DollarGestureEvent dgesture;
    SDL_DropEvent drop;

    Uint8 padding[56];
};

#define SDL_EVENT_WINDOW_FOCUS_GAINED 0x206
#define SDL_EVENT_WINDOW_FOCUS_LOST   0x207

inline int SDL_PollEvent(SDL_Event* event) {
    int res = SDL_PollEvent(reinterpret_cast<SDL2_Native_Event*>(event));
    if (res && event->type == SDL_WINDOWEVENT) {
        if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) event->type = SDL_EVENT_WINDOW_FOCUS_GAINED;
        else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) event->type = SDL_EVENT_WINDOW_FOCUS_LOST;
    }
    return res;
}

inline int SDL_PushEvent(SDL_Event* event) {
    return SDL_PushEvent(reinterpret_cast<SDL2_Native_Event*>(event));
}
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

#ifdef __SWITCH__
#include <switch.h>
#endif

// Types
#define SDL_AUDIO_S16 AUDIO_S16SYS
#define recording iscapture
#define gdevice cdevice
#define gbutton cbutton
#define gaxis caxis
#define touchID touchId
#define fingerID fingerId
#define SDL_SCANCODE_COUNT SDL_NUM_SCANCODES
#define SDL_KMOD_NONE KMOD_NONE
#define SDL_KMOD_ALT KMOD_ALT
#define SDL_KMOD_CTRL KMOD_CTRL
#define SDL_KMOD_GUI KMOD_GUI
#define SDL_KMOD_SHIFT KMOD_SHIFT

#define SDLK_A SDLK_a
#define SDLK_B SDLK_b
#define SDLK_C SDLK_c
#define SDLK_D SDLK_d
#define SDLK_E SDLK_e
#define SDLK_F SDLK_f
#define SDLK_G SDLK_g
#define SDLK_H SDLK_h
#define SDLK_I SDLK_i
#define SDLK_J SDLK_j
#define SDLK_K SDLK_k
#define SDLK_L SDLK_l
#define SDLK_M SDLK_m
#define SDLK_N SDLK_n
#define SDLK_O SDLK_o
#define SDLK_P SDLK_p
#define SDLK_Q SDLK_q
#define SDLK_R SDLK_r
#define SDLK_S SDLK_s
#define SDLK_T SDLK_t
#define SDLK_U SDLK_u
#define SDLK_V SDLK_v
#define SDLK_W SDLK_w
#define SDLK_X SDLK_x
#define SDLK_Y SDLK_y
#define SDLK_Z SDLK_z

#define SDL_GetNumLogicalCPUCores() SDL_GetCPUCount()
#define SDL_RemoveEventWatch(f, u) SDL_DelEventWatch((SDL_EventFilter)(f), u)

#define SDL_GetRenderWindow SDL_RenderGetWindow

inline bool SDL3_RenderRect(SDL_Renderer* renderer, const SDL_FRect* rect) {
    return SDL_RenderDrawRectF(renderer, rect) == 0;
}
#define SDL_RenderRect SDL3_RenderRect

inline SDL_Locale** SDL3_GetPreferredLocales(int* count) {
    if (count) *count = 1;
    SDL_Locale** loc_ptrs = (SDL_Locale**)SDL_malloc(sizeof(SDL_Locale*) * 2 + sizeof(SDL_Locale) + 16);
    SDL_Locale* loc = (SDL_Locale*)(loc_ptrs + 2);
    char* str = (char*)(loc + 1);
    std::strcpy(str, "en");
    std::strcpy(str + 4, "US");
    loc->language = str;
    loc->country = str + 4;
    loc_ptrs[0] = loc;
    loc_ptrs[1] = nullptr;
    return loc_ptrs;
}
#define SDL_GetPreferredLocales SDL3_GetPreferredLocales

inline const char* SDL_GetRendererName(SDL_Renderer* renderer) {
    static SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0) return info.name;
    return "switch_default";
}

inline void SDL3_StartTextInput(SDL_Window* = nullptr) { SDL_StartTextInput(); }
inline void SDL3_StopTextInput(SDL_Window* = nullptr) { SDL_StopTextInput(); }
#define SDL_StartTextInput SDL3_StartTextInput
#define SDL_StopTextInput SDL3_StopTextInput

inline bool SDL_RenderCoordinatesFromWindow(SDL_Renderer* renderer, float win_x, float win_y, float* x, float* y) {
    SDL_RenderWindowToLogical(renderer, (int)win_x, (int)win_y, x, y);
    return true;
}

#define SDL_SetRenderScale SDL_RenderSetScale
#define SDL_SetRenderClipRect SDL_RenderSetClipRect
inline bool SDL3_RenderFillRect(SDL_Renderer* renderer, const SDL_FRect* rect) {
    return SDL_RenderFillRectF(renderer, rect) == 0;
}
#define SDL_RenderFillRect SDL3_RenderFillRect

inline bool SDL_RenderDebugText(SDL_Renderer*, float, float, const char*) { return true; }
inline bool SDL_HasKeyboard() { return false; }

typedef struct SDL_DialogFileFilter {
    const char* name;
    const char* pattern;
} SDL_DialogFileFilter;
typedef void (*SDL_DialogFileCallback)(void* userdata, const char* const* filelist, int filter);
inline void SDL_ShowOpenFolderDialog(SDL_DialogFileCallback, void*, SDL_Window*, const char*, bool) {}
inline void SDL_ShowOpenFileDialog(SDL_DialogFileCallback, void*, SDL_Window*, const SDL_DialogFileFilter*, int, const char*, bool) {}

inline bool SDL3_Init(Uint32 flags) {
    return SDL_Init(flags) == 0;
}
#define SDL_Init SDL3_Init

inline bool SDL3_InitSubSystem(Uint32 flags) {
    return SDL_InitSubSystem(flags) == 0;
}
#define SDL_InitSubSystem SDL3_InitSubSystem

inline bool SDL3_SetRenderDrawColor(SDL_Renderer* renderer, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    return SDL_SetRenderDrawColor(renderer, r, g, b, a) == 0;
}
#define SDL_SetRenderDrawColor SDL3_SetRenderDrawColor

inline bool SDL3_RenderClear(SDL_Renderer* renderer) {
    return SDL_RenderClear(renderer) == 0;
}
#define SDL_RenderClear SDL3_RenderClear

inline bool SDL3_CreateWindowAndRenderer(const char* title, int width, int height, Uint32 window_flags, SDL_Window** window, SDL_Renderer** renderer) {
    (void)title;
    return SDL_CreateWindowAndRenderer(width, height, window_flags, window, renderer) == 0;
}
#define SDL_CreateWindowAndRenderer SDL3_CreateWindowAndRenderer

inline bool SDL3_RenderPresent(SDL_Renderer* renderer) {
    SDL_RenderPresent(renderer);
    return true;
}
#define SDL_RenderPresent SDL3_RenderPresent

inline const bool* SDL3_GetKeyboardState(int* numkeys) {
    return reinterpret_cast<const bool*>(SDL_GetKeyboardState(numkeys));
}
#define SDL_GetKeyboardState SDL3_GetKeyboardState

typedef bool (*SDL3_EventFilter)(void* userdata, SDL_Event* event);
inline bool SDL3_AddEventWatch(SDL3_EventFilter filter, void* userdata) {
    SDL_AddEventWatch((SDL_EventFilter)filter, userdata);
    return true;
}
#define SDL_AddEventWatch SDL3_AddEventWatch

inline SDL_Surface* SDL3_RenderReadPixels(SDL_Renderer* renderer, const SDL_Rect* rect) {
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    SDL_RenderReadPixels(renderer, rect, SDL_PIXELFORMAT_RGBA32, s->pixels, s->pitch);
    return s;
}
#define SDL_RenderReadPixels SDL3_RenderReadPixels

inline bool SDL_BlitSurfaceScaled(SDL_Surface* src, const SDL_Rect* srcrect, SDL_Surface* dst, const SDL_Rect* dstrect, SDL_ScaleMode scaleMode = SDL_ScaleModeNearest) {
    (void)scaleMode;
    return SDL_BlitScaled(src, (SDL_Rect*)srcrect, dst, (SDL_Rect*)dstrect) == 0;
}

inline SDL_Keycode SDL3_GetKeyFromScancode(SDL_Scancode scancode, SDL_Keymod mod = KMOD_NONE, bool alt = false) {
    return SDL_GetKeyFromScancode(scancode);
}
#define SDL_GetKeyFromScancode SDL3_GetKeyFromScancode

typedef struct SDL_AudioSpec SDL2_Native_AudioSpec;

typedef struct SDL3_AudioSpec {
    SDL_AudioFormat format;
    int channels;
    int freq;
} SDL3_AudioSpec;
#define SDL_AudioSpec SDL3_AudioSpec

typedef SDL_RWops SDL_IOStream;
typedef SDL_GameController SDL_Gamepad;
typedef SDL_GameControllerAxis SDL_GamepadAxis;
typedef SDL_GameControllerButton SDL_GamepadButton;
typedef int SDL_JoystickID;
typedef Sint64 SDL_FingerID;

#ifndef SDL_WINDOW_HIGH_PIXEL_DENSITY
#define SDL_WINDOW_HIGH_PIXEL_DENSITY 0
#endif

// Scale modes
#define SDL_SCALEMODE_NEAREST SDL_ScaleModeNearest
#define SDL_SCALEMODE_LINEAR  SDL_ScaleModeLinear
#define SDL_SCALEMODE_PIXELART SDL_ScaleModeNearest

// Logical Presentation
typedef enum {
    SDL_LOGICAL_PRESENTATION_DISABLED = 0,
    SDL_LOGICAL_PRESENTATION_STRETCH,
    SDL_LOGICAL_PRESENTATION_LETTERBOX,
    SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
} SDL_RendererLogicalPresentation;

inline bool SDL_SetRenderLogicalPresentation(SDL_Renderer* renderer, int w, int h, SDL_RendererLogicalPresentation mode) {
    if (mode == SDL_LOGICAL_PRESENTATION_DISABLED) {
        return SDL_RenderSetLogicalSize(renderer, 0, 0) == 0;
    }
    SDL_RenderSetIntegerScale(renderer, mode == SDL_LOGICAL_PRESENTATION_INTEGER_SCALE ? SDL_TRUE : SDL_FALSE);
    return SDL_RenderSetLogicalSize(renderer, w, h) == 0;
}

inline bool SDL_GetRenderLogicalPresentation(SDL_Renderer* renderer, int* w, int* h, SDL_RendererLogicalPresentation* mode) {
    int lw = 0, lh = 0;
    SDL_RenderGetLogicalSize(renderer, &lw, &lh);
    if (w) *w = lw;
    if (h) *h = lh;
    if (mode) *mode = (lw == 0 || lh == 0) ? SDL_LOGICAL_PRESENTATION_DISABLED : SDL_LOGICAL_PRESENTATION_LETTERBOX;
    return true;
}

inline bool SDL_GetRenderLogicalPresentationRect(SDL_Renderer* renderer, SDL_FRect* rect) {
    if (!rect) return false;
    SDL_Rect r;
    SDL_RenderGetViewport(renderer, &r);
    rect->x = (float)r.x;
    rect->y = (float)r.y;
    rect->w = (float)r.w;
    rect->h = (float)r.h;
    return true;
}

inline bool SDL_GetCurrentRenderOutputSize(SDL_Renderer* renderer, int* w, int* h) {
    return SDL_GetRendererOutputSize(renderer, w, h) == 0;
}

inline bool SDL_SetRenderVSync(SDL_Renderer* renderer, int vsync) {
    (void)renderer; (void)vsync;
    return true;
}

inline bool SDL_GetTextureSize(SDL_Texture* texture, float* w, float* h) {
    int iw = 0, ih = 0;
    int res = SDL_QueryTexture(texture, nullptr, nullptr, &iw, &ih);
    if (w) *w = (float)iw;
    if (h) *h = (float)ih;
    return res == 0;
}

// The Switch SDL2 GLES2 renderer has no packed 24-bit texture format. RGB24
// textures are created as ABGR8888 (byte order R,G,B,A) and tagged through
// the texture userdata so uploads are expanded to 32 bits.
inline void* SDL3_Rgb24Tag() {
    static int tag;
    return &tag;
}
inline SDL_Texture* SDL3_CreateTexture(SDL_Renderer* renderer, Uint32 format, int access, int w, int h) {
    if (format == SDL_PIXELFORMAT_RGB24) {
        SDL_Texture* t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, access, w, h);
        if (t) SDL_SetTextureUserData(t, SDL3_Rgb24Tag());
        return t;
    }
    return SDL_CreateTexture(renderer, format, access, w, h);
}
#define SDL_CreateTexture SDL3_CreateTexture

inline bool SDL3_UpdateTexture(SDL_Texture* texture, const SDL_Rect* rect, const void* pixels, int pitch) {
    if (texture && pixels && SDL_GetTextureUserData(texture) == SDL3_Rgb24Tag()) {
        int w = 0, h = 0;
        if (rect) {
            w = rect->w;
            h = rect->h;
        } else if (SDL_QueryTexture(texture, nullptr, nullptr, &w, &h) != 0)
            return false;
        static thread_local std::vector<Uint8> rgba;
        rgba.resize(size_t(w) * size_t(h) * 4);
        const Uint8* src = static_cast<const Uint8*>(pixels);
        for (int y = 0; y < h; ++y) {
            const Uint8* s = src + size_t(y) * size_t(pitch);
            Uint8* d = rgba.data() + size_t(y) * size_t(w) * 4;
            for (int x = 0; x < w; ++x, s += 3, d += 4) {
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            }
        }
        return SDL_UpdateTexture(texture, rect, rgba.data(), w * 4) == 0;
    }
    return SDL_UpdateTexture(texture, rect, pixels, pitch) == 0;
}
#define SDL_UpdateTexture SDL3_UpdateTexture

inline bool SDL3_RenderTexture(SDL_Renderer* renderer, SDL_Texture* texture, const SDL_FRect* srcrect, const SDL_FRect* dstrect) {
    SDL_Rect src;
    SDL_Rect* psrc = nullptr;
    if (srcrect) {
        src.x = (int)srcrect->x;
        src.y = (int)srcrect->y;
        src.w = (int)srcrect->w;
        src.h = (int)srcrect->h;
        psrc = &src;
    }
    return SDL_RenderCopyF(renderer, texture, psrc, dstrect) == 0;
}
#define SDL_RenderTexture SDL3_RenderTexture

// Surface helpers
inline SDL_Surface* SDL_CreateSurface(int width, int height, Uint32 format) {
    return SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, format);
}

inline SDL_Surface* SDL_CreateSurfaceFrom(int width, int height, Uint32 format, void* pixels, int pitch) {
    return SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, pitch, format);
}

inline void SDL_DestroySurface(SDL_Surface* surface) {
    SDL_FreeSurface(surface);
}

inline SDL_Surface* SDL_DuplicateSurface(SDL_Surface* surface) {
    if (!surface) return nullptr;
    return SDL_ConvertSurface(surface, surface->format, 0);
}

inline SDL_Surface* SDL_ConvertSurface(SDL_Surface* surface, Uint32 format) {
    return SDL_ConvertSurfaceFormat(surface, format, 0);
}

// IOStream helpers
#define SDL_IO_SEEK_SET RW_SEEK_SET
#define SDL_IO_SEEK_CUR RW_SEEK_CUR
#define SDL_IO_SEEK_END RW_SEEK_END

enum {
    SDL_IO_STATUS_READY = 0,
    SDL_IO_STATUS_ERROR = 1,
    SDL_IO_STATUS_EOF = 2
};

inline SDL_IOStream* SDL_IOFromFile(const char* file, const char* mode) {
    return SDL_RWFromFile(file, mode);
}

inline bool SDL_CloseIO(SDL_IOStream* context) {
    return SDL_RWclose(context) == 0;
}

inline Sint64 SDL_SeekIO(SDL_IOStream* context, Sint64 offset, int whence) {
    return SDL_RWseek(context, offset, whence);
}

inline size_t SDL_ReadIO(SDL_IOStream* context, void* ptr, size_t size) {
    return SDL_RWread(context, ptr, 1, size);
}

inline Sint64 SDL_GetIOSize(SDL_IOStream* context) {
    return SDL_RWsize(context);
}

inline int SDL_GetIOStatus(SDL_IOStream* context) {
    return SDL_IO_STATUS_READY;
}

// Time helpers
inline uint64_t SDL_GetTicksNS(void) {
#ifdef __SWITCH__
    return armGetSystemTick() * 1000000000ULL / armGetSystemTickFreq();
#else
    return (uint64_t)SDL_GetTicks() * 1000000ULL;
#endif
}

// Gamepad mappings
typedef enum {
    SDL_GAMEPAD_TYPE_UNKNOWN = 0,
    SDL_GAMEPAD_TYPE_STANDARD,
    SDL_GAMEPAD_TYPE_XBOX360,
    SDL_GAMEPAD_TYPE_XBOXONE,
    SDL_GAMEPAD_TYPE_PS3,
    SDL_GAMEPAD_TYPE_PS4,
    SDL_GAMEPAD_TYPE_PS5,
    SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO,
    SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR
} SDL_GamepadType;

typedef enum {
    SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN = 0,
    SDL_GAMEPAD_BUTTON_LABEL_A,
    SDL_GAMEPAD_BUTTON_LABEL_B,
    SDL_GAMEPAD_BUTTON_LABEL_X,
    SDL_GAMEPAD_BUTTON_LABEL_Y,
    SDL_GAMEPAD_BUTTON_LABEL_CROSS,
    SDL_GAMEPAD_BUTTON_LABEL_CIRCLE,
    SDL_GAMEPAD_BUTTON_LABEL_SQUARE,
    SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE
} SDL_GamepadButtonLabel;

#define SDL_GAMEPAD_BUTTON_SOUTH SDL_CONTROLLER_BUTTON_A
#define SDL_GAMEPAD_BUTTON_EAST  SDL_CONTROLLER_BUTTON_B
#define SDL_GAMEPAD_BUTTON_WEST  SDL_CONTROLLER_BUTTON_X
#define SDL_GAMEPAD_BUTTON_NORTH SDL_CONTROLLER_BUTTON_Y
#define SDL_GAMEPAD_BUTTON_BACK  SDL_CONTROLLER_BUTTON_BACK
#define SDL_GAMEPAD_BUTTON_GUIDE SDL_CONTROLLER_BUTTON_GUIDE
#define SDL_GAMEPAD_BUTTON_START SDL_CONTROLLER_BUTTON_START
#define SDL_GAMEPAD_BUTTON_LEFT_STICK  SDL_CONTROLLER_BUTTON_LEFTSTICK
#define SDL_GAMEPAD_BUTTON_RIGHT_STICK SDL_CONTROLLER_BUTTON_RIGHTSTICK
#define SDL_GAMEPAD_BUTTON_LEFT_SHOULDER  SDL_CONTROLLER_BUTTON_LEFTSHOULDER
#define SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
#define SDL_GAMEPAD_BUTTON_DPAD_UP    SDL_CONTROLLER_BUTTON_DPAD_UP
#define SDL_GAMEPAD_BUTTON_DPAD_DOWN  SDL_CONTROLLER_BUTTON_DPAD_DOWN
#define SDL_GAMEPAD_BUTTON_DPAD_LEFT  SDL_CONTROLLER_BUTTON_DPAD_LEFT
#define SDL_GAMEPAD_BUTTON_DPAD_RIGHT SDL_CONTROLLER_BUTTON_DPAD_RIGHT
#define SDL_GAMEPAD_BUTTON_MISC1 ((SDL_GamepadButton)15)
#define SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 ((SDL_GamepadButton)16)
#define SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 ((SDL_GamepadButton)17)
#define SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 ((SDL_GamepadButton)18)
#define SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 ((SDL_GamepadButton)19)
#define SDL_GAMEPAD_BUTTON_TOUCHPAD ((SDL_GamepadButton)20)

#define SDL_GAMEPAD_AXIS_LEFTX SDL_CONTROLLER_AXIS_LEFTX
#define SDL_GAMEPAD_AXIS_LEFTY SDL_CONTROLLER_AXIS_LEFTY
#define SDL_GAMEPAD_AXIS_LEFT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERLEFT
#define SDL_GAMEPAD_AXIS_RIGHT_TRIGGER SDL_CONTROLLER_AXIS_TRIGGERRIGHT

#define SDL_INIT_GAMEPAD SDL_INIT_GAMECONTROLLER

#define SDL_OpenGamepad(id) SDL_GameControllerOpen(id)
#define SDL_CloseGamepad(p) SDL_GameControllerClose(p)
#define SDL_GetGamepadAxis(p, a) SDL_GameControllerGetAxis(p, (SDL_GameControllerAxis)(a))
#define SDL_GetGamepadButton(p, b) SDL_GameControllerGetButton(p, (SDL_GameControllerButton)(b))
#define SDL_GetGamepadID(p) SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p))
#define SDL_GamepadConnected(p) SDL_GameControllerGetAttached(p)
#define SDL_GetGamepadType(p) SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO
#define SDL_GetGamepadButtonLabel(p, b) SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN

inline SDL_JoystickID* SDL_GetGamepads(int* count) {
    int num = SDL_NumJoysticks();
    if (count) *count = num;
    if (num <= 0) return nullptr;
    SDL_JoystickID* ids = (SDL_JoystickID*)SDL_malloc(sizeof(SDL_JoystickID) * num);
    for (int i = 0; i < num; ++i) ids[i] = i;
    return ids;
}

// Events
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_KEY_UP SDL_KEYUP
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#define SDL_EVENT_GAMEPAD_ADDED SDL_CONTROLLERDEVICEADDED
#define SDL_EVENT_GAMEPAD_REMOVED SDL_CONTROLLERDEVICEREMOVED
#define SDL_EVENT_GAMEPAD_BUTTON_DOWN SDL_CONTROLLERBUTTONDOWN
#define SDL_EVENT_GAMEPAD_BUTTON_UP SDL_CONTROLLERBUTTONUP
#define SDL_EVENT_GAMEPAD_AXIS_MOTION SDL_CONTROLLERAXISMOTION
#define SDL_EVENT_AUDIO_DEVICE_REMOVED SDL_AUDIODEVICEREMOVED
#define SDL_EVENT_WINDOW_FOCUS_GAINED 0x206
#define SDL_EVENT_WINDOW_FOCUS_LOST   0x207
#define SDL_EVENT_RENDER_DEVICE_RESET 0x1F00
#define SDL_EVENT_RENDER_TARGETS_RESET 0x1F01
#define SDL_EVENT_RENDER_DEVICE_LOST 0x1F02
#define SDL_EVENT_DID_ENTER_BACKGROUND SDL_APP_DIDENTERBACKGROUND
#define SDL_EVENT_WILL_ENTER_FOREGROUND SDL_APP_WILLENTERFOREGROUND
#define SDL_EVENT_DID_ENTER_FOREGROUND SDL_APP_DIDENTERFOREGROUND
#define SDL_EVENT_WILL_ENTER_BACKGROUND SDL_APP_WILLENTERBACKGROUND
#define SDL_EVENT_FINGER_DOWN SDL_FINGERDOWN
#define SDL_EVENT_FINGER_UP SDL_FINGERUP
#define SDL_EVENT_FINGER_MOTION SDL_FINGERMOTION
#define SDL_EVENT_FINGER_CANCELED 0x1F03
#define SDL_EVENT_TEXT_INPUT SDL_TEXTINPUT

// Hints & Misc
#define SDL_HINT_APP_ID "SDL_APP_ID"
#define SDL_HINT_APP_NAME "SDL_APP_NAME"
#define SDL_HINT_ENABLE_STEAM_SCREEN_KEYBOARD "SDL_ENABLE_STEAM_SCREEN_KEYBOARD"

// Audio Stream Adapter
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK 0
typedef void (SDLCALL *SDL3_AudioStreamCallback)(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount);

struct RRAudioBridge {
    SDL_AudioDeviceID dev_id = 0;
    SDL3_AudioStreamCallback cb = nullptr;
    void* userdata = nullptr;
    std::vector<uint8_t> buffer;
    SDL_mutex* mutex = nullptr;
    bool active = false;
};

inline void SDLCALL _rr_sdl2_audio_callback(void* userdata, Uint8* stream, int len) {
    RRAudioBridge* s = (RRAudioBridge*)userdata;
    if (!s || !s->cb) {
        std::memset(stream, 0, len);
        return;
    }
    SDL_LockMutex(s->mutex);
    s->cb(s->userdata, (SDL_AudioStream*)s, len, (int)s->buffer.size());
    int available = (int)s->buffer.size();
    int to_copy = std::min(available, len);
    if (to_copy > 0) {
        std::memcpy(stream, s->buffer.data(), to_copy);
        s->buffer.erase(s->buffer.begin(), s->buffer.begin() + to_copy);
    }
    if (to_copy < len) {
        std::memset(stream + to_copy, 0, len - to_copy);
    }
    SDL_UnlockMutex(s->mutex);
}

inline SDL_AudioStream* SDL_OpenAudioDeviceStream(SDL_AudioDeviceID devid, const SDL_AudioSpec* spec, SDL3_AudioStreamCallback callback, void* userdata) {
    RRAudioBridge* s = new RRAudioBridge();
    s->cb = callback;
    s->userdata = userdata;
    s->mutex = SDL_CreateMutex();

    SDL2_Native_AudioSpec desired, obtained;
    std::memset(&desired, 0, sizeof(desired));
    desired.freq = spec ? spec->freq : 44100;
    desired.format = spec ? spec->format : AUDIO_S16SYS;
    desired.channels = spec ? spec->channels : 2;
    desired.samples = 1024;
    desired.callback = _rr_sdl2_audio_callback;
    desired.userdata = s;

    s->dev_id = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (s->dev_id == 0) {
        SDL_DestroyMutex(s->mutex);
        delete s;
        return nullptr;
    }
    return (SDL_AudioStream*)s;
}

inline bool SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s) return false;
    SDL_PauseAudioDevice(s->dev_id, 0);
    s->active = true;
    return true;
}

inline int SDL_GetAudioStreamQueued(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s) return 0;
    SDL_LockMutex(s->mutex);
    int sz = (int)s->buffer.size();
    SDL_UnlockMutex(s->mutex);
    return sz;
}

inline bool SDL_LockAudioStream(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s) return false;
    return SDL_LockMutex(s->mutex) == 0;
}

inline bool SDL_UnlockAudioStream(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s) return false;
    return SDL_UnlockMutex(s->mutex) == 0;
}

inline bool SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* buf, int len) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s || !buf || len <= 0) return false;
    const uint8_t* p = (const uint8_t*)buf;
    s->buffer.insert(s->buffer.end(), p, p + len);
    return true;
}

inline void SDL_DestroyAudioStream(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    if (!s) return;
    SDL_CloseAudioDevice(s->dev_id);
    if (s->mutex) SDL_DestroyMutex(s->mutex);
    delete s;
}

inline SDL_AudioDeviceID SDL_GetAudioStreamDevice(SDL_AudioStream* stream) {
    RRAudioBridge* s = (RRAudioBridge*)stream;
    return s ? s->dev_id : 0;
}

#endif // RR_SWITCH_SDL3_COMPAT_H
