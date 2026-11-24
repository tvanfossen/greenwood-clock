/**
 * @fileoverview Web control panel script (served from /www): display state, surprise, weather, schedule, settings, assets and device info via the local API.
 * @brief Web control panel logic for the Greenwood Clock.
 * v2: colours from the device theme (GET /api/theme -> CSS variables); theme picker replaces
 *     the text colour; surprise colours are theme roles.
 * @version 2
 */
// Greenwood Clock — Control Page
// Vanilla JS, no dependencies

const API = '';  // same origin

// ---- Utility ----

/**
 * Show @p msg in a toast element (created on first use) for 2.5 s.
 * @version 1
 * @utility
 */
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

/**
 * Fetch an API path on the same origin; throws (after logging) on network errors or a non-OK status, using the body text as the message.
 * @version 1
 * @utility
 * @return Promise resolving to the parsed JSON response.
 */
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

// ---- Theme ----

/**
 * Set the page's CSS colour variables from the device theme (GET /api/theme): --<role> for
 * each role and --on-<role> for text on it, role names with '_' as '-'.
 * @version 1
 * @req REQ-UI-002
 * @req REQ-UI-003
 */
async function applyTheme() {
  try {
    const t = await api('/api/theme');
    const root = document.documentElement.style;
    for (const [role, hex] of Object.entries(t.roles || {})) {
      root.setProperty('--' + role.replace(/_/g, '-'), hex);
    }
    for (const [role, hex] of Object.entries(t.on || {})) {
      root.setProperty('--on-' + role.replace(/_/g, '-'), hex);
    }
    root.setProperty('color-scheme', t.dark ? 'dark' : 'light');
  } catch {
    // keep the last applied theme
  }
}

/**
 * Fill the theme dropdown from GET /api/themes, selecting @p current.
 * @version 1
 * @req REQ-UI-002
 * @req REQ-UI-003
 */
async function populateThemeSelect(current) {
  const sel = document.getElementById('set-theme');
  try {
    const themes = await api('/api/themes');
    sel.innerHTML = themes.map(t =>
      '<option value="' + t.stem + '"' + (t.stem === current ? ' selected' : '') + '>' +
      t.name + '</option>').join('');
  } catch {
    sel.innerHTML = '<option value="' + current + '" selected>' + current + '</option>';
  }
}

// ---- Display Control ----

/**
 * Show the current display FSM state from GET /api/display/state and set the connection indicator by whether it answered.
 * @version 1
 * @req REQ-UI-002
 */
async function refreshState() {
  try {
    const data = await api('/api/display/state');
    document.getElementById('current-state').textContent = data.state || '---';
    document.getElementById('connection-status').classList.add('connected');
  } catch {
    document.getElementById('connection-status').classList.remove('connected');
  }
}

/**
 * POST a display state to /api/display/state, toast the result and re-read the state after 500 ms.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Build a one-label surprise layout from the message, colour role and duration inputs and POST it to /api/display/surprise.
 * v2: the colour is a theme role name; the background is the raised surface.
 * @version 2
 * @req REQ-UI-002
 */
async function sendSurprise() {
  const text = document.getElementById('surprise-text').value.trim();
  if (!text) { toast('Enter a message'); return; }

  const color = document.getElementById('surprise-color').value;
  const duration = parseInt(document.getElementById('surprise-duration').value);

  const layout = {
    bg_color: 'surface_raised',
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

/**
 * Convert Celsius to Fahrenheit.
 * @version 1
 * @utility
 * @return Fahrenheit rounded to a whole-number string.
 */
function cToF(c) { return (c * 9/5 + 32).toFixed(0); }
/**
 * Convert km/h to mph.
 * @version 1
 * @utility
 * @return mph rounded to a whole-number string.
 */
function kmhToMph(k) { return (k * 0.621371).toFixed(0); }

/**
 * Render current conditions from GET /api/weather/current (temp, description, feels-like, wind, humidity in imperial units).
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Build one forecast tile for period i, pairing a day period with the night after it.
 * @version 1
 * @req REQ-UI-002
 * @return Tile HTML, or '' for a standalone night period that is not first.
 */
function forecastDayHtml(periods, i) {
  const p = periods[i];
  if (!p.daytime && i > 0) return '';  // skip standalone night periods
  const night = periods[i + 1];
  const hi = p.daytime ? p.temp : (night ? night.temp : '');
  const lo = night && !night.daytime ? night.temp : '';
  return '<div class="forecast-day">' +
    '<div class="day-name">' + p.name.substring(0, 3) + '</div>' +
    '<div>' + p.short + '</div>' +
    '<div class="temps">' + hi + '/' + (lo || '--') + '</div>' +
  '</div>';
}

/**
 * Render the forecast from GET /api/weather/forecast as one tile per day with high/low temperatures.
 * @version 2
 * @req REQ-UI-002
 */
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
      html += forecastDayHtml(fc.periods, i);
    }
    el.innerHTML = html;
  } catch {
    // silent
  }
}

