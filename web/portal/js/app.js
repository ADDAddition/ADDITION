const LOCAL_API = (() => {
    const queryApi = new URLSearchParams(window.location.search).get('api');
    if (queryApi) {
        const normalized = queryApi.trim().replace(/\/$/, '');
        localStorage.setItem('PORTAL_API_BASE', normalized);
        return normalized;
    }

    const stored = (localStorage.getItem('PORTAL_API_BASE') || '').trim();
    if (stored) {
        return stored.replace(/\/$/, '');
    }

    const host = window.location.hostname;
    if (host === 'localhost' || host === '127.0.0.1') {
        return 'http://127.0.0.1:8080';
    }

    if (host === 'dogecointoday.com' || host === 'www.dogecointoday.com') {
        return 'https://api.dogecointoday.com';
    }

    return 'https://api.xa1.ai';
})();
const TPS_HISTORY = [];
const TPS_HISTORY_MAX = 60;

async function getJson(path) {
    try {
        const res = await fetch(LOCAL_API + '/api' + path);
        return await res.json();
    } catch (e) {
        return { ok: false, error: e.message || 'network error' };
    }
}

async function postJson(path, body) {
    try {
        const res = await fetch(LOCAL_API + path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body || {})
        });
        return await res.json();
    } catch (e) {
        return { ok: false, error: e.message || 'network error' };
    }
}

function setText(id, value) {
    const el = document.getElementById(id);
    if (el) {
        el.textContent = value;
    }
}

window.switchSwap = function () {
    const tIn = document.getElementById('swap-token-in');
    const tOut = document.getElementById('swap-token-out');
    if (!tIn || !tOut) return;
    const temp = tIn.value;
    tIn.value = tOut.value;
    tOut.value = temp;
};

async function refresh() {
    const infoRes = await getJson('/getinfo');
    const monRes = await getJson('/monetary');

    const netRaw = document.getElementById('network-raw');

    if (infoRes.ok && infoRes.data) {
        const d = infoRes.data;
        setText('m-height', d.height || '0');
        setText('m-peers', d.peers || '0');
        setText('kpi-tps', d.last_tps || '0.00');
        pushTpsPoint(Number(d.last_tps || 0));
        renderTpsChart();
        setText('m-diff', d.difficulty_target || '-');

        const mineMs = Number(d.last_mine_ms || 0);
        const hash = mineMs > 0 ? (1000 / mineMs) : 0;
        const hashStr = hash > 1000 ? `${hash.toFixed(2)} MH/s` : `${hash.toFixed(2)} kH/s`;
        setText('m-hash', hashStr);

        const hs = document.getElementById('health-status');
        if (hs) {
            hs.textContent = 'OPERATIONAL';
            hs.style.color = '#27C93F';
        }

        if (netRaw) {
            netRaw.textContent = infoRes.raw || JSON.stringify(d, null, 2);
        }

        const kpiHeight = document.getElementById('kpi-height');
        if (kpiHeight) kpiHeight.textContent = d.height || '0';

        const kpiPeers = document.getElementById('kpi-peers');
        if (kpiPeers) kpiPeers.textContent = d.peers || '0';

        const kpiHealth = document.getElementById('kpi-health');
        if (kpiHealth) {
            kpiHealth.textContent = 'OPERATIONAL';
            kpiHealth.className = 'status-ok';
        }
    } else {
        const hs = document.getElementById('health-status');
        if (hs) {
            hs.textContent = 'OFFLINE';
            hs.style.color = '#FF3333';
        }

        if (netRaw) {
            netRaw.textContent = infoRes.error || infoRes.raw || 'RPC offline';
        }

        const kpiHealth = document.getElementById('kpi-health');
        if (kpiHealth) {
            kpiHealth.textContent = 'OFFLINE';
            kpiHealth.className = 'status-danger';
        }
    }

    if (monRes.ok && monRes.data) {
        const m = monRes.data;
        setText('m-emitted', `${Number(m.emitted || 0).toLocaleString()} /ADD`);
        setText('tok-max', Number(m.max_supply || 0).toLocaleString());
        setText('tok-emitted', Number(m.emitted || 0).toLocaleString());

        const maxSupply = Number(m.max_supply || 0);
        const emitted = Number(m.emitted || 0);
        const remain = Math.max(0, maxSupply - emitted);
        const progress = maxSupply > 0 ? ((emitted / maxSupply) * 100) : 0;

        setText('tok-remaining', remain.toLocaleString());
        setText('tok-progress', `${progress.toFixed(4)}%`);

        const tokRaw = document.getElementById('tokenomics-raw');
        if (tokRaw) {
            tokRaw.textContent = monRes.raw || JSON.stringify(m, null, 2);
        }
    }
}

