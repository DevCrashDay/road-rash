# Road Rash Switch Port — Handoff Log

**Goal:** The authentic reverse-engineered Road Rash engine (`road-rash-arkits/src`) running on the Nintendo Switch (Ryujinx 1.3.3 + real Atmosphère CFW hardware), with authentic 1996 PC assets and local 2–4 player splitscreen multiplayer.

---

## 1. Provenance & Authentic User Assets

- **Source Disc:** User provided authentic 1996 CD images:
  - `C:\Users\schim\Downloads\Road.Rash.1996.EA.ROADRASH.mdf` (Alcohol 120% raw 2448-byte sector disc dump)
  - `C:\Users\schim\Downloads\road rash\Road.Rash.1996.EA.ROADRASH.iso` (ISO9660 image)
- **Extracted Reference Binary:**
  - `ROADRASH.EXE` located at LBA 235646 on the user's disc image
  - File size: **541,184 bytes**
  - SHA-256: `73d7f9744b5b1a9482b3909950cdf3fe34119fe13d023b031e9c6739a60f18a2` (matches reference 1996 PC release)
  - Extracted directly into `switch-roadrash/romfs/ROADRASH.EXE`
- **Asset Packages Included in RomFS:**
  - `AUDIO/`: 53 original Sound Blaster WAV sound effects + full Soundgarden RRA CD soundtrack (`HB_TRIP.RRA`, 9.2 MB)
  - `DATA/`: Bike specs, biker data (`HIBOB.DAT`), color palette (`PALETTE.RAW`)
  - `IMAGES/`: Credits (`CREDITS.RRI`) and UI assets
  - `TEXT/`: Language resource libraries (`ENU.DLL`, `BADLOC.DLL`, `FUTB.DLL`, etc.)
  - `VIDEO/`: Complete 296 MB Cinepak `.AVI` cutscenes (Intro, Win 1-6, Lose 1-10, Wreck 1-6, Start 1-6, Bike Showrooms)

---

## 2. Architecture & File Layout

| Component | Path | Description |
| --- | --- | --- |
| **Engine Source** | `road-rash-arkits/src/` | Authentic C++20 reverse-engineered engine with original physics, AI, combat, and tables |
| **Switch Compatibility Layer** | `switch-roadrash/include/SDL3/SDL.h` | Complete SDL3-to-SDL2 bridge tailored for devkitPro `switch-sdl2` (2.28.5) |
| **Switch Platform Init** | `switch-roadrash/source/switch_platform.cpp` | RomFS mounting, stdio redirection to SD logs, exception throw wrappers (`__cxa_throw`) |
| **Build Script** | `switch-roadrash/scripts/build_switch.py` | Automated MSYS2/devkitA64 build script, ELF GCS binary patcher, RomFS packager, and NRO generator |
| **Output Binary** | `switch-roadrash/roadrash_switch.nro` | Self-contained Switch homebrew NRO (500 MB with full assets & video) |
| **SD Card Ready Release** | `sdcard_release/switch/roadrash/roadrash_switch.nro` | Drop-in folder ready to copy to a Nintendo Switch microSD card (`sdmc:/switch/roadrash/`) |
| **Game Logs** | `%APPDATA%\Ryujinx\sdcard\switch\roadrash\roadrash.log` | Redirected stdout and stderr from the game |

---

## 3. What Worked

1. **Authentic Binary Compatibility:**
   - The engine reads static tables directly from the original 1996 binary (`ROADRASH.EXE`, 541,184 bytes).
   - Successfully verified against user's original disc image `Road.Rash.1996.EA.ROADRASH.mdf` and extracted into `romfs/`.
   - Eliminates the `Unrecognised depth table` failure caused by the recompiled 30th Anniversary `RoadRash95.exe`.

2. **GCC 16 ARMv9 Guarded Control Stack (GCS) Binary Patcher:**
   - GCC 16.1.0 ARM64 unwinder contains `mrs x1, gcspr_el0` (`0xD53B2521`), which crashes Ryujinx and Switch Cortex-A57 hardware with `Unknown MRS 0xD53B2521`.
   - Automated post-link patch in `build_switch.py` replaces the preceding conditional branch with an unconditional branch and replaces the `mrs` instruction with `nop` (`0x1F2003D5`) at both unwinder sites. Exceptions propagate smoothly without crashes.

