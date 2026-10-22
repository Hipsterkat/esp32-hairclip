/*
 * Hairclip control page. Talks to the firmware's JSON API (docs/API.md).
 * Every string from the device or the user is inserted with textContent,
 * never as HTML.
 */
'use strict';

(function main() {
  const preview = createPreview(HC_FONTS, HC_CHARMAP);
  const $ = (id) => document.getElementById(id);
  const BUILTIN = ['Hello!', 'Be Happy :)', 'Love You'];
  const SCALE = 6; // canvas pixels per OLED pixel
  const POLL_MS = 4000;
  const KEEPALIVE_MS = 20000;

  const st = {
    device: null, // last /api/settings answer
    display: null, // draft display settings shown in the controls
    status: null,
    statusAt: 0,
    failures: 0,
    wifiOff: false,
    lastKeepalive: 0,
  };

  // ---- HTTP ---------------------------------------------------------------

  async function api(path, data) {
    const ctrl = new AbortController();
    const timer = setTimeout(() => ctrl.abort(), 8000);
    try {
      const opts = { signal: ctrl.signal, cache: 'no-store', headers: {} };
      if (data) {
        opts.method = 'POST';
        opts.headers['X-Hairclip'] = '1';
        opts.body = new URLSearchParams(data);
      }
      const res = await fetch(path, opts);
      let body = null;
      try {
        body = await res.json();
      } catch (e) {
        body = null;
      }
      if (!res.ok || !body || body.ok === false) {
        const err = new Error((body && body.error) || 'Request failed (HTTP ' + res.status + ')');
        err.body = body;
        throw err;
      }
      return body;
    } catch (e) {
      if (e.name === 'AbortError' || e instanceof TypeError) {
        throw new Error('The hairclip did not answer. Is your phone still on its Wi-Fi?');
      }
      throw e;
    } finally {
      clearTimeout(timer);
    }
  }

  // ---- Small UI helpers -----------------------------------------------------

  let toastTimer = 0;
  function toast(msg, kind) {
    const t = $('toast');
    t.textContent = msg;
    t.className = 'toast show ' + (kind || '');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.className = 'toast'; }, 2800);
  }

  function banner(msg, kind) {
    const b = $('banner');
    b.hidden = !msg;
    b.textContent = msg || '';
    b.className = 'banner' + (kind ? ' ' + kind : '');
  }

  function setConn(kind, text) {
    $('conn').className = 'pill ' + kind;
    $('connText').textContent = text;
    $('sConn').textContent = text;
  }

  function busy(btn, on) {
    btn.disabled = on;
    btn.classList.toggle('busy', on);
  }

  const fmtSeconds = (s) => Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');

  function fmtUptime(s) {
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    return (d ? d + 'd ' : '') + (d || h ? h + 'h ' : '') + m + 'm';
  }

  // ---- Preview --------------------------------------------------------------

  const canvas = $('oled');
  const ctx = canvas.getContext('2d');
  let scene = null;
  let animStart = 0;
  let lastKey = null;
  let raf = 0;

  function paint(px) {
    const d = st.display;
    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    const level = 0.45 + 0.55 * (d.brightness / 100);
    ctx.fillStyle = 'rgba(232,244,255,' + level.toFixed(2) + ')';
    for (let y = 0; y < 32; y++) {
      for (let x = 0; x < 128; x++) {
        if (!px[y * 128 + x] === !d.invert) continue;
        const sx = d.rotation === 180 ? 127 - x : x;
        const sy = d.rotation === 180 ? 31 - y : y;
        ctx.fillRect(sx * SCALE + 0.5, sy * SCALE + 0.5, SCALE - 1, SCALE - 1);
      }
    }
  }

  function tick(now) {
    raf = 0;
    if (!scene) return;
    const f = preview.frame(scene, now - animStart);
    if (f.key !== lastKey) {
      paint(f.pixels);
      lastKey = f.key;
    }
    if (scene.animated) raf = requestAnimationFrame(tick);
  }

  function refreshPreview() {
    if (!st.device) return;
    const lim = st.device.limits;
    const n = preview.normalize($('message').value, lim.maxChars, lim.maxBytes);
    scene = preview.makeScene(n.text, st.display, !st.wifiOff);
    animStart = performance.now();
    lastKey = null;
    if (!raf) raf = requestAnimationFrame(tick);
  }

  // ---- Message editor ---------------------------------------------------------

  function updateEditor() {
    const lim = st.device.limits;
    const n = preview.normalize($('message').value, lim.maxChars, lim.maxBytes);
    const counter = $('counter');
    counter.textContent = n.chars + ' / ' + lim.maxChars;
    counter.classList.toggle('over', n.tooLong);
    $('save').disabled = n.tooLong;
    const bad = [...new Set(n.cps.filter((cp) => cp !== 10 && !preview.isDisplayable(cp)))];
    const warn = $('warn');
    warn.hidden = bad.length === 0;
    warn.textContent = bad.length
      ? 'Shown as “?” on the clip: ' + bad.slice(0, 8).map((cp) => String.fromCodePoint(cp)).join(' ')
      : '';
    let hint = '';
    if (n.tooLong) hint = 'Too long for the clip';
    else if (n.changed) hint = 'Smart quotes and emoji are simplified';
    else if (n.chars === 0) hint = 'Saving an empty message blanks the display';
    $('hint').textContent = hint;
    refreshPreview();
  }

  function showCurrent(msg) {
    $('current').textContent = msg === '' ? '(empty: display blank)' : msg;
  }

  async function saveMessage(text) {
    const btn = $('save');
    busy(btn, true);
    try {
      const r = await api('/api/message', { message: text });
      st.device.message = r.message;
      showCurrent(r.message);
      if ($('message').value !== r.message) {
        $('message').value = r.message;
        updateEditor();
      }
      let msg = r.changed ? 'Saved (some characters were simplified)' : 'Saved';
      if (r.unsupported && r.unsupported.length) msg = 'Saved, but some characters show as “?”';
      toast(msg, 'ok');
    } catch (e) {
      if (e.body && e.body.message !== undefined) showCurrent(e.body.message);
      toast(e.message, 'err');
    } finally {
      busy(btn, false);
      updateEditor();
    }
  }

  // ---- Presets ------------------------------------------------------------

  function chip(text, index) {
    const b = document.createElement('button');
    b.type = 'button';
    b.className = 'chip';
    b.textContent = text;
    b.addEventListener('click', () => {
      $('message').value = text;
      updateEditor();
      saveMessage(text);
    });
    if (index === undefined) return b;
    const wrap = document.createElement('span');
    wrap.className = 'chipwrap';
    const del = document.createElement('button');
    del.type = 'button';
    del.className = 'chipdel';
    del.textContent = '×';
    del.setAttribute('aria-label', 'Delete preset ' + text);
    del.addEventListener('click', async () => {
      if (!confirm('Delete the preset “' + text + '”?')) return;
      try {
        const r = await api('/api/presets', { action: 'delete', index: String(index) });
        st.device.presets = r.presets;
        renderPresets();
        toast('Preset deleted', 'ok');
      } catch (e) {
        toast(e.message, 'err');
      }
    });
    wrap.append(b, del);
    return wrap;
  }

  function renderPresets() {
    const box = $('presets');
    box.replaceChildren(...BUILTIN.map((t) => chip(t)), ...st.device.presets.map((t, i) => chip(t, i)));
    $('addPreset').disabled = st.device.presets.length >= st.device.limits.maxPresets;
  }

  async function addPreset() {
    const lim = st.device.limits;
    const n = preview.normalize($('message').value, lim.maxChars, lim.maxBytes);
    if (n.chars === 0) {
      toast('Type a message first', 'err');
      return;
    }
    try {
      const r = await api('/api/presets', { action: 'add', text: n.text });
      st.device.presets = r.presets;
      renderPresets();
      toast('Preset saved', 'ok');
    } catch (e) {
      toast(e.message, 'err');
    }
  }

  // ---- Display settings -----------------------------------------------------

  function renderControls() {
    const d = st.display;
    for (const id of ['font', 'align']) {
      for (const b of $(id).querySelectorAll('button')) b.setAttribute('aria-checked', String(b.dataset.v === d[id]));
    }
    const font = st.device.fonts.find((f) => f.id === d.font);
    const twoOk = !!(font && font.twoLines);
    $('lines').checked = d.lines === 2;
    $('lines').disabled = !twoOk;
    $('linesHint').textContent = twoOk ? 'When the text fits on two lines' : 'Small font only';
    $('scroll').checked = d.scroll;
    $('speed').value = d.speed;
    $('speedVal').textContent = d.speed + ' / 10';
    $('speed').disabled = !d.scroll;
    $('brightness').value = d.brightness;
    $('brightnessVal').textContent = d.brightness + '%';
    $('rotation').checked = d.rotation === 180;
    $('invert').checked = d.invert;
  }

  const wire = (v) => (v === true ? '1' : v === false ? '0' : String(v));

  async function sendSetting(field, value, persist) {
    st.display[field] = value;
    renderControls();
    refreshPreview();
    try {
      const r = await api('/api/settings', { [field]: wire(value), persist: persist ? '1' : '0' });
      st.device.display = r.display;
    } catch (e) {
      toast(e.message, 'err');
    }
  }

  function bindSlider(id, parse) {
    const el = $(id);
    let last = 0;
    let pending = 0;
    el.addEventListener('input', () => {
      st.display[id] = parse(el.value);
      renderControls();
      refreshPreview();
      // Live preview on the real panel while dragging, at most ~5 per second.
      const now = Date.now();
      clearTimeout(pending);
      const send = () => {
        last = Date.now();
        sendSetting(id, parse(el.value), false);
      };
      if (now - last > 200) send();
      else pending = setTimeout(send, 200 - (now - last));
    });
    el.addEventListener('change', () => {
      clearTimeout(pending);
      sendSetting(id, parse(el.value), true);
    });
  }

  function bindControls() {
    for (const id of ['font', 'align']) {
      $(id).addEventListener('click', (ev) => {
        const b = ev.target.closest('button');
        if (b && b.dataset.v !== st.display[id]) sendSetting(id, b.dataset.v, true);
      });
    }
    $('lines').addEventListener('change', (ev) => sendSetting('lines', ev.target.checked ? 2 : 1, true));
    $('scroll').addEventListener('change', (ev) => sendSetting('scroll', ev.target.checked, true));
    $('rotation').addEventListener('change', (ev) => sendSetting('rotation', ev.target.checked ? 180 : 0, true));
    $('invert').addEventListener('change', (ev) => sendSetting('invert', ev.target.checked, true));
    bindSlider('speed', (v) => parseInt(v, 10));
    bindSlider('brightness', (v) => parseInt(v, 10));
  }

  // ---- Status, keep-alive and Wi-Fi off ---------------------------------------

  function renderStatus() {
    const s = st.status;
    if (!s || st.wifiOff) return; // keep the "Wi-Fi off" notice once the user turned it off
    const w = s.wifi;
    const since = Math.floor((Date.now() - st.statusAt) / 1000);
    $('sWifi').textContent = 'Access point “' + w.ssid + '” · ' + w.ip + ' · ' + w.clients + ' connected';
    $('sFw').textContent = s.firmware;
    $('sUp').textContent = fmtUptime(s.uptimeS + since);
    $('sOled').textContent = s.oled.detected ? 'Detected at ' + s.oled.address : 'Not detected at boot (check wiring)';
    $('sReset').textContent = s.resetReason + (s.brownouts ? ' · ' + s.brownouts + ' brownout reset(s) seen' : '');
    if (w.state === 'stopping') {
      $('sAuto').textContent = 'now';
      banner('The hairclip is turning its Wi-Fi off. Hold BOOT for 1.5 s to turn it back on.', 'err');
      setConn('warn', 'Wi-Fi turning off');
      return;
    }
    const left = Math.max(0, w.autoOffInS - since);
    $('sAuto').textContent = fmtSeconds(left) + ' without activity';
    if (st.failures === 0) setConn('ok', 'Connected');
    if (left <= 30 && !st.wifiOff) banner('Wi-Fi turns off in ' + left + ' s to save battery. Tap here to stay connected.', '');
    else if (st.failures === 0) banner('');
  }

  async function pollStatus() {
    if (document.hidden || st.wifiOff) return;
    try {
      st.status = await api('/api/status');
      st.statusAt = Date.now();
      st.failures = 0;
      renderStatus();
    } catch (e) {
      st.failures++;
      if (st.failures >= 2) {
        setConn('err', 'Not connected');
        banner('Can’t reach the hairclip. Its Wi-Fi may have turned off: hold BOOT for 1.5 s, then rejoin its network.', 'err');
      }
    }
  }

  async function keepAlive(force) {
    const now = Date.now();
    if (st.wifiOff || (!force && now - st.lastKeepalive < KEEPALIVE_MS)) return;
    st.lastKeepalive = now;
    try {
      const r = await api('/api/keepalive', {});
      if (st.status) {
        st.status.wifi.autoOffInS = r.autoOffInS;
        st.statusAt = Date.now();
        renderStatus();
      }
    } catch (e) {
      /* the next status poll reports connection problems */
    }
  }

  async function turnWifiOff() {
    if (!confirm('Turn the hairclip’s Wi-Fi off now? This page will disconnect. Hold BOOT for 1.5 s to turn it back on.')) return;
    try {
      const r = await api('/api/wifi/off', {});
      st.wifiOff = true;
      setConn('warn', 'Wi-Fi off');
      banner('Wi-Fi turns off in ' + r.stopInS + ' s. Your message stays on the display. To reconnect later, hold BOOT for 1.5 s and rejoin the network.', 'err');
      refreshPreview();
    } catch (e) {
      toast(e.message, 'err');
    }
  }

  // ---- Start-up -------------------------------------------------------------

  async function load() {
    try {
      const d = await api('/api/settings');
      st.device = d;
      st.display = Object.assign({}, d.display);
      $('message').value = d.message;
      showCurrent(d.message);
      renderControls();
      renderPresets();
      updateEditor();
      setConn('ok', 'Connected');
    } catch (e) {
      setConn('err', 'Not connected');
      banner('Could not load the settings: ' + e.message + ' Pull down to reload.', 'err');
      return;
    }
    bindControls();
    $('message').addEventListener('input', updateEditor);
    $('save').addEventListener('click', () => saveMessage($('message').value));
    $('clear').addEventListener('click', () => {
      $('message').value = '';
      updateEditor();
      $('message').focus();
    });
    $('addPreset').addEventListener('click', addPreset);
    $('wifiOff').addEventListener('click', turnWifiOff);
    $('banner').addEventListener('click', () => keepAlive(true));
    for (const ev of ['pointerdown', 'keydown', 'input']) document.addEventListener(ev, () => keepAlive(false), { passive: true });
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) pollStatus();
    });
    setInterval(pollStatus, POLL_MS);
    setInterval(renderStatus, 1000);
    pollStatus();
  }

  load();
})();
