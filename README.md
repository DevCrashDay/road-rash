# Road Rash NX — Nintendo Switch Port & Switch-Only Multiplayer

Fork of [arkits/road-rash](https://github.com/arkits/road-rash) focused on **Nintendo Switch homebrew** (`aarch64` / libnx), **local 1–4 player couch splitscreen**, and a **self-hosted Switch-to-Switch-only online multiplayer backend** (`rr-netd`) with an authenticated Web Admin Console.

Companion Web ISO → NRO builder repository: [DevCrashDay/road-rash-nx](https://github.com/DevCrashDay/road-rash-nx).

---

## Key Additions in This Fork

### Nintendo Switch Port (`__SWITCH__` & `switch-nx` branch)
- **Hardware GLES2 Race Renderer (`src/gpu_race.cpp`):** Renders sky/horizon, world quads, fog, grading/post-effects, and dashboard directly into SDL target textures on Switch / Ryujinx.
- **Native Horizon Controller Applet (`hidLaShowControllerSupport`):** Always boots in single-player mode (`local_players = 1` on Switch startup) and adds 2–4 local players only when connected through **Controls → Controllers** in the main menu or in-race pause menu.
- **Selectable Splitscreen Layout:** Choose **Horizontal** or **Vertical** splitscreen in **Gamepad** or **Display** settings for 2–4 local players with per-seat aspect scaling and player badges (`P1`–`P4`).
- **Cached Asset Digest (`net_digest_v2.bin`):** Verifies the reference disc data once and caches the digest on SD (`sdmc:/switch/roadrash/`) for fast online lobby startup.
- **Switch WebSocket Transport (`ws_channel_switch.cpp`):** Native `bsd:u` / `ssl` / `sfdnsres` WebSocket client supporting `ws://` (LAN/Ryujinx) and `wss://` (TLS).

### Online Multiplayer Fixes
- **Switch Platform Identity (`Platform::switch_nx`):** Identifies Switch clients cleanly in `Hello` handshakes (`platform = 7`).
- **Fullscreen Online Viewport:** Forces 1-player fullscreen rendering during online races instead of splitting the screen across remote opponent seats.
- **Unified Rollback Clock:** Uses `SDL_GetTicks()` consistently across lobby handover (`Client::begin`) and race ticks (`RaceScreen::step_online`), preventing countdown `7` clock-sync stalls.
- **Online In-Race Pause Menu (`+` / `-`):** Opens the pause overlay (`Resume`, `Sound`, `Display`, `Controls`, `Quit Race`) without pausing online rollback ticks; **Quit Race** sends `RoomLeave` and returns cleanly to the lobby.

### Switch-Only Multiplayer Backend & Web Admin Console (`server/` & `switch-server/`)
- **Strict Platform Gate (`RR_NETD_SWITCH_ONLY=1`):** Rejects non-Switch clients (`web`, `windows`, `macos`, `linux`, `android`, `bot`, `unknown`) at `Hello` with `RejectReason::protocol` while logging and counting blocked attempts.
- **Web Admin Console (`/admin` & `/net/health`):** Single-page dark admin dashboard behind Caddy reverse proxy, protected by `RR_NETD_ADMIN_TOKEN` (cookie / Bearer auth):
  - Live KPIs (`players`, `rooms`, `connections`, `blocked_non_switch`, `desyncs`, `late_inputs`).
  - Live room/seat monitor (roster, characters, votes, ready flags, ping, current tick) with **Kick**, **Close Room**, and **Broadcast** actions.
  - Runtime config toggles (`switch_only`, `maintenance_mode`, `min_humans`, `max_rooms`), event feed, and `.rrlog` replay downloads.

---

## Branch Layout

| Branch | Contents |
| --- | --- |
| `main` | Core C++20/SDL engine, GLES2 Switch race renderer, `rr-netd` Switch-only server + `/admin` console, C++ tests |
| `switch-nx` | Everything on `main` plus `switch-roadrash/` (libnx wrapper, WS client, `build_switch.py`, PC/Web builders) and `switch-server/` (Docker Compose, Caddyfile, 2-instance Ryujinx launcher) |

*(Commercial game data, `.iso` disc dumps, `romfs/`, compiled `.nro`/`.elf`/`.nsp` binaries, and `.env` / key files are excluded from source control.)*

---

## Running the Switch-Only Server (`switch-server/`)

1. Check out `switch-nx` (or copy `switch-server/` alongside `road-rash-arkits/`).
2. Configure environment and build the stack with Docker Compose:
   ```sh
   cd switch-server
   cp .env.example .env   # set a strong RR_NETD_ADMIN_TOKEN
   docker compose up -d --build
   ```
3. Endpoints exposed via Caddy on port `8080`:
   - **Switch WebSocket:** `ws://<server-ip>:8080/net` (or `wss://<domain>/net` after setting your domain in `switch-server/Caddyfile`)
   - **Web Admin Console:** `http://localhost:8080/admin`
   - **Health JSON:** `http://localhost:8080/net/health`

To test locally with two emulator windows on Windows, run `switch-server/launch_2_ryujinx.cmd` (creates isolated `.ryu-p1` / `.ryu-p2` profiles pointed at `ws://127.0.0.1:8080/net` with internet access enabled). To let a real Switch on the same Wi-Fi join a PC server/Ryujinx session, allow inbound TCP port `8080` in Windows Firewall and set `net_server=ws://<PC-LAN-IP>:8080/net`.

---

## Real Switch & Ryujinx Setup

1. Build `roadrash_switch.nro` via [DevCrashDay/road-rash-nx](https://github.com/DevCrashDay/road-rash-nx) (supply your own 1996/1997 PC `ROADRASH.ISO`) or from source with devkitPro (`python switch-roadrash/scripts/build_switch.py` on the `switch-nx` branch). Both Switch and Ryujinx peers must run matching builds (identical `RR_SIM_SOURCE_HASH`).
2. Copy the NRO to your microSD card:
   ```text
   sdmc:/switch/roadrash/roadrash_switch.nro
   ```
3. Configure your server URL in `sdmc:/switch/roadrash/settings.txt` (created on first run):
   ```ini
   net_server=ws://<server-lan-ip>:8080/net
   ```
4. Launch on Switch in **full RAM mode** (hold `R` while opening any installed title to open `hbmenu`, or use an `application_type: 1` forwarder targeting `sdmc:/switch/roadrash/roadrash_switch.nro`).

---

## Switch Controls

| Action | Dual Joy-Con / Pro Controller |
| --- | --- |
| Accelerate | `A` / `ZR` |
| Brake | `B` / `ZL` |
| Steer | Left Stick / D-Pad |
| Punch / Kick / Backhand | `Y` / `X` / `R` |
| Pause / In-Race Menu | `+` / `-` |
| Controller & Splitscreen Setup | **Controls → Controllers** / **Split Screen** |

---

## Building Desktop / Server Standalone

Requires CMake 3.22+, a C++20 compiler, and SDL3 3.2+ (the server target builds without SDL or game assets):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target rr-netd --parallel
```

Your own unmodified 1997 Windows disc data (`ROADRASH.EXE` + 341 SHA-256 verified files) is required to run the game client. For full desktop/mobile/WASM documentation and reverse-engineering references, see `docs/` and upstream [arkits/road-rash](https://github.com/arkits/road-rash). Commercial assets are not included; third-party notices live in `third_party/`.
