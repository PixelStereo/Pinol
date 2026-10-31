#pragma once
// The page the phone remote serves (RemoteServer.h): a single file, no library, no external request. Its style and its script
// are hashed into the Content-Security-Policy at start-up, so that nothing but this exact code can run in it.
#include <QByteArray>

namespace remote_page {
inline constexpr char kStyle[] = R"CSS(
:root { color-scheme: dark; --bg:#17171a; --panel:#232328; --line:#34343b; --text:#ececf0; --dim:#8a8a94; --accent:#d6d6db; }
* { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
html, body { margin:0; background:var(--bg); color:var(--text); font:16px/1.35 -apple-system, system-ui, "Segoe UI", sans-serif; }
header { position:sticky; top:0; z-index:2; display:flex; align-items:center; gap:10px; padding:12px 16px; background:var(--bg); border-bottom:1px solid var(--line); }
header h1 { flex:1; margin:0; font-size:17px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
#state { font-size:12px; color:var(--dim); }
#state.ok::before { content:"● "; color:#46c46a; }
#state.bad::before { content:"● "; color:#e5564a; }
main { display:grid; gap:12px; padding:16px; grid-template-columns:repeat(auto-fill,minmax(260px,1fr)); }
.card { background:var(--panel); border:1px solid var(--line); border-radius:12px; padding:14px; }
.card label { display:flex; justify-content:space-between; gap:8px; margin-bottom:10px; color:var(--dim); font-size:14px; }
.card label b { color:var(--text); font-weight:600; }
input[type=range] { width:100%; height:44px; margin:0; accent-color:var(--accent); touch-action:pan-y; }
.switch, .hold, .choice button { width:100%; min-height:56px; border-radius:10px; border:1px solid var(--line); background:#2d2d34; color:var(--text); font:inherit; font-size:17px; touch-action:none; user-select:none; -webkit-user-select:none; }
.switch.on, .hold.on, .choice button.on { background:var(--accent); color:#1a1a1d; border-color:var(--accent); font-weight:600; }
.choice { display:grid; grid-template-columns:repeat(auto-fit,minmax(84px,1fr)); gap:8px; }
.choice button { min-height:50px; font-size:15px; touch-action:manipulation; }
input[type=color] { width:100%; height:56px; border:1px solid var(--line); border-radius:10px; background:#2d2d34; padding:4px; }
#login { max-width:340px; margin:18vh auto 0; padding:0 20px; text-align:center; }
#login input { width:100%; padding:14px; font-size:22px; text-align:center; letter-spacing:.3em; background:var(--panel); color:var(--text); border:1px solid var(--line); border-radius:10px; }
#login p { color:var(--dim); }
#login button { width:100%; margin-top:12px; min-height:56px; border-radius:10px; border:0; background:var(--accent); color:#1a1a1d; font:inherit; font-size:18px; font-weight:600; }
#empty { color:var(--dim); text-align:center; padding:40px 16px; grid-column:1/-1; }
)CSS";

inline constexpr char kBody[] = R"HTML(
<header><h1 id="title">NyxMap</h1><span id="state">…</span></header>
<div id="login" hidden><h2>NyxMap remote</h2><p id="loginText">Enter the code set in NyxMap (Settings › OSC › Phone remote).</p><input id="pin" type="password" autocomplete="off" autocapitalize="off" autocorrect="off" spellcheck="false" maxlength="32" enterkeyhint="go"><button id="go" type="button">Connect</button></div>
<main id="list" hidden></main>
)HTML";

inline constexpr char kScript[] = R"JS(
(() => {
  const $ = s => document.querySelector(s);
  let pin = new URLSearchParams(location.search).get('pin') || sessionStorage.getItem('nyxmap-pin') || '', needPin = true;
  let ws = null, opened = false, failures = 0, controls = [], values = {}, active = new Set(), sendAt = {};
  const state = (text, cls) => { const s = $('#state'); s.textContent = text; s.className = cls || ''; };
  const send = obj => { if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); };
  const throttled = (key, fn) => { const now = performance.now(); if (now - (sendAt[key] || 0) > 33) { sendAt[key] = now; fn(); return true; } return false; };
  const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
  const fmt = (c, v) => { const d = c.step >= 1 ? 0 : Math.min(4, Math.ceil(-Math.log10(c.step))); return Number(v).toFixed(d) + (c.unit || ''); };
  const hex = rgb => '#' + rgb.map(x => Math.round(Math.max(0, Math.min(1, x)) * 255).toString(16).padStart(2, '0')).join('');
  const unhex = h => [1, 3, 5].map(i => parseInt(h.substr(i, 2), 16) / 255);
  function login(message) {
    $('#list').hidden = true; $('#login').hidden = false;
    if (message) $('#loginText').textContent = message;
    const input = $('#pin'); input.value = ''; input.focus();
    const go = () => { if (!input.value) return; pin = input.value.trim(); failures = 0; $('#login').hidden = true; connect(); };
    input.onkeydown = e => { if (e.key === 'Enter') go(); };
    $('#go').onclick = go; // a phone's keypad may have no Enter key
  }
  function connect() {
    if (needPin && !pin) return login();
    state('connecting…');
    opened = false;
    ws = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws' + (needPin ? '?pin=' + encodeURIComponent(pin) : ''));
    ws.onopen = () => { opened = true; failures = 0; sessionStorage.setItem('nyxmap-pin', pin); state('connected', 'ok'); };
    ws.onmessage = e => {
      let m; try { m = JSON.parse(e.data); } catch (_) { return; }
      if (m.t === 'controls') { controls = m.list || []; $('#title').textContent = m.title || 'NyxMap'; values = {}; controls.forEach(c => { values[c.id] = c.kind === 'color' ? c.rgb : c.value; }); render(); }
      else if (m.t === 'values') { Object.assign(values, m.v || {}); update(); }
    };
    ws.onclose = () => {
      state('offline', 'bad');
      if (needPin && !opened && ++failures >= 2) { sessionStorage.removeItem('nyxmap-pin'); pin = ''; login('Wrong PIN, or NyxMap cannot be reached. Enter the PIN again.'); return; }
      setTimeout(connect, Math.min(4000, 400 + failures * 500));
    };
  }
  function render() {
    const list = $('#list'); list.hidden = false; list.textContent = '';
    if (!controls.length) { list.appendChild(el('div', '', 'No control yet. Make some in NyxMap › Controls.')).id = 'empty'; return; }
    for (const c of controls) {
      const card = el('div', 'card'); card.dataset.id = c.id;
      const head = el('label'); head.appendChild(el('span', '', c.name)); const val = el('b', 'val', ''); head.appendChild(val);
      if (c.kind !== 'slider') val.hidden = true;
      card.appendChild(head);
      if (c.kind === 'slider') {
        const r = el('input'); r.type = 'range'; r.min = c.min; r.max = c.max; r.step = c.step;
        const on = () => active.add(c.id), off = () => { active.delete(c.id); send({ t: 'set', id: c.id, v: Number(r.value) }); };
        r.addEventListener('pointerdown', on); r.addEventListener('pointerup', off); r.addEventListener('pointercancel', off);
        r.addEventListener('input', () => { val.textContent = fmt(c, r.value); throttled(c.id, () => send({ t: 'set', id: c.id, v: Number(r.value) })); });
        card.appendChild(r);
      } else if (c.kind === 'toggle') {
        const b = el('button', 'switch', 'Off'); b.onclick = () => send({ t: 'set', id: c.id, v: values[c.id] >= 0.5 ? 0 : 1 }); card.appendChild(b);
      } else if (c.kind === 'button') {
        const b = el('button', 'hold', 'Hold');
        const down = e => { e.preventDefault(); b.setPointerCapture && b.setPointerCapture(e.pointerId); send({ t: 'set', id: c.id, v: 1 }); };
        const up = () => send({ t: 'set', id: c.id, v: 0 });
        b.addEventListener('pointerdown', down); b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
        b.addEventListener('contextmenu', e => e.preventDefault()); card.appendChild(b);
      } else if (c.kind === 'choice') {
        const row = el('div', 'choice');
        c.choices.forEach((name, i) => { const b = el('button', '', name); b.onclick = () => send({ t: 'set', id: c.id, v: i }); row.appendChild(b); });
        card.appendChild(row);
      } else if (c.kind === 'color') {
        const p = el('input'); p.type = 'color';
        p.addEventListener('input', () => throttled(c.id, () => send({ t: 'color', id: c.id, rgb: unhex(p.value) })));
        p.addEventListener('change', () => send({ t: 'color', id: c.id, rgb: unhex(p.value) }));
        p.addEventListener('focus', () => active.add(c.id)); p.addEventListener('blur', () => active.delete(c.id));
        card.appendChild(p);
      }
      list.appendChild(card);
    }
    update();
  }
  function update() {
    for (const c of controls) {
      const card = document.querySelector('.card[data-id="' + c.id + '"]'); if (!card) continue;
      const v = values[c.id];
      if (c.kind === 'slider') { const r = card.querySelector('input'); if (!active.has(c.id)) r.value = v; card.querySelector('.val').textContent = fmt(c, active.has(c.id) ? r.value : v); }
      else if (c.kind === 'toggle') { const b = card.querySelector('button'); b.classList.toggle('on', v >= 0.5); b.textContent = v >= 0.5 ? 'On' : 'Off'; }
      else if (c.kind === 'button') { card.querySelector('button').classList.toggle('on', v >= 0.5); }
      else if (c.kind === 'choice') { card.querySelectorAll('button').forEach((b, i) => b.classList.toggle('on', i === Math.round(v))); }
      else if (c.kind === 'color') { if (!active.has(c.id)) card.querySelector('input').value = hex(v); }
    }
  }
  // Whether it asks a code: the server says (it says nothing else before the code)
  fetch('/info').then(r => r.json()).then(i => { needPin = !!i.pin; }).catch(() => {}).then(connect);
})();
)JS";
} // namespace remote_page
