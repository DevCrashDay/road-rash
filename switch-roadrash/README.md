# Road Rash (30th Anniversary) — Nintendo Switch Homebrew Edition

A native homebrew port of **Road Rash (30th Anniversary Edition)** for the Nintendo Switch.

---

## Features
- **100% Native AArch64 Execution:** Runs bare-metal on the Nintendo Switch's Nvidia Tegra X1 (quad Cortex-A57 @ 1.02 GHz) with zero emulation overhead.
- **Hardware Graphics Pipeline:** Supports **OpenGL / OpenGL ES (via switch-mesa)** and native **deko3d (Maxwell GM20B)** for 60 FPS locked presentation.
- **Local Couch Multiplayer (Splitscreen):**
  - **2-Player Mode:** Cinematic horizontal split (1280x360 per viewport) or vertical split.
  - **3-Player Mode:** 2 viewports top, 1 centered bottom + stats/map view.
  - **4-Player Mode:** 2x2 grid (4 simultaneous racers on one console!).
- **Joy-Con Hotplugging & Single Joy-Con Mode:**
  - Full support for single horizontal Joy-Cons (play 2-player right away with the 2 Joy-Cons that come with the console).
  - Full support for Dual Joy-Cons and Nintendo Switch Pro Controllers.
- **All 30th Anniversary Assets Embedded:** 187 MB standalone `.nro` bundle with all sound effects, SoundFont banks, tracks, riders, bikes, and images bundled inside RomFS.

---

## Installation on Real Hardware (Atmosphère CFW)

1. Connect your Nintendo Switch microSD card to your PC (or use DBI / ftpd / Hekate USB Mass Storage).
2. Copy the file `roadrash.nro` to the `switch/roadrash/` directory on your SD card:
   ```
   sdmc:/switch/roadrash/roadrash.nro
   ```
3. Insert the microSD card back into your Switch and boot into **Atmosphère CFW**.
4. Launch the **Homebrew Menu** (hold `R` while launching any installed game for Title Override / full RAM mode).
5. Select **Road Rash (30th Anniversary)** and press `A` to play!

---

## Controls

### Single Horizontal Joy-Con (Ideal for 2-4 Player Couch Play)
* **Steer:** Analog Stick Left / Right
* **Accelerate:** Face Button Right (A / X)
* **Brake:** Face Button Down (B / A)
* **Punch / Weapon Attack:** Face Button Top (X / Y)
* **Kick:** Face Button Left (Y / B)
* **Nitro Boost:** Right Shoulder (SL)
* **Rear Mirror:** Left Shoulder (SR)
* **Pause / Options:** Plus (+) or Minus (-)

### Dual Joy-Con / Pro Controller
* **Steer:** Left Stick or D-Pad
* **Accelerate:** ZR (Right Trigger) or A
* **Brake:** ZL (Left Trigger) or B
* **Punch / Weapon:** X
* **Kick:** B
* **Nitro:** R
* **Rear Mirror:** L
* **Pause / Options:** Plus (+)

---

## File Locations
- Project Source & Makefile: `C:\Users\schim\Downloads\road rash\switch-roadrash`
- Built NRO Executable: `C:\Users\schim\Downloads\road rash\switch-roadrash\roadrash_switch.nro`
- SD Card Ready Package: `C:\Users\schim\Downloads\road rash\sdcard_release\switch\roadrash\roadrash.nro`
