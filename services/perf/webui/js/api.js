const API_BASE = '';

async function fetchStats(service, stage) {
    const url = `${API_BASE}/api/v1/stats/current?service=${encodeURIComponent(service)}&stage=${encodeURIComponent(stage)}`;
    const resp = await fetch(url);
    if (!resp.ok) return null;
    return resp.json();
}

async function fetchSeries() {
    const resp = await fetch(`${API_BASE}/api/v1/series`);
    if (!resp.ok) return [];
    const data = await resp.json();
    return data.series || [];
}
