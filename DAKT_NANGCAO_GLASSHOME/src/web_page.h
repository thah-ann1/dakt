#pragma once

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html><html lang="vi"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>GlassHome · Nhà Kính Thông Minh</title>
<style>
:root{
  --bg:#0a0e1a; --card:#1a2036; --card2:#232b45; --border:#2a3350;
  --text:#e8ecf5; --muted:#8b96b4;
  --green:#10b981; --green-glow:rgba(16,185,129,.45);
  --red:#ef4444; --red-glow:rgba(239,68,68,.45);
  --orange:#f59e0b; --orange-glow:rgba(245,158,11,.45);
  --blue:#3b82f6; --purple:#a855f7;
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
body{margin:0;padding:16px;background:radial-gradient(1200px 600px at 50% -10%, #1e2a4a 0%, var(--bg) 55%);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Arial,sans-serif;min-height:100vh;}
.container{max-width:960px;margin:0 auto;}

.header{display:flex;align-items:center;gap:12px;margin-bottom:20px;}
.logo{width:48px;height:48px;border-radius:14px;background:linear-gradient(135deg,#10b981,#3b82f6);display:flex;align-items:center;justify-content:center;font-size:24px;}
.title-block h1{font-size:20px;font-weight:800;margin:0;}
.subtitle{color:var(--muted);font-size:12px;display:flex;gap:8px;margin-top:2px;}
.pill{display:inline-flex;align-items:center;gap:5px;background:rgba(16,185,129,.12);color:var(--green);padding:2px 8px;border-radius:99px;font-size:11px;font-weight:600;}
.pill .dot{width:6px;height:6px;border-radius:50%;background:currentColor;animation:pulse 2s infinite;}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}

.mode-row{display:flex;gap:8px;padding:6px;background:var(--card);border:1px solid var(--border);border-radius:14px;margin-bottom:20px;}
.mode-btn{flex:1;padding:11px;border:none;border-radius:10px;font-weight:600;font-size:13px;cursor:pointer;background:transparent;color:var(--muted);}
.mode-btn.active-auto{background:linear-gradient(135deg,#10b981,#059669);color:#fff;}
.mode-btn.active-manual{background:linear-gradient(135deg,#f59e0b,#d97706);color:#fff;}

.sensors{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:24px;}
.sensor{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:14px;position:relative;overflow:hidden;}
.sensor::before{content:'';position:absolute;top:0;left:0;right:0;height:3px;background:var(--accent,#3b82f6);}
.sensor-head{display:flex;align-items:center;gap:8px;margin-bottom:8px;}
.sensor-icon{width:32px;height:32px;border-radius:10px;display:flex;align-items:center;justify-content:center;font-size:16px;background:rgba(59,130,246,.15);}
.sensor-label{color:var(--muted);font-size:11px;font-weight:600;text-transform:uppercase;}
.sensor-value{font-size:20px;font-weight:800;}
.sensor.temp{--accent:#f97316;} .sensor.co2{--accent:#a855f7;} .sensor.hum{--accent:#06b6d4;}
.range{color:var(--muted);font-size:10px;margin-top:4px;}
.sensor-value.warn{color:var(--orange);}
.sensor.soil{--accent:#10b981;} .sensor.water{--accent:#3b82f6;} .sensor.light{--accent:#eab308;}

.section-title{font-size:12px;font-weight:700;color:var(--muted);text-transform:uppercase;margin:24px 0 12px;}
.device{display:flex;align-items:center;gap:12px;background:var(--card);border:1px solid var(--border);border-radius:14px;padding:12px 14px;margin-bottom:10px;}
.device-info{flex:1;}
.device-name{font-weight:600;display:flex;align-items:center;gap:8px;}
.status-dot{width:9px;height:9px;border-radius:50%;background:#475569;}
.status-dot.on{background:var(--green);} .status-dot.off{background:#334155;} .status-dot.busy{background:var(--orange);}
.device-sub{color:var(--muted);font-size:11px;margin-top:2px;}
.controls{display:flex;gap:6px;}
button.act{border:none;border-radius:10px;padding:9px 14px;font-weight:600;font-size:12px;cursor:pointer;background:#2a3350;color:#cbd5e1;min-width:60px;}
button.act.act-on{background:linear-gradient(135deg,#10b981,#059669);color:#fff;}
button.act.act-off{background:linear-gradient(135deg,#ef4444,#dc2626);color:#fff;}
button.act.act-stop{background:linear-gradient(135deg,#f59e0b,#d97706);color:#fff;}
button.act:disabled{opacity:.4;cursor:not-allowed;}
</style></head><body>
<div class="container">
  <div class="header">
    <div class="logo">🌱</div>
    <div class="title-block">
      <h1>GlassHome</h1>
      <div class="subtitle"><span class="pill"><span class="dot"></span><span id="uptime">--</span></span> · <span id="clients">0</span> thiết bị</div>
    </div>
  </div>

  <div class="mode-row">
    <button class="mode-btn" id="btn-auto"   onclick="ctrl('mode','AUTO')">⚡ TỰ ĐỘNG</button>
    <button class="mode-btn" id="btn-manual" onclick="ctrl('mode','MANUAL')">✋ THỦ CÔNG</button>
  </div>

  <div class="sensors">
    <div class="sensor temp">
      <div class="sensor-head"><div class="sensor-icon">🌡️</div><div class="sensor-label">Nhiệt độ</div></div>
      <div class="sensor-value" id="vT">--<span style="font-size:12px">°C</span></div>
      <div class="range">Tối ưu 18-23°C</div>
    </div>
    <div class="sensor hum">
      <div class="sensor-head"><div class="sensor-icon">💦</div><div class="sensor-label">Ẩm không khí</div></div>
      <div class="sensor-value" id="vH">--<span style="font-size:12px">%</span></div>
      <div class="range">Tối ưu 65-80%</div>
    </div>
    <div class="sensor soil">
      <div class="sensor-head"><div class="sensor-icon">🪴</div><div class="sensor-label">Độ ẩm đất</div></div>
      <div class="sensor-value" id="vSoil">--<span style="font-size:12px">%</span></div>
      <div class="range">Tối ưu 70-85%</div>
    </div>
    <div class="sensor water">
      <div class="sensor-head"><div class="sensor-icon">💧</div><div class="sensor-label">Mực nước</div></div>
      <div class="sensor-value" id="vTank">--</div>
    </div>
    <div class="sensor co2">
      <div class="sensor-head"><div class="sensor-icon">🫧</div><div class="sensor-label">CO₂</div></div>
      <div class="sensor-value" id="vC">--<span style="font-size:12px">ppm</span></div>
      <div class="range">Tối ưu 700-1000</div>
    </div>
    <div class="sensor light">
      <div class="sensor-head"><div class="sensor-icon">☀️</div><div class="sensor-label">Ánh sáng</div></div>
      <div class="sensor-value" id="vL">--</div>
    </div>
  </div>

  <div class="section-title">💦 Tưới & Nhiệt độ</div>
  <div class="device">
    <div class="device-info"><div class="device-name"><span class="status-dot" id="dot-pump"></span>Máy bơm</div><div class="device-sub" id="sub-pump">Chờ...</div></div>
    <div class="controls">
      <button class="act" id="pump-on"  onclick="ctrl('pump','ON')">BẬT</button>
      <button class="act" id="pump-off" onclick="ctrl('pump','OFF')">TẮT</button>
    </div>
  </div>
  <div class="device">
    <div class="device-info"><div class="device-name"><span class="status-dot" id="dot-heater"></span>Đèn sưởi</div><div class="device-sub" id="sub-heater">Chờ...</div></div>
    <div class="controls">
      <button class="act" id="heater-on"  onclick="ctrl('heater','ON')">BẬT</button>
      <button class="act" id="heater-off" onclick="ctrl('heater','OFF')">TẮT</button>
    </div>
  </div>

  <div class="section-title">💨 Thông gió</div>
  <div class="device">
    <div class="device-info"><div class="device-name"><span class="status-dot" id="dot-fan1"></span>Quạt hút ẩm</div><div class="device-sub" id="sub-fan1">Chờ...</div></div>
    <div class="controls">
      <button class="act" id="fan1-on"  onclick="ctrl('fan_1','ON')">BẬT</button>
      <button class="act" id="fan1-off" onclick="ctrl('fan_1','OFF')">TẮT</button>
    </div>
  </div>
  <div class="device">
    <div class="device-info"><div class="device-name"><span class="status-dot" id="dot-fan2"></span>Quạt CO₂</div><div class="device-sub" id="sub-fan2">Chờ...</div></div>
    <div class="controls">
      <button class="act" id="fan2-fwd"  onclick="ctrl('fan_2','FWD')">VÀO</button>
      <button class="act" id="fan2-rev"  onclick="ctrl('fan_2','REV')">RA</button>
      <button class="act" id="fan2-stop" onclick="ctrl('fan_2','STOP')">DỪNG</button>
    </div>
  </div>

  <div class="section-title">🏠 Mái che</div>
  <div class="device">
    <div class="device-info"><div class="device-name"><span class="status-dot" id="dot-roof"></span>Động cơ mái</div><div class="device-sub" id="sub-roof">Chờ...</div></div>
    <div class="controls">
      <button class="act" id="roof-fwd"  onclick="ctrl('roof','FWD')">MỞ</button>
      <button class="act" id="roof-rev"  onclick="ctrl('roof','REV')">ĐÓNG</button>
      <button class="act" id="roof-stop" onclick="ctrl('roof','STOP')">DỪNG</button>
    </div>
  </div>

</div>

<script>
const ctrlTimers = {}, ctrlLatest = {};
async function apiFetch(url, opts={}, timeoutMs=3000) {
  const ac = new AbortController(); const tid = setTimeout(()=>ac.abort(), timeoutMs);
  try { const r = await fetch(url, {...opts, signal: ac.signal}); clearTimeout(tid); return r; }
  catch(e) { clearTimeout(tid); throw e; }
}
function ctrl(device, action){
  ctrlLatest[device] = action;
  if (ctrlTimers[device]) clearTimeout(ctrlTimers[device]);
  ctrlTimers[device] = setTimeout(async () => {
    const act = ctrlLatest[device];
    delete ctrlLatest[device]; delete ctrlTimers[device];
    for (let retry = 0; retry < 3; retry++) {
      try {
        const r = await apiFetch('/api/control', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({device, action: act}) }, 3000);
        if (r.ok) break;
      } catch(e) {}
      await new Promise(r => setTimeout(r, 150));
    }
    pollLatest();
  }, 80);
}
function setText(id, val){ const e=document.getElementById(id); if(e) e.textContent=val; }
function setClass(id, ...cls){
  const e=document.getElementById(id); if(!e) return;
  e.classList.remove('on','off','busy','act-on','act-off','act-stop','active-auto','active-manual');
  cls.forEach(c=>c && e.classList.add(c));
}
function updateUI(d){
  setClass('btn-auto', d.mode==='AUTO' ? 'active-auto' : null);
  setClass('btn-manual', d.mode==='MANUAL' ? 'active-manual' : null);

  document.getElementById('vT').innerHTML = parseFloat(d.temperature).toFixed(1) + '<span style="font-size:12px">°C</span>';
  document.getElementById('vC').innerHTML = Math.trunc(d.co2) + '<span style="font-size:12px">ppm</span>';
  document.getElementById('vSoil').innerHTML = Math.trunc(d.soil_pct) + '<span style="font-size:12px">%</span>';
  document.getElementById('vH').innerHTML = Math.trunc(d.humidity) + '<span style="font-size:12px">%</span>';

  // Tô màu cam khi giá trị lệch khỏi khoảng tối ưu của rau cải
  const warn = (id, v, lo, hi) => { const e=document.getElementById(id); if(e) e.classList.toggle('warn', v<lo || v>hi); };
  warn('vT',   parseFloat(d.temperature), 18, 23);
  warn('vH',   d.humidity,  65, 80);
  warn('vSoil',d.soil_pct,  70, 85);
  warn('vC',   d.co2,       700, 1000);

  setText('vTank', d.tank === 'CON' ? 'Còn nước' : 'HẾT NƯỚC!');
  
  const isBright = d.light==='SANG';
  setText('vL', isBright ? 'Sáng' : 'Tối');

  const up = d.uptime|0; const h = Math.floor(up/3600), m = Math.floor((up%3600)/60), s = up%60;
  setText('uptime', h>0 ? `${h}h ${m}m` : (m>0 ? `${m}m ${s}s` : `${s}s`));
  setText('clients', d.clients);

  updateDevice('pump', d.pump, ['ON','OFF'], ['Đang bơm','Đã tắt']);
  updateDevice('heater', d.heater, ['ON','OFF'], ['Đang sưởi','Đã tắt']);
  updateDevice('fan1', d.fan_1, ['ON','OFF'], ['Đang chạy (ẩm/nóng)','Đã tắt']);
  updateDevice('fan2', d.fan_2, ['VAO','RA','DUNG'], ['Hút vào','Đẩy ra','Đã dừng']);
  updateDevice('roof', d.roof, ['MO','DONG','DA MO','DA DONG','DUNG'], ['Đang mở','Đang đóng','Đã mở','Đã đóng','Đã dừng']);

  // KHÓA NÚT BẬT BƠM NẾU HẾT NƯỚC
  const pOn = document.getElementById('pump-on');
  if(pOn) { pOn.disabled = (d.tank === 'HET'); pOn.title = (d.tank === 'HET') ? 'Hết nước - Không thể bơm' : ''; }
}
function updateDevice(key, state, states, labels){
  const idx = states.indexOf(state);
  const isOn = (state==='ON' || state==='VAO' || state==='MO' || state==='DONG');
  const isOff = (state==='OFF'|| state==='DUNG'|| state==='DA MO'|| state==='DA DONG');
  setClass('dot-'+key, isOn ? 'on' : (isOff ? 'off' : 'busy'));
  if(state==='MO'||state==='DONG') setClass('dot-'+key, 'busy');
  setText('sub-'+key, idx>=0 ? labels[idx] : state);

  if(key==='pump' || key==='heater' || key==='fan1'){
    setClass(key+'-on', state==='ON' ? 'act-on' : null);
    setClass(key+'-off', state==='OFF' ? 'act-off' : null);
  } else if(key==='fan2'){
    setClass('fan2-fwd', state==='VAO' ? 'act-on' : null);
    setClass('fan2-rev', state==='RA' ? 'act-off' : null);
    setClass('fan2-stop', state==='DUNG' ? 'act-stop' : null);
  } else if(key==='roof'){
    setClass('roof-fwd', state==='MO' ? 'act-on' : null);
    setClass('roof-rev', state==='DONG' ? 'act-off' : null);
    setClass('roof-stop', (state==='DUNG'||state==='DA MO'||state==='DA DONG') ? 'act-stop' : null);
  }
}
let pollBusy = false;
async function pollLatest(){
  if (pollBusy) return; pollBusy = true;
  try { const r = await apiFetch('/api/latest', {}, 2500); if (r.ok) updateUI(await r.json()); } catch(e) {}
  finally { pollBusy = false; }
}
pollLatest(); setInterval(pollLatest, 1500);
</script></body></html>
)HTMLPAGE";
