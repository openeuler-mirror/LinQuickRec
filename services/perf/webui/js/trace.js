async function searchTrace() {
    const traceId = document.getElementById('traceIdInput').value.trim();
    if (!traceId) return;

    const result = await fetchTrace(traceId);
    const container = document.getElementById('trace-result');

    if (!result || !result.tree) {
        container.innerHTML = '<p>Trace not found.</p>';
        return;
    }

    let html = '<table><tr><th>Service</th><th>Stage</th><th>Duration (ms)</th><th>Waterfall</th><th>Status</th></tr>';

    // Collect all leaf spans to compute time range for waterfall scaling
    const allSpans = [];
    function collect(node) {
        if (node.has_span || node.virtual) allSpans.push(node);
        for (const c of (node.children || [])) collect(c);
    }
    collect(result.tree);

    if (!allSpans.length) {
        container.innerHTML = '<p>No spans in tree.</p>';
        return;
    }

    const minTs = allSpans[0].ts_us || 0;
    const maxDuration = Math.max(...allSpans.map(s => s.duration_ms || 0), 1);
    const tree = result.tree;

    function render(node, depth) {
        if (!node || (!node.has_span && !node.virtual && !(node.children && node.children.length))) return;
        const indent = depth * 20;
        const offset = node.ts_us ? ((node.ts_us - minTs) / 1000).toFixed(0) : 0;
        const dur = node.duration_ms || 0;
        const barWidth = Math.max((dur / maxDuration) * 200, 3);

        html += `<tr>
            <td style="padding-left:${indent}px">${node.service}</td>
            <td>${node.stage}</td>
            <td>${dur.toFixed(3)}</td>
            <td>
                <div style="margin-left:${offset}px">
                    <div style="background:#2196f3;height:18px;width:${barWidth}px;border-radius:2px;opacity:0.8"></div>
                </div>
            </td>
            <td>${node.status || (node.virtual ? 'virtual' : '')}</td>
        </tr>`;
        for (const c of (node.children || [])) render(c, depth + 1);
    }

    render(tree, 0);

    html += '</table>';
    const lastSpan = allSpans[allSpans.length - 1];
    html += `<p style="margin-top:8px;color:#666;font-size:12px">${allSpans.length} nodes, total ${lastSpan ? ((lastSpan.ts_us - minTs) / 1000).toFixed(1) : 0} ms</p>`;
    container.innerHTML = html;
}