/**
 * Render active alerts from GET /api/weather/alerts as banners styled by severity.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Build the schedule number inputs (SCHED_FIELDS) filled from GET /api/display/schedule.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * POST the schedule inputs (integers, 0 when empty) to /api/display/schedule.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Fill the background dropdown with "None", every backgrounds asset, and the current value if it is not an asset.
 * @version 1
 * @req REQ-UI-002
 */
async function populateBackgroundSelect(sel, bg) {
  sel.innerHTML = '<option value="">None</option>';
  try {
    const assets = await api('/api/assets/list');
    const backgrounds = (assets.assets || []).filter(a => a.category === 'backgrounds');
    for (const a of backgrounds) {
      const path = 'A:/backgrounds/' + a.name;
      sel.innerHTML += '<option value="' + path + '"' +
        (bg === path ? ' selected' : '') + '>' + a.name + '</option>';
    }
  } catch { /* no assets */ }
  if (bg && !sel.querySelector('[value="' + bg + '"]')) {
    sel.innerHTML += '<option value="' + bg + '" selected>' + bg + '</option>';
  }
}

/**
 * Fill brightness, theme and the background dropdown (backgrounds assets plus the current value) from GET /api/settings, /api/themes and /api/assets/list.
 * v3: theme dropdown (was a text colour).
 * @version 3
 * @req REQ-UI-002
 */
async function refreshSettings() {
  try {
    const data = await api('/api/settings');
    document.getElementById('set-brightness').value = data.brightness || 50;
    document.getElementById('brightness-val').textContent = (data.brightness || 50) + '%';
    await populateThemeSelect(data.theme || 'greenwood');

    // Populate background dropdown from assets
    await populateBackgroundSelect(document.getElementById('set-background'),
                                   data.background_image || '');
  } catch {
    // silent
  }
}

document.getElementById('set-brightness').addEventListener('input', function() {
  document.getElementById('brightness-val').textContent = this.value + '%';
});

/**
 * POST brightness, theme and background image to /api/settings, then re-read the theme.
 * v2: theme (was text colour); the page recolours to it.
 * @version 2
 * @req REQ-UI-002
 */
async function saveSettings() {
  const body = {
    brightness: parseInt(document.getElementById('set-brightness').value),
    theme: document.getElementById('set-theme').value,
    background_image: document.getElementById('set-background').value
  };
  try {
    await api('/api/settings', {
      method: 'POST',
      headers: {'Content-Type': 'application/json'},
      body: JSON.stringify(body)
    });
    toast('Settings saved');
    setTimeout(applyTheme, 500);   // the device re-applies the theme on its own task
  } catch (e) {
    toast('Error: ' + e.message);
  }
}

// ---- Asset Manager ----

/**
 * List assets with sizes from GET /api/assets/list.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Upload the chosen file to <category>/<name> via POST /api/assets/upload, then refresh the asset list.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Show uptime (minutes), free heap (KB) and SD card state from GET /api/status.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * After a confirm dialog, POST /debug/reboot and toast the result.
 * @version 1
 * @req REQ-UI-002
 */
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

/**
 * Refresh every panel section once.
 * v2: applies the device theme first.
 * @version 2
 * @req REQ-UI-002
 */
function refreshAll() {
  applyTheme();
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
