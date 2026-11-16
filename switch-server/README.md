# Road Rash NX — Switch-Only Multiplayer Server & Admin Console

Dedicated `rr-netd` multiplayer backend restricted strictly to Nintendo Switch (`Platform::switch_nx`) clients, blocking PC, browser, mobile, and bot connections, paired with an authenticated Web Admin Console.

## Quick Start (Docker Compose)

1. Copy `.env.example` to `.env` and set a strong `RR_NETD_ADMIN_TOKEN`:
   ```sh
   cp .env.example .env
   ```
2. Build and start the stack:
   ```sh
   docker compose up -d --build
   ```
3. Open the Web Admin Console:
   - Local: `http://localhost:8080/admin`
   - Health JSON: `http://localhost:8080/net/health`
   - Switch WebSocket endpoint: `ws://<LAN-IP>:8080/net` (or `wss://<domain>/net` behind TLS)

## Nintendo Switch Client Setup

In `sdmc:/switch/roadrash/settings.txt` (generated on first run), set:
```ini
net_server=ws://<your-server-ip>:8080/net
```
Or over HTTPS/WSS when hosted on a public domain (edit `Caddyfile` to replace `:80` with `your-domain.example.com`):
```ini
net_server=wss://your-domain.example.com/net
```

## Admin Console Features (`/admin`)

- **Switch-Only Enforcement (`RR_NETD_SWITCH_ONLY=1`)**: Rejects `web`, `windows`, `macos`, `linux_desktop`, `android`, `bot`, and `unknown` clients on `Hello`, counting and logging every blocked attempt.
- **Rooms & Seats Monitor**: View public & unlisted rooms, seat rosters, character picks, course votes, ready states, pings, and live race tick/desync counts.
- **Moderation**: Kick connected clients, close stalled rooms, or broadcast system chat notices into any room.
- **Runtime Controls**: Toggle Switch-only gate, toggle Maintenance Mode, adjust `min_humans` (`1` for solo testing, `2` for matches) and `max_rooms`.
- **Race Logs**: Browse and download `.rrlog` race/desync recordings.
