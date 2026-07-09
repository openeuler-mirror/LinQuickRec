async function loadSeries() {
    const list = await fetchSeriesList();
    const container = document.getElementById('series-list');
    if (!list.length) {
        container.innerHTML = '<p>No series yet. Click + New Series to start.</p>';
        return;
    }

    let html = '<table><tr><th>ID</th><th>Name</th><th>Status</th><th>Span Count</th></tr>';
    for (const s of list) {
        html += `<tr>
            <td>${s.id}</td>
            <td><a href="#" onclick="viewSeries(${s.id},'${s.name}')">${s.name}</a></td>
            <td>${s.status}</td>
            <td>${s.span_count || 0}</td>
        </tr>`;
    }
    html += '</table>';
    container.innerHTML = html;
}

async function startSeries() {
    const name = prompt('Series name:', 'baseline-' + new Date().toISOString().slice(0,19));
    if (!name) return;
    await startSeriesReq(name);
    loadSeries();
}

async function stopSeries() {
    await stopSeriesReq();
    loadSeries();
}

async function viewSeries(id, name) {
    // Fetch stats for this series by querying all known stages
    document.getElementById('series-detail').style.display = 'block';
    document.getElementById('series-detail-title').textContent = `Series: ${name} (ID: ${id})`;

    // Display series summary from the list
    const list = await fetchSeriesList();
    const info = list.find(s => s.id == id);
    const statsContainer = document.getElementById('series-stats');
    if (info) {
        statsContainer.innerHTML = `<div class="card">
            <h3>Status</h3><div class="metric">${info.status}</div>
            <div class="metric-label">${info.span_count || 0} spans</div>
        </div>`;
    }
}

function loadSeriesSpans() {
    document.getElementById('series-spans-table').style.display = 'table';
}

loadSeries();
