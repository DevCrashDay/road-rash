import os

headers = [
    'SDL_events.h', 'SDL_keyboard.h', 'SDL_scancode.h', 'SDL_gamepad.h',
    'SDL_video.h', 'SDL_render.h', 'SDL_surface.h', 'SDL_audio.h',
    'SDL_iostream.h', 'SDL_rect.h', 'SDL_timer.h', 'SDL_hints.h',
    'SDL_pixels.h', 'SDL_mouse.h', 'SDL_dialog.h'
]
out_dir = r'C:/Users/schim/Downloads/road rash/switch-roadrash/include/SDL3'
for h in headers:
    path = os.path.join(out_dir, h)
    with open(path, 'w', encoding='utf-8') as f:
        f.write('#pragma once\n#include "SDL.h"\n')
print("Successfully wrote forwarding headers")
