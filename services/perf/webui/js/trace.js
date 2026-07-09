async function searchTrace() {
    const traceId = document.getElementById('traceIdInput').value.trim();
    if (!traceId) return;

    const result = await fetchTrace(traceId);
    const container = document.getElementById('trace-result');

    if (!result || !result.spans) {
        container.innerHTML = '<p>Trace not found.</p>';
        return;
    }

    const spans = result.spans;
    if (!spans.length) {
        container.innerHTML = '<p>No spans found for this trace_id.</p>';
        return;
    }

    spans.sort((a,b) => a.ts_us - b.ts_us);
    const baseTs = spans[0].ts_us;
    const maxDuration = Math.max(...spans.map(s => s.duration_ms), 1);

    let html = '<table><tr><th>Service</th><th>Stage</th><th>Duration (ms)</th><th>Waterfall</th><th>Status</th></tr>';
    for (const s of spans) {
        const offset = ((s.ts_us - baseTs) / 1000).toFixed(0);
        const barWidth = Math.max((s.duration_ms / maxDuration) * 200, 5);
        html += `<tr>
            <td>${s.service}</td>
            <td>${s.stage}</td>
            <td>${s.duration_ms.toFixed(3)}</td>
            <td>
                <div style="margin-left:${offset}px">
                    <div style="background:#2196f3;height:18px;width:${barWidth}px;border-radius:2px;opacity:0.8"></div>
                </div>
            </td>
            <td>${s.status}</td>
        </tr>`;
    }
    html += '</table>';
    html += `<p style="margin-top:8px;color:#666;font-size:12px">${spans.length} spans, total ${((spans[spans.length-1].ts_us - baseTs) / 1000).toFixed(1)} ms</p>`;
    container.innerHTML = html;
}