3. **SDL3 to SDL2 Compatibility Bridge:**
   - DevkitPro ships SDL2 2.28.5. Upstream SDL3 fails to build for Switch due to threading configuration constraints.
   - Built an extensive compatibility header (`switch-roadrash/include/SDL3/SDL.h`) that correctly handles:
     - Return convention differences (`bool` in SDL3 vs `int` in SDL2) for `SDL_GetCurrentRenderOutputSize`, `SDL_Init`, `SDL_RenderClear`, `SDL_RenderTexture`, etc.
     - Custom audio streamer with lock-protected buffer feeding `SDL_OpenAudioDevice`.
     - Multi-gamepad routing via `SDL_GetGamepads` and `SDL_OpenGamepad`.

4. **RGB24 to ABGR8888 Automatic Texture Promotion:**
   - Switch SDL2 GLES2 renderer rejects `SDL_PIXELFORMAT_RGB24` during Cinepak video decoding (`Parameter 'texture' is invalid`).
   - Shim creates `ABGR8888` streaming textures and dynamically expands 24-bit RGB rows to 32-bit with alpha=255. Video playback runs smoothly.

5. **Local Splitscreen Multiplayer (2, 3, and 4 Players):**
   - **Dynamic Partitioning:**
     - **2 Players:** Widescreen horizontal split (Player 1 top, Player 2 bottom) with a 2-pixel divider.
     - **3 or 4 Players:** Quad 2x2 grid (P1 top-left, P2 top-right, P3 bottom-left, P4 bottom-right).
   - **Multi-Seat Control Routing:**
     - Connected controllers (Joy-Cons, Pro Controllers) are automatically bound to Seats 0, 1, 2, 3 via `pads_[0..3]`.
     - Player 1 supports controller and keyboard.
     - Controls for all seats are packed into `std::vector<uint32_t> seat_controls` and stepped simultaneously in `Race::step(controls)`.
   - **Independent Camera & HUD:**
     - Each human rider receives an independent camera viewport via `Race::presentation_copy(seat)`.
     - Each split view displays its own bike, speedometer, mirrors, and a colored player badge (P1: Blue, P2: Red, P3: Green, P4: Yellow).
   - **Race Completion:**
     - Uses `Race::all_humans_done()` to allow all human players to finish or wreck before concluding the race.

---

## 4. What Did NOT Work / Dead Ends

1. **Using 30th Anniversary `RoadRash95.exe` (Recompiled 2026):**
   - *Problem:* `.data` section is relocated from `0x00463000` to `0x0058e000`, shifting the depth table and career tables.
   - *Result:* Throws `Unrecognised depth table` from `road.cpp:45`.
   - *Solution:* Used authentic 1996 `ROADRASH.EXE` extracted from user's CD image.
2. **Standard 7-Zip extraction on user's `Road.Rash.1996.EA.ROADRASH.iso`:**
   - *Problem:* ISO was created from a CD-ROM XA Mode 2 Form 1 track with 8-byte subheaders prepended to each sector, causing 7-Zip to fail with `Headers Error`.
   - *Solution:* Parsed the user's authentic `Road.Rash.1996.EA.ROADRASH.mdf` directly using its 2448-byte sector layout (offset +24 bytes user data) and extracted `ROADRASH.EXE` byte-for-byte.
3. **Upstream SDL3 Build on Switch:**
   - *Problem:* SDL3 upstream requires C11/C++20 threading primitives that devkitPro's current newlib toolchain does not natively expose without custom pthreads wrappers.
   - *Solution:* Built the high-performance SDL3-to-SDL2 header shim.
4. **Copying `Game` object for splitscreen viewports:**
   - *Problem:* `Game` contains non-copyable `std::unique_ptr` assets (`RaceAssets`, `RaceInstance`).
   - *Solution:* Swapped `g_.instance.race` with `std::make_unique<Race>(view)` temporarily during the viewport draw call and restored it immediately, achieving O(1) rendering without copying.

