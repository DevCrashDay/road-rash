#pragma once

namespace rr::netd {

inline constexpr const char* kAdminUiHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Road Rash NX — Switch Multiplayer Admin</title>
<style>
:root{
  --bg:#090d14;--panel:#111826;--panel2:#162033;--border:#23314a;
  --text:#e6edf7;--muted:#8a9bb8;--accent:#f59e0b;--cyan:#22d3ee;
  --green:#22c55e;--red:#ef4444;--purple:#a78bfa;
}
*{box-sizing:border-box}
body{margin:0;font:14px/1.45 ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text)}
header{position:sticky;top:0;z-index:10;display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;padding:12px 20px;background:rgba(11,16,26,.94);backdrop-filter:blur(8px);border-bottom:1px solid var(--border)}
.brand{display:flex;align-items:center;gap:10px;font-weight:800;letter-spacing:.04em}
.brand span.logo{background:linear-gradient(135deg,var(--accent),#ef4444);color:#0b0f17;padding:3px 8px;border-radius:6px;font-size:12px}
.badges,.actions{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
.pill{display:inline-flex;align-items:center;gap:6px;padding:3px 10px;border-radius:999px;font-size:12px;font-weight:700;border:1px solid var(--border);background:var(--panel2);color:var(--muted)}
.pill.ok{color:var(--green);border-color:rgba(34,197,94,.4);background:rgba(34,197,94,.12)}
.pill.warn{color:var(--accent);border-color:rgba(245,158,11,.4);background:rgba(245,158,11,.12)}
.pill.bad{color:var(--red);border-color:rgba(239,68,68,.4);background:rgba(239,68,68,.12)}
.pill.info{color:var(--cyan);border-color:rgba(34,211,238,.35);background:rgba(34,211,238,.1)}
main{max-width:1400px;margin:0 auto;padding:18px 20px 40px;display:grid;gap:18px}
.kpis{display:grid;grid-template-columns:repeat(auto-fit,minmax(165px,1fr));gap:12px}
.kpi{background:var(--panel);border:1px solid var(--border);border-radius:10px;padding:12px 14px}
.kpi .label{font-size:11px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted)}
.kpi .val{font-size:24px;font-weight:800;margin-top:4px;font-variant-numeric:tabular-nums}
.grid2{display:grid;grid-template-columns:1.25fr 1fr;gap:18px}
@media(max-width:1024px){.grid2{grid-template-columns:1fr}}
section.card{background:var(--panel);border:1px solid var(--border);border-radius:12px;overflow:hidden}
section.card h2{margin:0;padding:12px 16px;font-size:14px;text-transform:uppercase;letter-spacing:.05em;border-bottom:1px solid var(--border);display:flex;justify-content:space-between;align-items:center;background:var(--panel2)}
.body{padding:14px 16px}
button,select,input{font:inherit;color:var(--text);background:#0c1320;border:1px solid var(--border);border-radius:7px;padding:6px 10px}
button{cursor:pointer;font-weight:600;transition:background .15s,border-color .15s}
button:hover{background:#1a263c;border-color:#3b527a}
button.primary{background:var(--accent);color:#090d14;border-color:var(--accent)}
button.primary:hover{background:#fbbf24}
button.danger{background:rgba(239,68,68,.16);color:#fca5a5;border-color:rgba(239,68,68,.45)}
button.danger:hover{background:rgba(239,68,68,.3)}
button.sm{padding:4px 8px;font-size:12px}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{text-align:left;padding:7px 8px;border-bottom:1px solid rgba(35,49,74,.65);white-space:nowrap}
th{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.05em}
tr:last-child td{border-bottom:none}
.scroll{overflow-x:auto;max-height:360px;overflow-y:auto}
.room-card{background:#0d1422;border:1px solid var(--border);border-radius:10px;padding:12px;margin-bottom:12px}
.room-card:last-child{margin-bottom:0}
.room-top{display:flex;flex-wrap:wrap;gap:8px;justify-content:space-between;align-items:center;margin-bottom:8px}
.room-code{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:16px;font-weight:800;color:var(--cyan)}
.room-actions{display:flex;flex-wrap:wrap;gap:6px;margin-top:10px}
.room-actions input{flex:1;min-width:160px}
.ctrl-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px;align-items:end}
.field label{display:block;font-size:12px;color:var(--muted);margin-bottom:4px}
.field input[type=number],.field input[type=text],.field input[type=password]{width:100%}
.toggle-row{display:flex;align-items:center;justify-content:space-between;padding:8px 10px;background:#0d1422;border:1px solid var(--border);border-radius:8px}
.events{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px;max-height:320px;overflow-y:auto;display:flex;flex-direction:column;gap:4px}
.ev{padding:5px 8px;border-radius:6px;background:#0d1422;border-left:3px solid var(--border);word-break:break-word}
.ev.blocked_platform{border-left-color:var(--red);background:rgba(239,68,68,.08)}
.ev.desync{border-left-color:var(--accent);background:rgba(245,158,11,.08)}
.ev.race_start,.ev.race_end{border-left-color:var(--green)}
.ev.admin_kick,.ev.admin_close_room,.ev.admin_broadcast,.ev.admin_config{border-left-color:var(--cyan)}
.empty{color:var(--muted);font-style:italic;padding:12px 0}
.codebox{display:flex;gap:8px;align-items:center;margin-top:12px;padding:8px 10px;background:#0c1320;border:1px solid var(--border);border-radius:8px;font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}
.codebox code{flex:1;overflow-x:auto;color:var(--cyan)}
#loginView{max-width:420px;margin:90px auto;background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:24px}
#loginView h1{margin:0 0 6px;font-size:20px}
#loginView p{margin:0 0 16px;color:var(--muted);font-size:13px}
#loginErr{color:var(--red);font-size:13px;min-height:18px;margin-top:8px}
.hidden{display:none!important}
</style>
</head>
<body>
<div id="loginView">
  <h1>Road Rash NX Admin</h1>
  <p>Nintendo Switch-only multiplayer console. Sign in with your <code>RR_NETD_ADMIN_TOKEN</code>.</p>
  <form id="loginForm">
    <div class="field"><label for="tok">Admin Token</label><input id="tok" type="password" autocomplete="current-password" required placeholder="Enter admin token"></div>
    <div style="margin-top:14px;display:flex;gap:8px">
      <button class="primary" type="submit" style="flex:1">Sign In</button>
    </div>
    <div id="loginErr"></div>
  </form>
</div>

<div id="appView" class="hidden">
<header>
  <div class="brand">
    <span class="logo">NX ADMIN</span>
    <span>Road Rash Switch Multiplayer</span>
    <span id="revPill" class="pill">rev -</span>
  </div>
  <div class="badges">
    <span id="nxPill" class="pill ok">Switch-Only: ON</span>
    <span id="maintPill" class="pill">Open</span>
    <span id="upPill" class="pill info">Up 0s</span>
    <select id="pollSel" title="Auto refresh">
      <option value="1000">1s live</option>
      <option value="2000" selected>2s poll</option>
      <option value="5000">5s poll</option>
      <option value="0">Manual</option>
    </select>
    <button id="refreshBtn" class="sm">Refresh</button>
    <button id="logoutBtn" class="sm danger">Logout</button>
  </div>
</header>

<main>
  <div class="kpis" id="kpis"></div>

  <div class="grid2">
    <section class="card">
      <h2><span>Live Switch Rooms &amp; Races</span><span id="roomCountBadge" class="pill">0</span></h2>
      <div class="body" id="roomsBox"><div class="empty">No active rooms</div></div>
    </section>

    <section class="card">
      <h2><span>Server Controls &amp; Switch Endpoint</span></h2>
      <div class="body">
        <div class="ctrl-grid">
          <div class="toggle-row">
            <span>Switch-Only Gate (<code>switch_nx</code>)</span>
            <button id="toggleNxBtn" class="sm">Toggle</button>
          </div>
          <div class="toggle-row">
            <span>Maintenance Mode</span>
            <button id="toggleMaintBtn" class="sm">Toggle</button>
          </div>
          <div class="field">
            <label for="minHumansInp">Min Players to Start</label>
            <input id="minHumansInp" type="number" min="1" max="8" value="2">
          </div>
          <div class="field">
            <label for="maxRoomsInp">Max Rooms</label>
            <input id="maxRoomsInp" type="number" min="1" max="5000" value="500">
          </div>
        </div>
        <div style="margin-top:10px;display:flex;gap:8px;justify-content:flex-end">
          <button id="applyCfgBtn" class="primary sm">Apply Config</button>
        </div>
        <div class="codebox">
          <span>Switch <code>settings.txt</code>:</span>
          <code id="switchUrlCode">net_server=...</code>
          <button id="copyUrlBtn" class="sm">Copy</button>
        </div>
      </div>
    </section>
  </div>

  <div class="grid2">
    <section class="card">
      <h2><span>Connected Clients</span><span id="connCountBadge" class="pill">0</span></h2>
      <div class="body scroll" id="connsBox"><div class="empty">No active connections</div></div>
    </section>

    <section class="card">
      <h2><span>Security &amp; Event Feed</span><span id="blockedBadge" class="pill">0 blocked</span></h2>
      <div class="body"><div class="events" id="eventsBox"><div class="empty">No events yet</div></div></div>
    </section>
  </div>

  <section class="card">
    <h2><span>Race Logs (.rrlog)</span><span id="logCountBadge" class="pill">0 files</span></h2>
    <div class="body scroll" id="logsBox"><div class="empty">No race logs recorded</div></div>
  </section>
</main>
</div>

<script>
const $ = (id) => document.getElementById(id);
const CHARS = ['','Natasha','Biff','P.E.#1','Cydney','Mondo','Bose','Slick','Krusty'];
const COURSES = ['','The City','The Peninsula','Pacific Highway','Sierra Nevada','Napa Valley'];
const AI_MODES = ['Humans Only','Limited AI','Full AI'];
let timer = null, stateData = null;

function esc(s) {
  return String(s ?? '').replace(/[&<>"']/g, (c) => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}
function fmtMs(ms) {
  const s = Math.floor((ms || 0) / 1000);
  if (s < 60) return s + 's';
  const m = Math.floor(s / 60), rs = s % 60;
  if (m < 60) return m + 'm ' + rs + 's';
  return Math.floor(m / 60) + 'h ' + (m % 60) + 'm';
}

async function api(url, opts = {}) {
  const res = await fetch(url, { credentials: 'same-origin', ...opts });
  if (res.status === 401 || res.status === 403) {
    showLogin(res.status === 403 ? 'Admin token not configured on server (RR_NETD_ADMIN_TOKEN).' : '');
    throw new Error('auth');
  }
  return res.json();
}

function showLogin(msg = '') {
  if (timer) { clearInterval(timer); timer = null; }
  $('appView').classList.add('hidden');
  $('loginView').classList.remove('hidden');
  $('loginErr').textContent = msg;
}
function showApp() {
  $('loginView').classList.add('hidden');
  $('appView').classList.remove('hidden');
  const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
  $('switchUrlCode').textContent = 'net_server=' + proto + location.host + '/net';
  loadAll();
  resetPoll();
}
function resetPoll() {
  if (timer) clearInterval(timer);
  const ms = Number($('pollSel').value);
  if (ms > 0) timer = setInterval(() => loadAll().catch(() => {}), ms);
}

$('loginForm').addEventListener('submit', async (e) => {
  e.preventDefault();
  $('loginErr').textContent = '';
  const r = await fetch('/admin/api/login', {
    method: 'POST', credentials: 'same-origin',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ token: $('tok').value })
  });
  if (!r.ok) {
    const j = await r.json().catch(() => ({}));
    $('loginErr').textContent = j.error || ('Login failed (' + r.status + ')');
    return;
  }
  $('tok').value = '';
  showApp();
});
$('logoutBtn').onclick = async () => {
  await fetch('/admin/api/logout', { method: 'POST', credentials: 'same-origin' });
  showLogin('Signed out.');
};
$('refreshBtn').onclick = () => loadAll();
$('pollSel').onchange = resetPoll;
$('copyUrlBtn').onclick = () => navigator.clipboard?.writeText($('switchUrlCode').textContent);

async function postAction(url, body) {
  await api(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  await loadAll();
}

$('toggleNxBtn').onclick = () => stateData && postAction('/admin/api/config', { switch_only: !stateData.config.switch_only });
$('toggleMaintBtn').onclick = () => stateData && postAction('/admin/api/config', { maintenance_mode: !stateData.config.maintenance_mode });
$('applyCfgBtn').onclick = () => postAction('/admin/api/config', {
  min_humans: Number($('minHumansInp').value),
  max_rooms: Number($('maxRoomsInp').value)
});

async function loadAll() {
  const [st, logs] = await Promise.all([api('/admin/api/state'), api('/admin/api/logs')]);
  stateData = st;
  renderState(st);
  renderLogs(logs.logs || []);
}

function renderState(st) {
  const cfg = st.config || {}, c = st.counters || {};
  $('revPill').textContent = 'rev ' + String(st.rev || '?').slice(0, 7);
  $('upPill').textContent = 'Up ' + fmtMs(st.now_ms);
  $('nxPill').textContent = cfg.switch_only ? 'Switch-Only: ON' : 'Switch-Only: OFF';
  $('nxPill').className = 'pill ' + (cfg.switch_only ? 'ok' : 'bad');
  $('maintPill').textContent = cfg.maintenance_mode ? 'Maintenance' : 'Accepting Rooms';
  $('maintPill').className = 'pill ' + (cfg.maintenance_mode ? 'warn' : 'ok');
  if (document.activeElement !== $('minHumansInp')) $('minHumansInp').value = cfg.min_humans;
  if (document.activeElement !== $('maxRoomsInp')) $('maxRoomsInp').value = cfg.max_rooms;

  const cards = [
    ['Switch Players', c.active_players ?? 0, ''],
    ['Active Rooms', c.active_rooms ?? 0, ''],
    ['Open Sockets', c.active_connections ?? 0, ''],
    ['Blocked PC/Web', c.blocked_non_switch ?? 0, (c.blocked_non_switch || 0) > 0 ? 'color:var(--red)' : ''],
    ['Races Started', c.races_started ?? 0, ''],
    ['Races Finished', c.races_finished ?? 0, ''],
    ['Desyncs', c.desyncs ?? 0, (c.desyncs || 0) > 0 ? 'color:var(--accent)' : ''],
    ['Late Inputs', c.late_inputs ?? 0, '']
  ];
  $('kpis').innerHTML = cards.map(([l, v, style]) =>
    `<div class="kpi"><div class="label">${esc(l)}</div><div class="val" style="${style}">${esc(v)}</div></div>`
  ).join('');

  const rooms = st.rooms || [];
  $('roomCountBadge').textContent = rooms.length;
  $('roomsBox').innerHTML = rooms.length ? rooms.map((r) => {
    const seatsHtml = (r.seats || []).map((s) => `
      <tr>
        <td>#${s.seat + 1}${s.seat === r.host_seat ? ' <span class="pill warn">HOST</span>' : ''}</td>
        <td><strong>${esc(s.alias)}</strong></td>
        <td>${esc(CHARS[s.character] || ('#' + s.character))}</td>
        <td>${esc(COURSES[s.vote] || '—')}</td>
        <td>${s.ready ? '<span class="pill ok">Ready</span>' : '<span class="pill">Wait</span>'}</td>
        <td>${s.ping_ms} ms</td>
        <td>${s.connected ? esc(s.outcome || 'live') : '<span class="pill warn">away</span>'}</td>
      </tr>`).join('');
    const raceInfo = r.race
      ? `<span class="pill ok">RACING tick ${r.race.sealed_tick} · ${esc(COURSES[r.race.course] || r.race.course)}</span>`
      : `<span class="pill info">${esc(r.status.toUpperCase())}</span>`;
    return `
      <div class="room-card">
        <div class="room-top">
          <div>
            <span class="room-code">${esc(r.code)}</span>
            ${raceInfo}
            <span class="pill">${r.public ? 'Public' : 'Unlisted'}</span>
            <span class="pill">Lvl ${r.level + 1} · ${esc(AI_MODES[r.ai] || 'AI')} · ${r.cars ? 'Cars' : 'No Cars'}</span>
          </div>
          <div class="pill">fp ${esc(r.fingerprint)} · age ${fmtMs(r.age_ms)}</div>
        </div>
        <div class="scroll"><table>
          <thead><tr><th>Seat</th><th>Alias</th><th>Rider</th><th>Vote</th><th>Ready</th><th>Ping</th><th>State</th></tr></thead>
          <tbody>${seatsHtml}</tbody>
        </table></div>
        <div class="room-actions">
          <input id="msg-${esc(r.code)}" placeholder="System notice to room ${esc(r.code)}...">
          <button class="sm" onclick="sendRoomMsg('${esc(r.code)}')">Broadcast</button>
          <button class="sm danger" onclick="closeRoom('${esc(r.code)}')">Close Room</button>
        </div>
      </div>`;
  }).join('') : '<div class="empty">No active Switch rooms</div>';

  const conns = st.connections || [];
  $('connCountBadge').textContent = conns.length;
  $('connsBox').innerHTML = conns.length ? `<table>
    <thead><tr><th>ID</th><th>Platform</th><th>Alias</th><th>Room</th><th>RTT</th><th>Age</th><th>Action</th></tr></thead>
    <tbody>${conns.map((cn) => `
      <tr>
        <td>#${cn.id}</td>
        <td><span class="pill ${cn.platform === 'switch_nx' ? 'ok' : 'bad'}">${esc(cn.platform)}</span></td>
        <td>${esc(cn.alias || '(hello...)')}</td>
        <td>${cn.room ? esc(cn.room) + (cn.seat >= 0 ? ' #' + (cn.seat + 1) : ' (spec)') : 'lobby'}</td>
        <td>${cn.rtt_ms} ms</td>
        <td>${fmtMs(cn.age_ms)}</td>
        <td><button class="sm danger" onclick="kickConn(${cn.id})">Kick</button></td>
      </tr>`).join('')}
    </tbody></table>` : '<div class="empty">No active connections</div>';

  const evs = st.events || [];
  $('blockedBadge').textContent = (c.blocked_non_switch ?? 0) + ' blocked';
  $('eventsBox').innerHTML = evs.length ? evs.map((ev) =>
    `<div class="ev ${esc(ev.kind)}">[${fmtMs(ev.ts_ms)}] <strong>${esc(ev.kind)}</strong> ${esc(JSON.stringify(ev.data))}</div>`
  ).join('') : '<div class="empty">No events yet</div>';
}

function renderLogs(logs) {
  $('logCountBadge').textContent = logs.length + ' files';
  $('logsBox').innerHTML = logs.length ? `<table>
    <thead><tr><th>File</th><th>Size</th><th>Action</th></tr></thead>
    <tbody>${logs.map((l) => `
      <tr>
        <td><code>${esc(l.name)}</code></td>
        <td>${esc(l.size)} bytes</td>
        <td><a href="/admin/api/logs/${encodeURIComponent(l.name)}" download><button class="sm">Download</button></a></td>
      </tr>`).join('')}
    </tbody></table>` : '<div class="empty">No finished race logs yet</div>';
}

window.kickConn = (id) => postAction('/admin/api/kick', { conn_id: id });
window.closeRoom = (code) => postAction('/admin/api/room/close', { code });
window.sendRoomMsg = (code) => {
  const el = $('msg-' + code);
  if (!el || !el.value.trim()) return;
  postAction('/admin/api/room/broadcast', { code, text: el.value.trim() });
  el.value = '';
};

loadAll().then(() => showApp()).catch(() => showLogin());
</script>
</body>
</html>
)HTML";

} // namespace rr::netd
