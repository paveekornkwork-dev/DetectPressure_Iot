/* ===========================================================================
 *  Pressure Monitoring System — Dashboard Application
 *  Firebase Realtime Database integration, Chart.js, live status & alerts
 * =========================================================================== */

(function () {
  'use strict';

  // ═══════════════════════════════════════════════════════════════════════════
  //  Constants
  // ═══════════════════════════════════════════════════════════════════════════
  const STORAGE_KEY     = 'pressureMonitor_config';
  const MAX_CHART_POINTS = 300;     // Max data points on chart
  const OFFLINE_TIMEOUT  = 30000;   // 30 s without heartbeat → offline
  const SENSOR_MAX_KG    = 5.0;
  const GAUGE_CIRCUMFERENCE = 534;  // 2 * π * 85 ≈ 534

  // ═══════════════════════════════════════════════════════════════════════════
  //  State
  // ═══════════════════════════════════════════════════════════════════════════
  let firebaseApp     = null;
  let db              = null;
  let deviceId        = 'esp32_node_01';
  let pressureChart   = null;
  let chartData       = [];
  let chartRange      = 60;   // seconds to display
  let offlineTimer    = null;
  let clockInterval   = null;

  // ═══════════════════════════════════════════════════════════════════════════
  //  DOM References
  // ═══════════════════════════════════════════════════════════════════════════
  const $ = (sel) => document.querySelector(sel);
  const $$ = (sel) => document.querySelectorAll(sel);

  const dom = {
    // Status
    statusBadge:    $('#statusBadge'),
    statusText:     $('#statusText'),
    deviceId:       $('#deviceId'),
    deviceLocation: $('#deviceLocation'),
    deviceIp:       $('#deviceIp'),
    deviceRssi:     $('#deviceRssi'),
    deviceUptime:   $('#deviceUptime'),
    deviceLastSeen: $('#deviceLastSeen'),
    // Gauge
    gaugeFill:      $('#gaugeFill'),
    gaugeValue:     $('#gaugeValue'),
    gaugePressure:  $('#gaugePressure'),
    gaugeAdc:       $('#gaugeAdc'),
    // Chart
    chartCanvas:    $('#pressureChart'),
    // Alerts
    alertTableBody: $('#alertTableBody'),
    alertCount:     $('#alertCount'),
    // Settings modal
    settingsModal:  $('#settingsModal'),
    inputApiKey:    $('#inputApiKey'),
    inputDbUrl:     $('#inputDbUrl'),
    inputDeviceId:  $('#inputDeviceId'),
    btnSettings:    $('#btnSettings'),
    btnSaveSettings:    $('#btnSaveSettings'),
    btnCancelSettings:  $('#btnCancelSettings'),
    btnCloseSettings:   $('#btnCloseSettings'),
    // Header
    headerClock:    $('#headerClock'),
  };

  // ═══════════════════════════════════════════════════════════════════════════
  //  Initialize
  // ═══════════════════════════════════════════════════════════════════════════
  function init() {
    startClock();
    initChart();
    bindEvents();

    // Try loading saved config
    const saved = loadConfig();
    if (saved && saved.apiKey && saved.dbUrl) {
      dom.inputApiKey.value   = saved.apiKey;
      dom.inputDbUrl.value    = saved.dbUrl;
      dom.inputDeviceId.value = saved.deviceId || 'esp32_node_01';
      connectFirebase(saved.apiKey, saved.dbUrl, saved.deviceId || 'esp32_node_01');
    } else {
      // Show settings modal on first run
      openSettingsModal();
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Clock
  // ═══════════════════════════════════════════════════════════════════════════
  function startClock() {
    const tick = () => {
      dom.headerClock.textContent = new Date().toLocaleTimeString();
    };
    tick();
    clockInterval = setInterval(tick, 1000);
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Events
  // ═══════════════════════════════════════════════════════════════════════════
  function bindEvents() {
    // Settings modal
    dom.btnSettings.addEventListener('click', openSettingsModal);
    dom.btnCancelSettings.addEventListener('click', closeSettingsModal);
    dom.btnCloseSettings.addEventListener('click', closeSettingsModal);
    dom.btnSaveSettings.addEventListener('click', onSaveSettings);
    dom.settingsModal.addEventListener('click', (e) => {
      if (e.target === dom.settingsModal) closeSettingsModal();
    });

    // Chart range buttons
    $$('.chart-controls .btn').forEach(btn => {
      btn.addEventListener('click', () => {
        $$('.chart-controls .btn').forEach(b => b.classList.remove('btn--active'));
        btn.classList.add('btn--active');
        chartRange = parseInt(btn.dataset.range, 10);
        updateChartView();
      });
    });
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Settings Modal
  // ═══════════════════════════════════════════════════════════════════════════
  function openSettingsModal() {
    dom.settingsModal.classList.add('active');
    dom.inputApiKey.focus();
  }

  function closeSettingsModal() {
    dom.settingsModal.classList.remove('active');
  }

  function onSaveSettings() {
    const apiKey   = dom.inputApiKey.value.trim();
    const dbUrl    = dom.inputDbUrl.value.trim();
    const devId    = dom.inputDeviceId.value.trim() || 'esp32_node_01';

    if (!apiKey || !dbUrl) {
      alert('Please enter both API Key and Database URL.');
      return;
    }

    saveConfig({ apiKey, dbUrl, deviceId: devId });
    closeSettingsModal();
    connectFirebase(apiKey, dbUrl, devId);
  }

  function saveConfig(cfg) {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(cfg));
  }

  function loadConfig() {
    try {
      return JSON.parse(localStorage.getItem(STORAGE_KEY));
    } catch { return null; }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Firebase Connection
  // ═══════════════════════════════════════════════════════════════════════════
  function connectFirebase(apiKey, dbUrl, devId) {
    deviceId = devId;

    // Ensure URL has https://
    if (!dbUrl.startsWith('https://')) {
      dbUrl = 'https://' + dbUrl;
    }

    const firebaseConfig = {
      apiKey: apiKey,
      databaseURL: dbUrl,
    };

    // Re-initialize if already connected
    if (firebaseApp) {
      firebaseApp.delete().then(() => {
        firebaseApp = firebase.initializeApp(firebaseConfig);
        db = firebase.database();
        startListeners();
      });
    } else {
      firebaseApp = firebase.initializeApp(firebaseConfig);
      db = firebase.database();
      startListeners();
    }
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Firebase Listeners
  // ═══════════════════════════════════════════════════════════════════════════
  function startListeners() {
    const basePath = `sensors/${deviceId}`;

    // --- 1. Device status ---
    db.ref(`${basePath}/status`).on('value', (snap) => {
      const data = snap.val();
      if (!data) {
        setDeviceOffline();
        return;
      }
      updateDeviceStatus(data);
    });

    // --- 2. Current pressure data ---
    db.ref(`${basePath}/current`).on('value', (snap) => {
      const data = snap.val();
      if (!data) return;
      updateGauge(data);
      addChartPoint(data);
    });

    // --- 3. Alert history ---
    db.ref(`${basePath}/alerts`).orderByChild('timestamp').limitToLast(50)
      .on('value', (snap) => {
        const data = snap.val();
        renderAlerts(data);
      });

    // --- 4. Connection state ---
    db.ref('.info/connected').on('value', (snap) => {
      if (snap.val() === true) {
        console.log('[Firebase] Connected');
      } else {
        console.log('[Firebase] Disconnected');
      }
    });
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Device Status
  // ═══════════════════════════════════════════════════════════════════════════
  function updateDeviceStatus(data) {
    const isOnline = data.online === true;
    const lastSeen = data.last_seen ? new Date(data.last_seen) : null;

    // Check if last_seen is within threshold
    const now = Date.now();
    const elapsed = lastSeen ? now - lastSeen.getTime() : Infinity;
    const actuallyOnline = isOnline && elapsed < OFFLINE_TIMEOUT;

    // Update badge
    dom.statusBadge.className = `status-badge status-badge--${actuallyOnline ? 'online' : 'offline'}`;
    dom.statusText.textContent = actuallyOnline ? 'Online' : 'Offline';

    // Update info fields
    dom.deviceId.textContent       = data.device_id || deviceId;
    dom.deviceLocation.textContent = data.location || '—';
    dom.deviceIp.textContent       = data.ip || '—';
    dom.deviceRssi.textContent     = data.rssi != null ? `${data.rssi} dBm` : '—';
    dom.deviceUptime.textContent   = data.uptime_sec != null ? formatUptime(data.uptime_sec) : '—';
    dom.deviceLastSeen.textContent = lastSeen ? lastSeen.toLocaleString() : '—';

    // Reset offline timer
    clearTimeout(offlineTimer);
    if (actuallyOnline) {
      offlineTimer = setTimeout(setDeviceOffline, OFFLINE_TIMEOUT);
    }
  }

  function setDeviceOffline() {
    dom.statusBadge.className = 'status-badge status-badge--offline';
    dom.statusText.textContent = 'Offline';
  }

  function formatUptime(seconds) {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    if (h > 0) return `${h}h ${m}m ${s}s`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Gauge
  // ═══════════════════════════════════════════════════════════════════════════
  function updateGauge(data) {
    const weight = data.weight_kg != null ? data.weight_kg : 0;
    const pressure = data.pressure_kpa != null ? data.pressure_kpa : 0;
    const adc = data.adc_raw != null ? data.adc_raw : 0;

    // Update text values
    dom.gaugeValue.textContent    = weight.toFixed(2);
    dom.gaugePressure.textContent = pressure.toFixed(1) + ' kPa';
    dom.gaugeAdc.textContent      = adc;

    // Update gauge fill
    const pct = Math.min(weight / SENSOR_MAX_KG, 1);
    const offset = GAUGE_CIRCUMFERENCE * (1 - pct);
    dom.gaugeFill.style.strokeDashoffset = offset;

    // Color based on level
    let color;
    if (pct < 0.5)      color = 'var(--gauge-good)';
    else if (pct < 0.8) color = 'var(--gauge-warn)';
    else                 color = 'var(--gauge-danger)';

    dom.gaugeFill.style.stroke = color;
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Chart
  // ═══════════════════════════════════════════════════════════════════════════
  function initChart() {
    const ctx = dom.chartCanvas.getContext('2d');

    // Gradient fill
    const gradient = ctx.createLinearGradient(0, 0, 0, 320);
    gradient.addColorStop(0, 'rgba(0, 210, 255, 0.25)');
    gradient.addColorStop(1, 'rgba(0, 210, 255, 0.0)');

    pressureChart = new Chart(ctx, {
      type: 'line',
      data: {
        datasets: [{
          label: 'Weight (kg)',
          data: [],
          borderColor: '#00d2ff',
          backgroundColor: gradient,
          borderWidth: 2,
          pointRadius: 0,
          pointHoverRadius: 5,
          pointHoverBackgroundColor: '#00d2ff',
          pointHoverBorderColor: '#fff',
          pointHoverBorderWidth: 2,
          fill: true,
          tension: 0.35,
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: {
          duration: 300,
          easing: 'easeOutQuart',
        },
        interaction: {
          mode: 'index',
          intersect: false,
        },
        scales: {
          x: {
            type: 'time',
            time: {
              unit: 'second',
              displayFormats: {
                second: 'HH:mm:ss',
                minute: 'HH:mm',
              },
            },
            ticks: {
              color: '#64748b',
              maxTicksLimit: 10,
              font: { size: 11, family: 'Inter' },
            },
            grid: {
              color: 'rgba(255,255,255,0.04)',
              drawBorder: false,
            },
          },
          y: {
            min: 0,
            max: SENSOR_MAX_KG,
            ticks: {
              color: '#64748b',
              maxTicksLimit: 6,
              font: { size: 11, family: 'Inter' },
              callback: (v) => v.toFixed(1) + ' kg',
            },
            grid: {
              color: 'rgba(255,255,255,0.04)',
              drawBorder: false,
            },
          },
        },
        plugins: {
          legend: { display: false },
          tooltip: {
            backgroundColor: 'rgba(17,24,39,0.95)',
            titleColor: '#f0f4f8',
            bodyColor: '#94a3b8',
            borderColor: 'rgba(255,255,255,0.1)',
            borderWidth: 1,
            cornerRadius: 8,
            padding: 12,
            titleFont: { size: 12, family: 'Inter', weight: '600' },
            bodyFont:  { size: 11, family: 'Inter' },
            callbacks: {
              label: (ctx) => `Weight: ${ctx.parsed.y.toFixed(3)} kg`,
            },
          },
        },
      },
    });
  }

  function addChartPoint(data) {
    const ts = data.timestamp ? new Date(data.timestamp) : new Date();
    const weight = data.weight_kg != null ? data.weight_kg : 0;

    chartData.push({ x: ts, y: weight });

    // Cap stored data
    if (chartData.length > MAX_CHART_POINTS * 2) {
      chartData = chartData.slice(-MAX_CHART_POINTS);
    }

    updateChartView();
  }

  function updateChartView() {
    const now = Date.now();
    const cutoff = now - chartRange * 1000;

    const visible = chartData.filter(d => d.x.getTime() >= cutoff);

    pressureChart.data.datasets[0].data = visible;

    // Adjust time unit based on range
    if (chartRange <= 60) {
      pressureChart.options.scales.x.time.unit = 'second';
    } else if (chartRange <= 600) {
      pressureChart.options.scales.x.time.unit = 'second';
      pressureChart.options.scales.x.ticks.maxTicksLimit = 8;
    } else {
      pressureChart.options.scales.x.time.unit = 'minute';
    }

    pressureChart.update('none'); // No animation for real-time updates
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Alerts Table
  // ═══════════════════════════════════════════════════════════════════════════
  function renderAlerts(data) {
    if (!data) {
      dom.alertTableBody.innerHTML = `
        <tr class="data-table__empty">
          <td colspan="6">No alerts recorded yet</td>
        </tr>`;
      dom.alertCount.textContent = '0';
      return;
    }

    // Convert to array and sort descending by timestamp
    const alerts = Object.entries(data)
      .map(([key, val]) => ({ id: key, ...val }))
      .sort((a, b) => (b.timestamp || 0) - (a.timestamp || 0));

    dom.alertCount.textContent = alerts.length;

    dom.alertTableBody.innerHTML = alerts.map(alert => {
      const time = alert.timestamp ? new Date(alert.timestamp).toLocaleString() : '—';
      const isAbnormal = alert.type === 'ABNORMAL_PRESSURE';
      const typeClass = isAbnormal ? 'alert-type--abnormal' : 'alert-type--continuous';
      const typeLabel = isAbnormal ? '🚨 Abnormal' : '⏱️ Continuous';
      const statusClass = alert.resolved ? 'alert-status--resolved' : 'alert-status--active';
      const statusLabel = alert.resolved ? '✅ Resolved' : '⚠️ Active';

      return `
        <tr>
          <td>${time}</td>
          <td><span class="alert-type ${typeClass}">${typeLabel}</span></td>
          <td>${alert.weight_kg != null ? alert.weight_kg.toFixed(2) : '—'}</td>
          <td>${alert.pressure_kpa != null ? alert.pressure_kpa.toFixed(1) : '—'}</td>
          <td>${alert.location || '—'}</td>
          <td><span class="alert-status ${statusClass}">${statusLabel}</span></td>
        </tr>`;
    }).join('');
  }

  // ═══════════════════════════════════════════════════════════════════════════
  //  Boot
  // ═══════════════════════════════════════════════════════════════════════════
  document.addEventListener('DOMContentLoaded', init);
})();
