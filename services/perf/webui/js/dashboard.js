let chart = null;

async function refresh() {
    const sel = document.getElementById('stageSelect').value;
    const [service, stage] = sel.split('|');

    const stats = await fetchStats(service, stage);
    if (!stats) return;

    document.getElementById('count').textContent = stats.count || 0;
    document.getElementById('avg').textContent = (stats.avg || 0).toFixed(2);
    document.getElementById('p50').textContent = (stats.p50 || 0).toFixed(2);
    document.getElementById('p99').textContent = (stats.p99 || 0).toFixed(2);

    updateChart(service, stage, stats);
}

function onStageChange() {
    refresh();
}

function updateChart(service, stage, stats) {
    const ctx = document.getElementById('chart').getContext('2d');
    if (chart) chart.destroy();

    chart = new Chart(ctx, {
        type: 'bar',
        data: {
            labels: ['Min', 'Avg', 'P50', 'P99', 'Max'],
            datasets: [{
                label: `${service} | ${stage}`,
                data: [stats.min||0, stats.avg||0, stats.p50||0, stats.p99||0, stats.max||0],
                backgroundColor: ['#4caf50', '#2196f3', '#ff9800', '#f44336', '#9c27b0'],
            }]
        },
        options: {
            responsive: true,
            plugins: {
                legend: { display: false },
            },
            scales: {
                y: {
                    beginAtZero: true,
                    title: { display: true, text: 'ms' }
                }
            }
        }
    });
}

refresh();
setInterval(refresh, 3000);