---

## 5. Attempt Log

| # | Time | Action | Outcome |
|---|---|---|---|
| 1 | 00:31 | GCS unwinder ELF patch & throw wrapper | Fixed `Unknown MRS 0xD53B2521` crash |
| 2 | 00:34 | Stdio redirection to SD logs | Allowed diagnosis of engine crashes in emulator |
| 3 | 00:41 | Tested 1996 ROADRASH.EXE | Loaded assets, audio, and menu; halted on missing `INTRO.AVI` |
| 4 | 00:43 | Packaged 296 MB videos into RomFS | Threw `Parameter 'texture' is invalid` on video texture creation |
| 5 | 00:45 | RGB24→ABGR8888 fallback & SDL3 bool convention | Video playback and audio streams rendered cleanly |
| 6 | 00:56 | Parsed user's `Road.Rash.1996.EA.ROADRASH.mdf` | Verified SHA-256 match `73d7f974...` and extracted authentic EXE |
| 7 | 01:05 | Implemented 2-4 Player Splitscreen in `race_screen.cpp` | Viewport partitioning, multi-pad input, and `presentation_copy` |
| 8 | 01:09 | Rebuilt & verified in Ryujinx | Zero errors in `roadrash_err.log`; audio stream initialized; running smoothly |
| 9 | 2026-11-11 | Fixed P2-P4 bike choppy/drifting in splitscreen | Per-seat interpolated `ViewState` (`capture_seat_view`, ids follow the `presentation_copy` row swap, seat camera via `Race::seat_view_camera`); `actor_states` swapped with the view; every human seat animates as a player. Verified in Ryujinx 4P |
| 10 | 2026-11-11 | Local Players picker (Restroom > Configure Input > Gamepad): Auto / 1-4, saved as `local_players` in settings.txt | Auto = one seat per controller; fixed count overrides it; Mano a Mano min 2. Verified: 2 Players with 4 pads configured gives a 2-way split |
| 11 | 2026-11-11 | Settings never saved on Switch | `write_file_atomic`: fsync failure ignored on `__SWITCH__`; `rename_with_retry` deletes the target and renames again (Horizon won't rename over an existing file). settings.txt now updates |

---

## 6. Build Gotchas

- Run `scripts/build_switch.py` from **PowerShell** with `$env:DEVKITPRO="C:/devkitPro"`. From Git Bash, GCC can't see TMP (`Cannot create temporary file in C:\WINDOWS\`) and `DEVKITPRO` gets mangled to `C:/Program Files/Git/opt/devkitpro` at link time.
- The build script's up-to-date check only watches `switch-roadrash/include`, **not engine headers**. After you change a header in `road-rash-arkits/src` (especially a class layout), delete `build/*.o` to force a full rebuild (about 35 s).

## 7. How to Play & Test

### In Ryujinx Emulator:
```powershell
& "C:\Users\schim\Downloads\ryujinx-1.3.3-win_x64\publish\Ryujinx.exe" "C:\Users\schim\Downloads\road rash\switch-roadrash\roadrash_switch.nro"
```

### On Real Nintendo Switch Hardware (Atmosphère CFW):
1. Copy the `sdcard_release/switch/roadrash` folder to your Switch SD card under `sdmc:/switch/roadrash/`.
2. Launch Homebrew Menu (hbmenu) on your Switch and run **Road Rash Native**.
3. Plug in 1 to 4 controllers:
   - **1 Player:** Single player fullscreen career or quick race.
   - **2 Players:** Plug in 2 controllers or select Mano a Mano -> Automatic 2-player horizontal splitscreen.
   - **3 or 4 Players:** Plug in 3 or 4 controllers -> Automatic 3-way or 4-way quad splitscreen.
4. To force a player count: Restroom > Configure Input > Gamepad > **Local Players** (Auto / 1-4). In Ryujinx, Auto counts the players enabled under Options > Settings > Input.
