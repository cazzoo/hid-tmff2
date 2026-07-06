"use strict";

const $ = (id) => document.getElementById(id);

// ---- WebSocket --------------------------------------------------------

let ws = null;
function connect() {
  ws = new WebSocket((location.protocol === "https:" ? "wss://" : "ws://")
                     + location.host + "/ws");
  ws.onopen    = () => { $("ws-state").className = "dot on";  };
  ws.onclose   = () => { $("ws-state").className = "dot off"; setTimeout(connect, 1000); };
  ws.onerror   = () => { ws.close(); };
  ws.onmessage = (ev) => { onSample(JSON.parse(ev.data)); };
}
connect();

// ---- Status bar --------------------------------------------------------

function onStatus(s) {
  $("mode").textContent     = "mode: "      + (s.mode         || "—");
  $("device").textContent   = "device: "    + (s.device       || "—");
  $("virtual").textContent  = (s.virtual_path
    ? "virtual: " + s.virtual_path + "  (point your game here)"
    : "");
}

// ---- Rolling scope ------------------------------------------------------
const N = 900;
const canvas = $("scope");
const ctx    = canvas.getContext("2d");
const torqueBuf = new Float32Array(N);
const posBuf    = new Float32Array(N);
const gainBuf   = new Float32Array(N);
let head = 0, filled = 0;

function push(buf, v) { buf[head] = v; }
function commit() { head = (head + 1) % N; if (filled < N) filled++; }

function drawScope() {
  const W = canvas.width, H = canvas.height;
  ctx.clearRect(0, 0, W, H);

  // Grid lines
  ctx.strokeStyle = "#21262d"; ctx.lineWidth = 1;
  for (let i = 1; i <= 3; i++) {
    const y = (H / 4) * i;
    ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke();
  }
  // Zero axis
  ctx.strokeStyle = "#30363d";
  ctx.beginPath(); ctx.moveTo(0, H/2); ctx.lineTo(W, H/2); ctx.stroke();

  if (filled < 2) return;
  const count = filled;
  const start = (head - count + N) % N;
  const yAt = (v) => H/2 - v * (H/2 - 4);
  const xAt = (i) => (i / (N - 1)) * W;

  function trace(buf, color, lw) {
    ctx.strokeStyle = color; ctx.lineWidth = lw; ctx.beginPath();
    for (let i = 0; i < count; i++) {
      const x = xAt(i), y = yAt(buf[(start + i) % N]);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
  }
  trace(gainBuf,  "#d2a8ff", 1);
  trace(posBuf,   "#3fb950", 1);
  trace(torqueBuf,"#58a6ff", 2);
}

// ---- Gauges ------------------------------------------------------------

function setStrength(v) {
  const pct = Math.max(0, Math.min(100, Math.round(v * 100)));
  $("strength-fill").style.height = pct + "%";
  $("strength-val").textContent   = pct + "%";
}
function setDirection(deg, torque) {
  const t = Math.max(-1, Math.min(1, torque || 0));
  const fill = $("dir-fill");
  if (t >= 0) { fill.style.left = "50%"; fill.style.width = (t * 50) + "%"; }
  else         { fill.style.left = (50 + t * 50) + "%"; fill.style.width = (-t * 50) + "%"; }
  $("dir-val").textContent = Math.round(deg || 0) + "°";
}
function setBig(id, v, digits = 2) { $(id).textContent = v.toFixed(digits); }

// ---- Effects table -----------------------------------------------------

const effTbody = $("effects").querySelector("tbody");

function renderEffects(playing) {
  effTbody.innerHTML = "";
  if (!playing || !playing.length) {
    $("no-effects").style.display = "block";
    $("effects").style.display    = "none";
    return;
  }
  $("no-effects").style.display = "none";
  $("effects").style.display    = "";

  for (const e of playing) {
    const lvl = e.level        != null ? e.level
              : e.magnitude    != null ? e.magnitude
              : (e.start_level != null ? e.start_level + "→" + (e.end_level ?? "?")
              : (e.center      != null ? "c=" + e.center : "—"));
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td>${e.id}</td>
       <td>${e.name || e.type}</td>
       <td>${e.waveform || "—"}</td>
       <td class="num">${Math.round((e.direction || 0) * 360 / 0x10000)}</td>
       <td class="num">${lvl}</td>
       <td class="num">${e.age_ms}ms</td>
       <td class="num">${e.length_ms || "∞"}</td>
       <td class="num">${e.repeat || 1}</td>
       <td class="num">${(e.contrib != null ? e.contrib : 0).toFixed(3)}</td>`;
    effTbody.appendChild(tr);
  }
}

// ---- Sample dispatch ---------------------------------------------------

function onSample(s) {
  onStatus(s);
  push(torqueBuf, s.torque || 0);
  push(posBuf,    s.position    || 0);
  push(gainBuf,   s.gain        || 0);
  commit();
  drawScope();

  setStrength(s.strength || 0);
  setDirection(s.direction_deg || 0, s.torque || 0);
  setBig("torque-val", s.torque || 0);
  setBig("pos-val",    s.position || 0);
  $("gain-val").textContent = "gain " + Math.round((s.gain || 0) * 100) + "%";
  $("vel-val").textContent  = "vel " + (s.velocity || 0).toFixed(2) + "/s";
  renderEffects(s.playing);
}

// ---- Gains -------------------------------------------------------------

let gainsBuilt = false;
async function loadGains() {
  let gains = {};
  try {
    const r = await fetch("/api/gains");
    const j = await r.json();
    gains = j.gains || {};
    $("gains-note").textContent = j.observe ? "(observe mode)" : "";
  } catch (_) { /* server may not be ready yet */ }
  renderGains(gains);
  return gains;
}
function renderGains(gains) {
  const host = $("gains");
  const keys = Object.keys(gains);
  if (!keys.length) {
    host.innerHTML = '<span class="muted">No driver gain sysfs attributes found '
      + '(driver-agnostic: only appears for drivers exposing '
      + 'spring_level / damper_level / friction_level / gain).</span>';
    gainsBuilt = true;
    return;
  }
  host.innerHTML = "";
  for (const key of keys) {
    const g = gains[key];
    const row = document.createElement("div");
    row.className = "gain-row" + (g.writable ? " enabled" : " disabled");
    row.innerHTML =
      `<label>${g.name}</label>
       <input type="range" min="${g.min}" max="${g.max}" value="${g.value}"
              data-key="${key}" ${g.writable ? "" : "disabled"}>
       <span class="gv">${g.value}${g.unit}</span>`;
    host.appendChild(row);

    const input = row.querySelector("input");
    const gv    = row.querySelector(".gv");
    input.addEventListener("input", () => { gv.textContent = input.value + g.unit; });
    input.addEventListener("change", async () => {
      try {
        await fetch("/api/gains", {
          method:  "POST",
          headers: { "Content-Type": "application/json" },
          body:    JSON.stringify({ key, value: parseInt(input.value, 10) }),
        });
      } catch (_) { /* ignore */ }
    });
  }
  gainsBuilt = true;
}

loadGains();
setInterval(() => { if (gainsBuilt) loadGains(); }, 3000);
