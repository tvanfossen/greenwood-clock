// Greenwood Clock — Control Page
// Vanilla JS, no dependencies

const API = '';  // same origin

// ---- Utility ----

function toast(msg) {
  let el = document.querySelector('.toast');
  if (!el) {
    el = document.createElement('div');
    el.className = 'toast';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.classList.add('show');
  setTimeout(() => el.classList.remove('show'), 2500);
}

async function api(path, opts) {
  try {
    const res = await fetch(API + path, opts);
    if (!res.ok) {
      const text = await res.text();
      throw new Error(text || res.statusText);
    }
    return await res.json();
  } catch (e) {
    console.error('API error:', path, e);
    throw e;
  }
}

// ---- Display Control ----

async function refreshState() {
  try {
    const data = await api('/api/display/state');
    document.getElementById('current-state').textContent = data.state || '---';
    document.getElementById('connection-status').classList.add('connected');
  } catch {
    document.getElementById('connection-status').classList.remove('connected');
  }
}

async function forceState(state) {
  try {
    await api('/api/display/state', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({state})
    });
    toast('Switched to ' + state);
    setTimeout(refreshState, 500);
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Surprise Message ----

async function sendSurprise() {
  const text = document.getElementById('surprise-text').value.trim();
  if (!text) { toast('Enter a message'); return; }

  const color = document.getElementById('surprise-color').value;
  const duration = parseInt(document.getElementById('surprise-duration').value);

  const layout = {
    bg_color: '#1a1a2e',
    duration_s: duration,
    children: [
      {
        type: 'label',
        text: text,
        font: 'nunito_128',
        color: color,
        y: 250
      }
    ]
  };

  try {
    await api('/api/display/surprise', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(layout)
    });
    toast('Surprise sent!');
    document.getElementById('surprise-text').value = '';
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Weather ----

function cToF(c) { return (c * 9/5 + 32).toFixed(0); }
function kmhToMph(k) { return (k * 0.621371).toFixed(0); }

async function refreshWeather() {
  try {
    const c = await api('/api/weather/current');
    const el = document.getElementById('weather-data');
    if (!c.valid) {
      el.innerHTML = '<p class="placeholder">No weather data yet</p>';
      return;
    }
    el.innerHTML =
      '<div class="temp">' + cToF(c.temp_c) + '&deg;F</div>' +
      '<div class="desc">' + c.description + '</div>' +
      '<div class="detail">' +
        'Feels like ' + cToF(c.feels_like_c) + '&deg;F &middot; ' +
        'Wind ' + c.wind_dir + ' ' + kmhToMph(c.wind_speed_kmh) + ' mph &middot; ' +
        'Humidity ' + c.humidity + '%' +
      '</div>';
  } catch {
    // silent
  }
}

async function refreshForecast() {
  try {
    const fc = await api('/api/weather/forecast');
    const el = document.getElementById('forecast-data');
    if (!fc.valid || !fc.periods) {
      el.innerHTML = '';
      return;
    }
    // Group into days (pair day + night periods)
    let html = '';
    for (let i = 0; i < fc.periods.length; i++) {
      const p = fc.periods[i];
      if (!p.daytime && i > 0) continue;  // skip standalone night periods
      const night = fc.periods[i+1];
      const hi = p.daytime ? p.temp : (night ? night.temp : '');
      const lo = night && !night.daytime ? night.temp : '';
      html += '<div class="forecast-day">' +
        '<div class="day-name">' + p.name.substring(0, 3) + '</div>' +
        '<div>' + p.short + '</div>' +
        '<div class="temps">' + hi + '/' + (lo || '--') + '</div>' +
      '</div>';
    }
    el.innerHTML = html;
  } catch {
    // silent
  }
}

async function refreshAlerts() {
  try {
    const al = await api('/api/weather/alerts');
    const el = document.getElementById('alerts-data');
    if (!al.valid || al.count === 0) {
      el.innerHTML = '';
      return;
    }
    let html = '';
    for (const a of al.alerts) {
      const cls = a.severity === 'Moderate' ? 'moderate' :
                  a.severity === 'Minor' ? 'minor' : '';
      html += '<div class="alert-banner ' + cls + '">' +
        '<strong>' + a.event + '</strong> &mdash; ' + a.headline +
      '</div>';
    }
    el.innerHTML = html;
  } catch {
    // silent
  }
}

// ---- Schedule Config ----

const SCHED_FIELDS = [
  {key: 'weather_show_s', label: 'Weather Show', unit: 's'},
  {key: 'weather_cooldown_s', label: 'Weather Cooldown', unit: 's'},
  {key: 'radar_show_s', label: 'Radar Show', unit: 's'},
  {key: 'radar_cooldown_s', label: 'Radar Cooldown', unit: 's'},
  {key: 'astro_show_s', label: 'Astro Show', unit: 's'},
  {key: 'astro_cooldown_s', label: 'Astro Cooldown', unit: 's'},
  {key: 'photos_interval_s', label: 'Photos Interval', unit: 's'},
  {key: 'photos_show_s', label: 'Photos Show', unit: 's'},
  {key: 'ambient_interval_s', label: 'Ambient Interval', unit: 's'},
  {key: 'ambient_show_s', label: 'Ambient Show', unit: 's'},
];

async function refreshSchedule() {
  try {
    const data = await api('/api/display/schedule');
    const el = document.getElementById('schedule-fields');
    let html = '';
    for (const f of SCHED_FIELDS) {
      const val = data[f.key] || 0;
      html += '<div class="sched-row">' +
        '<label>' + f.label + '</label>' +
        '<input type="number" id="sched-' + f.key + '" value="' + val + '" min="0" max="65535">' +
        '<span class="unit">' + f.unit + '</span>' +
      '</div>';
    }
    el.innerHTML = html;
  } catch {
    // silent
  }
}

async function saveSchedule() {
  const body = {};
  for (const f of SCHED_FIELDS) {
    const el = document.getElementById('sched-' + f.key);
    if (el) body[f.key] = parseInt(el.value) || 0;
  }
  try {
    await api('/api/display/schedule', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body)
    });
    toast('Schedule saved');
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Settings ----

async function refreshSettings() {
  try {
    const data = await api('/api/settings');
    document.getElementById('set-brightness').value = data.brightness || 50;
    document.getElementById('brightness-val').textContent = (data.brightness || 50) + '%';
    document.getElementById('set-text-color').value = data.text_color || '#ffffff';

    // Populate background dropdown from assets
    const bg = data.background_image || '';
    const sel = document.getElementById('set-background');
    sel.innerHTML = '<option value="">None</option>';
    try {
      const assets = await api('/api/assets/list');
      if (assets.assets) {
        for (const a of assets.assets) {
          if (a.category === 'backgrounds') {
            const path = 'A:/backgrounds/' + a.name;
            sel.innerHTML += '<option value="' + path + '"' +
              (bg === path ? ' selected' : '') + '>' + a.name + '</option>';
          }
        }
      }
    } catch { /* no assets */ }
    if (bg && !sel.querySelector('[value="' + bg + '"]')) {
      sel.innerHTML += '<option value="' + bg + '" selected>' + bg + '</option>';
    }
  } catch {
    // silent
  }
}

document.getElementById('set-brightness').addEventListener('input', function() {
  document.getElementById('brightness-val').textContent = this.value + '%';
});

async function saveSettings() {
  const body = {
    brightness: parseInt(document.getElementById('set-brightness').value),
    text_color: document.getElementById('set-text-color').value,
    background_image: document.getElementById('set-background').value
  };
  try {
    await api('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body)
    });
    toast('Settings saved');
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Asset Manager ----

async function refreshAssets() {
  try {
    const data = await api('/api/assets/list');
    const el = document.getElementById('asset-list');
    if (!data.assets || data.assets.length === 0) {
      el.innerHTML = '<p class="placeholder">No assets found</p>';
      return;
    }
    let html = '';
    for (const a of data.assets) {
      const sz = a.size > 1024 ? (a.size / 1024).toFixed(0) + 'K' : a.size + 'B';
      html += '<div class="asset-item">' +
        '<span>' + a.name + '</span> ' +
        '<span class="size">' + sz + '</span>' +
      '</div>';
    }
    el.innerHTML = html;
  } catch {
    // silent
  }
}

async function uploadAsset() {
  const fileInput = document.getElementById('asset-file');
  const category = document.getElementById('asset-category').value;
  if (!fileInput.files.length) { toast('Select a file'); return; }

  const file = fileInput.files[0];
  const path = category + '/' + file.name;

  try {
    const res = await fetch('/api/assets/upload?path=' + encodeURIComponent(path), {
      method: 'POST',
      body: file
    });
    if (!res.ok) throw new Error(await res.text());
    toast('Uploaded ' + file.name);
    fileInput.value = '';
    refreshAssets();
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Device Info ----

async function refreshDeviceInfo() {
  try {
    const data = await api('/api/status');
    const el = document.getElementById('device-info');
    const uptime = Math.floor(data.uptime / 60);
    el.innerHTML =
      '<div class="detail">' +
        'Uptime: ' + uptime + ' min &middot; ' +
        'Heap: ' + (data.free_heap / 1024).toFixed(0) + ' KB &middot; ' +
        'SD: ' + (data.sd_card ? 'mounted' : 'missing') +
      '</div>';
  } catch {
    // silent
  }
}

async function rebootDevice() {
  if (!confirm('Reboot the clock?')) return;
  try {
    await api('/debug/reboot', {method: 'POST'});
    toast('Rebooting...');
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Init ----

function refreshAll() {
  refreshState();
  refreshWeather();
  refreshForecast();
  refreshAlerts();
  refreshDeviceInfo();
  refreshSchedule();
  refreshSettings();
  refreshAssets();
}

refreshAll();
setInterval(refreshState, 10000);
setInterval(refreshWeather, 60000);
setInterval(refreshForecast, 300000);
setInterval(refreshAlerts, 60000);
setInterval(refreshDeviceInfo, 30000);