function pushTpsPoint(v) {
    TPS_HISTORY.push(Number.isFinite(v) ? v : 0);
    if (TPS_HISTORY.length > TPS_HISTORY_MAX) {
        TPS_HISTORY.shift();
    }
}

function renderTpsChart() {
    const canvas = document.getElementById('tps-chart');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = '#0a0d1a';
    ctx.fillRect(0, 0, w, h);

    ctx.strokeStyle = 'rgba(255,255,255,0.12)';
    ctx.lineWidth = 1;
    for (let i = 1; i < 4; i += 1) {
        const y = (h / 4) * i;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(w, y);
        ctx.stroke();
    }

    if (TPS_HISTORY.length < 2) return;

    const min = Math.min(...TPS_HISTORY);
    const max = Math.max(...TPS_HISTORY);
    const span = Math.max(1, max - min);

    ctx.strokeStyle = '#53d8ff';
    ctx.lineWidth = 2;
    ctx.beginPath();

    TPS_HISTORY.forEach((v, i) => {
        const x = (i / (TPS_HISTORY.length - 1)) * (w - 20) + 10;
        const y = h - (((v - min) / span) * (h - 30) + 15);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    });

    ctx.stroke();

    ctx.fillStyle = '#edf1ff';
    ctx.font = '12px JetBrains Mono';
    ctx.fillText(`TPS now: ${TPS_HISTORY[TPS_HISTORY.length - 1].toFixed(2)}`, 12, 18);
}

window.executeBestRouteSwap = async () => {
    const out = document.getElementById('swap-result');
    if (out) out.textContent = 'Negotiating best route...';

    const tIn = (document.getElementById('swap-token-in') || {}).value || '';
    const tOut = (document.getElementById('swap-token-out') || {}).value || '';
    const amIn = Number(((document.getElementById('swap-amount-in') || {}).value || '0'));

    const res = await postJson('/api/swap/quote', {
        token_in: tIn,
        token_out: tOut,
        amount_in: amIn
    });

    const routeEl = document.getElementById('swap-route');
    const rawEl = document.getElementById('swap-raw');

    if (res.ok && res.data) {
        const quoteEl = document.getElementById('swap-quote-out');
        if (quoteEl) quoteEl.textContent = res.data.amount_out || '0.00';
        if (routeEl) routeEl.textContent = res.data.route || 'direct';
        if (out) out.textContent = `Best route: ${res.data.route || 'direct'}`;
        if (rawEl) rawEl.textContent = res.raw || JSON.stringify(res.data, null, 2);
    } else if (out) {
        out.textContent = `Error: ${res.error || res.raw || 'Check backend/daemon'}`;
        if (rawEl) rawEl.textContent = res.error || res.raw || 'No response';
    }
};

function bindButtons() {
    const switchBtn = document.getElementById('btn-swap-switch');
    if (switchBtn) {
        switchBtn.addEventListener('click', window.switchSwap);
    }

    const quoteBtn = document.getElementById('btn-swap-quote');
    if (quoteBtn) {
        quoteBtn.addEventListener('click', window.executeBestRouteSwap);
    }
}

window.addEventListener('load', () => {
    bindButtons();
    setInterval(refresh, 5000);
    refresh();
});
