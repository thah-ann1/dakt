#pragma once

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang="vi"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>GlassHome · Nhà Kính Thông Minh</title>
<style>
:root{
  --bg:#0a0e1a; --bg2:#131829; --card:#1a2036; --card2:#232b45;
  --border:#2a3350; --text:#e8ecf5; --muted:#8b96b4;
  --green:#10b981; --green-glow:rgba(16,185,129,.45);
  --red:#ef4444;   --red-glow:rgba(239,68,68,.45);
  --orange:#f59e0b; --orange-glow:rgba(245,158,11,.45);
  --blue:#3b82f6;  --blue-glow:rgba(59,130,246,.45);
  --purple:#a855f7;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
body{
  margin:0; padding:16px 16px 40px;
  background:radial-gradient(1200px 600px at 50% -10%, #1e2a4a 0%, var(--bg) 55%);
  color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;
  min-height:100vh; font-size:14px; line-height:1.5;
}
.container{max-width:960px;margin:0 auto;}

.header{display:flex;align-items:center;gap:12px;margin-bottom:20px;padding:4px 0;}
.logo{
  width:48px;height:48px;border-radius:14px;flex-shrink:0;
  background:linear-gradient(135deg,#10b981 0%,#3b82f6 100%);
  display:flex;align-items:center;justify-content:center;
  box-shadow:0 8px 24px rgba(16,185,129,.3);
  font-size:24px;
}
.title-block{flex:1;min-width:0;}
h1{font-size:20px;font-weight:800;margin:0;letter-spacing:-.3px;}
.subtitle{color:var(--muted);font-size:12px;margin-top:2px;display:flex;align-items:center;gap:8px;flex-wrap:wrap;}
.pill{
  display:inline-flex;align-items:center;gap:5px;
  background:rgba(16,185,129,.12);color:var(--green);
  border:1px solid rgba(16,185,129,.25);
  padding:2px 8px;border-radius:99px;font-size:11px;font-weight:600;
}
.pill .dot{width:6px;height:6px;border-radius:50%;background:currentColor;
  box-shadow:0 0 6px currentColor;animation:pulse 2s infinite;}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}

.mode-row{
  display:flex;gap:8px;padding:6px;
  background:var(--card);border:1px solid var(--border);
  border-radius:14px;margin-bottom:20px;
}
.mode-btn{
  flex:1;padding:11px 16px;border:none;border-radius:10px;
  font-family:inherit;font-weight:600;font-size:13px;cursor:pointer;
  background:transparent;color:var(--muted);transition:all .2s;
  display:flex;align-items:center;justify-content:center;gap:6px;
}
.mode-btn:hover{color:var(--text);}
.mode-btn.active-auto{
  background:linear-gradient(135deg,#10b981,#059669);
  color:#fff;box-shadow:0 4px 16px var(--green-glow);
}
.mode-btn.active-manual{
  background:linear-gradient(135deg,#f59e0b,#d97706);
  color:#fff;box-shadow:0 4px 16px var(--orange-glow);
}

.sensors{
  display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));
  gap:12px;margin-bottom:24px;
}
.sensor{
  position:relative;overflow:hidden;
  background:var(--card);border:1px solid var(--border);
  border-radius:16px;padding:14px 16px;
  transition:border-color .25s;
}
.sensor::before{
  content:'';position:absolute;top:0;left:0;right:0;height:3px;
  background:var(--accent,#3b82f6);opacity:.8;
}
.sensor:hover{border-color:var(--accent,#3b82f6);}
.sensor-head{display:flex;align-items:center;gap:8px;margin-bottom:8px;}
.sensor-icon{
  width:32px;height:32px;border-radius:10px;flex-shrink:0;
  display:flex;align-items:center;justify-content:center;
  font-size:16px;
  background:rgba(59,130,246,.15);
  border:1px solid rgba(59,130,246,.3);
}
.sensor-label{color:var(--muted);font-size:11px;font-weight:600;letter-spacing:.3px;text-transform:uppercase;}
.sensor-value{font-size:24px;font-weight:800;letter-spacing:-.5px;line-height:1.1;}
.sensor-value .unit{font-size:13px;font-weight:600;color:var(--muted);margin-left:3px;}

.sensor.temp    {--accent:#f97316;}
.sensor.hum     {--accent:#3b82f6;}
.sensor.co2     {--accent:#a855f7;}
.sensor.light-on {--accent:#eab308;}
.sensor.light-off{--accent:#6366f1;}

.section-title{
  display:flex;align-items:center;gap:8px;
  font-size:12px;font-weight:700;color:var(--muted);
  text-transform:uppercase;letter-spacing:1px;
  margin:24px 0 12px;padding-left:4px;
}
.section-title::after{
  content:'';flex:1;height:1px;
  background:linear-gradient(90deg,var(--border),transparent);
}

.device{
  display:flex;align-items:center;gap:12px;
  background:var(--card);border:1px solid var(--border);
  border-radius:14px;padding:12px 14px;margin-bottom:10px;
  transition:border-color .25s, background .25s;
}
.device:hover{border-color:var(--card2);background:var(--card2);}

.device-info{flex:1;min-width:0;}
.device-name{font-weight:600;font-size:14px;display:flex;align-items:center;gap:8px;}
.status-dot{
  width:9px;height:9px;border-radius:50%;flex-shrink:0;
  background:#475569;transition:all .3s;
}
.status-dot.on   {background:var(--green); box-shadow:0 0 10px var(--green-glow);}
.status-dot.off  {background:#334155;}
.status-dot.busy {background:var(--orange);box-shadow:0 0 10px var(--orange-glow);
  animation:pulse 1.2s infinite;}
.device-sub{color:var(--muted);font-size:11px;margin-top:2px;}

.controls{display:flex;gap:6px;flex-shrink:0;}

button.act{
  border:none;border-radius:10px;padding:9px 14px;
  font-family:inherit;font-weight:600;font-size:12px;cursor:pointer;
  background:#2a3350;color:#cbd5e1;transition:all .18s;
  min-width:60px;letter-spacing:.2px;
}
button.act:hover{background:#344066;color:#fff;transform:translateY(-1px);}
button.act:active{transform:translateY(0) scale(.97);}
button.act.act-on   {background:linear-gradient(135deg,#10b981,#059669);color:#fff;
  box-shadow:0 3px 12px var(--green-glow);}
button.act.act-off  {background:linear-gradient(135deg,#ef4444,#dc2626);color:#fff;
  box-shadow:0 3px 12px var(--red-glow);}
button.act.act-stop {background:linear-gradient(135deg,#f59e0b,#d97706);color:#fff;
  box-shadow:0 3px 12px var(--orange-glow);}
button.act:disabled{opacity:.4;cursor:not-allowed;transform:none;}

.footer{
  margin-top:32px;padding-top:20px;
  border-top:1px solid var(--border);
  color:var(--muted);font-size:11px;text-align:center;
  line-height:1.7;
}
.footer .brand{color:var(--green);font-weight:700;}

@media(max-width:480px){
  body{padding:12px 12px 32px;}
  h1{font-size:18px;}
  .sensors{grid-template-columns:repeat(2,1fr);gap:10px;}
  .sensor{padding:12px;}
  .sensor-value{font-size:20px;}
  .sensor-icon{width:28px;height:28px;font-size:14px;}
  button.act{padding:8px 10px;font-size:11px;min-width:52px;}
  .device{padding:10px 12px;gap:8px;}
}
</style></head><body>
<div class="container">

  <div class="header">
    <div class="logo">🌱</div>
    <div class="title-block">
      <h1>GlassHome</h1>
      <div class="subtitle">
        <span class="pill"><span class="dot"></span><span id="uptime">--</span></span>
        <span>· <span id="clients">0</span> thiết bị kết nối</span>
      </div>
    </div>
  </div>

  <div class="mode-row">
    <button class="mode-btn" id="btn-auto"   onclick="ctrl('mode','AUTO')">⚡ TỰ ĐỘNG</button>
    <button class="mode-btn" id="btn-manual" onclick="ctrl('mode','MANUAL')">✋ THỦ CÔNG</button>
  </div>

  <div class="sensors">
    <div class="sensor temp">
      <div class="sensor-head">
        <div class="sensor-icon">🌡️</div>
        <div class="sensor-label">Nhiệt độ</div>
      </div>
      <div class="sensor-value" id="vT">--<span class="unit">°C</span></div>
    </div>

    <div class="sensor hum">
      <div class="sensor-head">
        <div class="sensor-icon">💧</div>
        <div class="sensor-label">Độ ẩm</div>
      </div>
      <div class="sensor-value" id="vH">--<span class="unit">%</span></div>
    </div>

    <div class="sensor co2">
      <div class="sensor-head">
        <div class="sensor-icon">🫧</div>
        <div class="sensor-label">CO₂</div>
      </div>
      <div class="sensor-value" id="vC">--<span class="unit">ppm</span></div>
    </div>

    <div class="sensor light-off" id="lightCard">
      <div class="sensor-head">
        <div class="sensor-icon" id="lightIcon">🌙</div>
        <div class="sensor-label">Ánh sáng</div>
      </div>
      <div class="sensor-value" id="vL">--</div>
    </div>
  </div>

  <div class="section-title">💦 Tưới & Nhiệt độ</div>

  <div class="device">
    <div class="device-info">
      <div class="device-name">
        <span class="status-dot" id="dot-pump"></span>
        Máy bơm
      </div>
      <div class="device-sub" id="sub-pump">Đang chờ...</div>
    </div>
    <div class="controls">
      <button class="act" id="pump-on"  onclick="ctrl('pump','ON')">BẬT</button>
      <button class="act" id="pump-off" onclick="ctrl('pump','OFF')">TẮT</button>
    </div>
  </div>

  <div class="device">
    <div class="device-info">
      <div class="device-name">
        <span class="status-dot" id="dot-heater"></span>
        Đèn sưởi
      </div>
      <div class="device-sub" id="sub-heater">Đang chờ...</div>
    </div>
    <div class="controls">
      <button class="act" id="heater-on"  onclick="ctrl('heater','ON')">BẬT</button>
      <button class="act" id="heater-off" onclick="ctrl('heater','OFF')">TẮT</button>
    </div>
  </div>

  <div class="section-title">💨 Thông gió</div>

  <div class="device">
    <div class="device-info">
      <div class="device-name">
        <span class="status-dot" id="dot-fan1"></span>
        Quạt hút ẩm
      </div>
      <div class="device-sub" id="sub-fan1">Đang chờ...</div>
    </div>
    <div class="controls">
      <button class="act" id="fan1-on"  onclick="ctrl('fan_1','ON')">BẬT</button>
      <button class="act" id="fan1-off" onclick="ctrl('fan_1','OFF')">TẮT</button>
    </div>
  </div>

  <div class="device">
    <div class="device-info">
      <div class="device-name">
        <span class="status-dot" id="dot-fan2"></span>
        Quạt CO₂
      </div>
      <div class="device-sub" id="sub-fan2">Đang chờ...</div>
    </div>
    <div class="controls">
      <button class="act" id="fan2-fwd"  onclick="ctrl('fan_2','FWD')">VÀO</button>
      <button class="act" id="fan2-rev"  onclick="ctrl('fan_2','REV')">RA</button>
      <button class="act" id="fan2-stop" onclick="ctrl('fan_2','STOP')">DỪNG</button>
    </div>
  </div>

  <div class="section-title">🏠 Mái che</div>

  <div class="device">
    <div class="device-info">
      <div class="device-name">
        <span class="status-dot" id="dot-roof"></span>
        Động cơ mái
      </div>
      <div class="device-sub" id="sub-roof">Đang chờ...</div>
    </div>
    <div class="controls">
      <button class="act" id="roof-fwd"  onclick="ctrl('roof','FWD')">MỞ</button>
      <button class="act" id="roof-rev"  onclick="ctrl('roof','REV')">ĐÓNG</button>
      <button class="act" id="roof-stop" onclick="ctrl('roof','STOP')">DỪNG</button>
    </div>
  </div>

  <div class="footer">
    <span class="brand">GlassHome</span> · Nhà kính thông minh ESP32<br>
    Kết nối WiFi <b>GlassHome_ESP32</b> · Truy cập <b>192.168.4.1</b>
  </div>

</div>

<script>
// ===== FIX #6: debounce riêng cho từng device =====
const ctrlTimers = {};
const ctrlLatest = {};

// ===== apiFetch có timeout =====
async function apiFetch(url, opts={}, timeoutMs=3000) {
  const ac  = new AbortController();
  const tid = setTimeout(()=>ac.abort(), timeoutMs);
  try {
    const r = await fetch(url, {...opts, signal: ac.signal});
    clearTimeout(tid);
    return r;
  } catch(e) {
    clearTimeout(tid);
    throw e;
  }
}

// ===== FIX #6 + #7: debounce + retry =====
function ctrl(device, action){
  ctrlLatest[device] = action;
  if (ctrlTimers[device]) clearTimeout(ctrlTimers[device]);

  ctrlTimers[device] = setTimeout(async () => {
    const act = ctrlLatest[device];
    delete ctrlLatest[device];
    delete ctrlTimers[device];

    // Retry tối đa 3 lần nếu server trả 503
    for (let retry = 0; retry < 3; retry++) {
      try {
        const r = await apiFetch('/api/control', {
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body: JSON.stringify({device, action: act})
        }, 3000);
        if (r.ok) break;              // thành công → thoát
      } catch(e) { /* retry */ }
      await new Promise(r => setTimeout(r, 150));
    }
    pollLatest();
  }, 80);   // chờ 80ms để gộp click nhanh
}

function setText(id, val){ const e=document.getElementById(id); if(e) e.textContent=val; }
function setClass(id, ...cls){
  const e=document.getElementById(id); if(!e) return;
  e.classList.remove('on','off','busy','act-on','act-off','act-stop','active-auto','active-manual');
  cls.forEach(c=>c && e.classList.add(c));
}

function updateUI(d){
  setClass('btn-auto',   d.mode==='AUTO'   ? 'active-auto'   : null);
  setClass('btn-manual', d.mode==='MANUAL' ? 'active-manual' : null);

  document.getElementById('vT').innerHTML =
    parseFloat(d.temperature).toFixed(1) + '<span class="unit">°C</span>';
  document.getElementById('vH').innerHTML =
    parseFloat(d.humidity).toFixed(1) + '<span class="unit">%</span>';
  document.getElementById('vC').innerHTML =
    Math.trunc(d.co2) + '<span class="unit">ppm</span>';

  const isBright = d.light==='SANG';
  document.getElementById('lightCard').className = 'sensor ' + (isBright?'light-on':'light-off');
  document.getElementById('lightIcon').textContent = isBright ? '☀️' : '🌙';
  document.getElementById('vL').textContent = isBright ? 'Sáng' : 'Tối';

  const up = d.uptime|0;
  const h = Math.floor(up/3600), m = Math.floor((up%3600)/60), s = up%60;
  setText('uptime', h>0 ? `${h}h ${m}m` : (m>0 ? `${m}m ${s}s` : `${s}s`));
  setText('clients', d.clients);

  updateDevice('pump',   d.pump,   ['ON','OFF'],           ['Đang bơm','Đã tắt']);
  updateDevice('heater', d.heater, ['ON','OFF'],           ['Đang sưởi','Đã tắt']);
  updateDevice('fan1',   d.fan_1,  ['ON','OFF'],           ['Đang hút ẩm','Đã tắt']);
  updateDevice('fan2',   d.fan_2,  ['VAO','RA','DUNG'],    ['Hút vào','Đẩy ra','Đã dừng']);
  updateDevice('roof',   d.roof,   ['MO','DONG','DA MO','DA DONG','DUNG'],
                                   ['Đang mở','Đang đóng','Đã mở','Đã đóng','Đã dừng']);
}

function updateDevice(key, state, states, labels){
  const idx = states.indexOf(state);
  const isOn  = (state==='ON' || state==='VAO' || state==='MO' || state==='DONG');
  const isOff = (state==='OFF'|| state==='DUNG'|| state==='DA MO'|| state==='DA DONG');

  setClass('dot-'+key, isOn ? 'on' : (isOff ? 'off' : 'busy'));
  if(state==='MO'||state==='DONG') setClass('dot-'+key, 'busy');

  setText('sub-'+key, idx>=0 ? labels[idx] : state);

  if(key==='pump' || key==='heater' || key==='fan1'){
    setClass(key+'-on',  state==='ON'  ? 'act-on'  : null);
    setClass(key+'-off', state==='OFF' ? 'act-off' : null);
  }
  else if(key==='fan2'){
    setClass('fan2-fwd',  state==='VAO'  ? 'act-on'  : null);
    setClass('fan2-rev',  state==='RA'   ? 'act-off' : null);
    setClass('fan2-stop', state==='DUNG' ? 'act-stop': null);
  }
  else if(key==='roof'){
    setClass('roof-fwd',  state==='MO'   ? 'act-on'  : null);
    setClass('roof-rev',  state==='DONG' ? 'act-off' : null);
    setClass('roof-stop', (state==='DUNG'||state==='DA MO'||state==='DA DONG') ? 'act-stop' : null);
  }
}

// ===== Polling có guard =====
let pollBusy = false;
async function pollLatest(){
  if (pollBusy) return;
  pollBusy = true;
  try {
    const r = await apiFetch('/api/latest', {}, 2500);
    if (r.ok) updateUI(await r.json());
  } catch(e) {}
  finally { pollBusy = false; }
}

pollLatest();
setInterval(pollLatest, 1500);
</script></body></html>
)HTMLPAGE";