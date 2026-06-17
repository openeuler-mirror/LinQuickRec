#!/usr/bin/env python3
"""Collect LinQuickRec PERF logs from Kubernetes pods and build an HTML report."""

from __future__ import annotations

import argparse
import datetime as dt
import html
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from typing import Any


DEFAULT_SERVICES = "proxy,feature,recall,precalc,rank-master,rank-sub"
TIMESTAMP_RE = re.compile(r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
KV_RE = re.compile(r"(\w+)=([^\s]+)")


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=check)


def service_label(service: str) -> str:
    return f"app={service}-service"


def collect_logs(namespace: str, services: list[str], remote_log_dir: str, raw_dir: Path) -> None:
    raw_dir.mkdir(parents=True, exist_ok=True)
    for service in services:
        cmd = [
            "kubectl", "get", "pods",
            "-n", namespace,
            "-l", service_label(service),
            "-o", "json",
        ]
        proc = run(cmd)
        pods = json.loads(proc.stdout).get("items", [])
        if not pods:
            print(f"[WARN] no pods found for {service}", file=sys.stderr)
            continue

        for pod in pods:
            pod_name = pod["metadata"]["name"]
            pod_dir = raw_dir / pod_name
            if pod_dir.exists():
                shutil.rmtree(pod_dir)
            pod_dir.mkdir(parents=True, exist_ok=True)
            src = f"{namespace}/{pod_name}:{remote_log_dir}/."
            cp_cmd = ["kubectl", "cp", src, str(pod_dir)]
            cp = run(cp_cmd, check=False)
            if cp.returncode != 0:
                print(f"[WARN] kubectl cp failed for {pod_name}: {cp.stderr.strip()}", file=sys.stderr)


def parse_timestamp(line: str, fallback: float) -> tuple[str, float]:
    match = TIMESTAMP_RE.search(line)
    if not match:
        iso = dt.datetime.fromtimestamp(fallback).isoformat(timespec="milliseconds")
        return iso, fallback * 1000.0
    parsed = dt.datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S.%f")
    return parsed.isoformat(timespec="milliseconds"), parsed.timestamp() * 1000.0


def parse_perf_line(line: str, path: Path, pod: str, fallback_mtime: float) -> dict[str, Any] | None:
    if "PERF " not in line:
        return None
    _, payload = line.split("PERF ", 1)
    fields = {key: value for key, value in KV_RE.findall(payload)}
    required = {"service", "stage", "metric", "duration_ms", "status"}
    if not required.issubset(fields):
        return None
    try:
        duration_ms = float(fields["duration_ms"])
    except ValueError:
        return None

    ts_iso, ts_ms = parse_timestamp(line, fallback_mtime)
    event = {
        "timestamp": ts_iso,
        "timestamp_ms": ts_ms,
        "service": fields.pop("service"),
        "stage": fields.pop("stage"),
        "metric": fields.pop("metric"),
        "duration_ms": duration_ms,
        "status": fields.pop("status"),
        "trace_id": fields.pop("trace_id", "-"),
        "pod": pod,
        "source": str(path),
        "extra": fields,
    }
    return event


def iter_log_files(root: Path) -> list[Path]:
    files: list[Path] = []
    for path in root.rglob("*"):
        if path.is_file() and (".log" in path.name or path.suffix == ".txt"):
            files.append(path)
    return sorted(files)


def parse_logs(raw_dir: Path) -> list[dict[str, Any]]:
    events: list[dict[str, Any]] = []
    for path in iter_log_files(raw_dir):
        pod = path.relative_to(raw_dir).parts[0] if path != raw_dir else "-"
        fallback_mtime = path.stat().st_mtime
        try:
            with path.open("r", encoding="utf-8", errors="replace") as fh:
                for line in fh:
                    event = parse_perf_line(line, path, pod, fallback_mtime)
                    if event:
                        events.append(event)
        except OSError as exc:
            print(f"[WARN] failed to read {path}: {exc}", file=sys.stderr)
    events.sort(key=lambda item: item["timestamp_ms"])
    return events


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    rank = max(1, math.ceil((pct / 100.0) * len(sorted_values)))
    return sorted_values[min(rank - 1, len(sorted_values) - 1)]


def compute_stats(events: list[dict[str, Any]]) -> list[dict[str, Any]]:
    grouped: dict[tuple[str, str, str], list[float]] = {}
    for event in events:
        key = (event["metric"], event["service"], event["stage"])
        grouped.setdefault(key, []).append(event["duration_ms"])

    rows: list[dict[str, Any]] = []
    for (metric, service, stage), values in sorted(grouped.items()):
        rows.append({
            "metric": metric,
            "service": service,
            "stage": stage,
            "count": len(values),
            "ave": sum(values) / len(values),
            "p99": percentile(values, 99),
            "p999": percentile(values, 99.9),
            "p9999": percentile(values, 99.99),
            "min": min(values),
            "max": max(values),
        })
    return rows


def write_data_json(out_dir: Path, events: list[dict[str, Any]], stats: list[dict[str, Any]]) -> None:
    data = {
        "generated_at": dt.datetime.now().isoformat(timespec="seconds"),
        "events": events,
        "stats": stats,
    }
    (out_dir / "data.json").write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def fmt_json_for_html(data: dict[str, Any]) -> str:
    return json.dumps(data, ensure_ascii=False).replace("</", "<\\/")


def write_html(out_dir: Path, events: list[dict[str, Any]], stats: list[dict[str, Any]]) -> None:
    data = {"events": events, "stats": stats}
    html_text = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LinQuickRec Performance Report</title>
<style>
body {{ margin: 0; font-family: Arial, sans-serif; color: #1f2933; background: #f7f9fb; }}
header {{ padding: 20px 28px; background: #10384f; color: #fff; }}
h1 {{ margin: 0; font-size: 24px; letter-spacing: 0; }}
main {{ padding: 20px 28px 36px; }}
.controls {{ display: grid; grid-template-columns: repeat(4, minmax(160px, 1fr)); gap: 12px; margin-bottom: 18px; }}
label {{ display: grid; gap: 5px; font-size: 12px; font-weight: 700; color: #435466; }}
select {{ height: 34px; border: 1px solid #cbd5df; border-radius: 6px; padding: 0 8px; background: #fff; }}
.panel {{ background: #fff; border: 1px solid #d8e0e8; border-radius: 8px; padding: 16px; margin-bottom: 18px; }}
.panel h2 {{ margin: 0 0 12px; font-size: 18px; }}
canvas {{ width: 100%; height: 360px; display: block; border: 1px solid #e3e8ef; border-radius: 6px; background: #fff; }}
table {{ width: 100%; border-collapse: collapse; font-size: 13px; }}
th, td {{ border-bottom: 1px solid #e4e9ef; padding: 8px 9px; text-align: right; white-space: nowrap; }}
th:first-child, td:first-child, th:nth-child(2), td:nth-child(2), th:nth-child(3), td:nth-child(3) {{ text-align: left; }}
th {{ background: #eef3f7; color: #334; position: sticky; top: 0; }}
.table-wrap {{ max-height: 520px; overflow: auto; border: 1px solid #e3e8ef; border-radius: 6px; }}
.meta {{ color: #536579; font-size: 13px; margin-top: 8px; }}
@media (max-width: 760px) {{ .controls {{ grid-template-columns: 1fr; }} main {{ padding: 14px; }} }}
</style>
</head>
<body>
<header>
<h1>LinQuickRec Performance Report</h1>
<div class="meta">Generated from PERF logs. Durations are milliseconds.</div>
</header>
<main>
<section class="controls">
<label>Metric<select id="metric"></select></label>
<label>Service<select id="service"></select></label>
<label>Stage<select id="stage"></select></label>
<label>Pod<select id="pod"></select></label>
</section>
<section class="panel">
<h2>Latency Over Time</h2>
<canvas id="chart" width="1200" height="360"></canvas>
<div id="chartMeta" class="meta"></div>
</section>
<section class="panel">
<h2>All Stage Statistics</h2>
<div class="table-wrap"><table id="statsTable"></table></div>
</section>
<section class="panel">
<h2>BRPC Communication Statistics</h2>
<div class="table-wrap"><table id="brpcTable"></table></div>
</section>
</main>
<script>
const DATA = {fmt_json_for_html(data)};
const colors = ["#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c", "#0891b2", "#4f46e5", "#be123c"];

function uniq(items) {{ return Array.from(new Set(items)).sort(); }}
function byId(id) {{ return document.getElementById(id); }}
function optionList(values) {{ return ["all"].concat(values); }}
function fillSelect(id, values) {{
  const el = byId(id);
  const old = el.value || "all";
  el.innerHTML = optionList(values).map(v => `<option value="${{v}}">${{v}}</option>`).join("");
  el.value = optionList(values).includes(old) ? old : "all";
}}
function filters() {{
  return {{ metric: byId("metric").value, service: byId("service").value, stage: byId("stage").value, pod: byId("pod").value }};
}}
function matches(item, f) {{
  return (f.metric === "all" || item.metric === f.metric)
    && (f.service === "all" || item.service === f.service)
    && (f.stage === "all" || item.stage === f.stage)
    && (f.pod === "all" || item.pod === f.pod);
}}
function filteredEvents() {{ return DATA.events.filter(e => matches(e, filters())); }}
function formatNum(v) {{ return Number(v).toFixed(3); }}
function percentile(values, pct) {{
  if (!values.length) return 0;
  const sorted = values.slice().sort((a, b) => a - b);
  const rank = Math.max(1, Math.ceil((pct / 100) * sorted.length));
  return sorted[Math.min(rank - 1, sorted.length - 1)];
}}
function statsFromEvents(events) {{
  const groups = {{}};
  for (const e of events) {{
    const key = `${{e.metric}}\\u0001${{e.service}}\\u0001${{e.stage}}`;
    (groups[key] ||= []).push(e.duration_ms);
  }}
  return Object.entries(groups).sort().map(([key, values]) => {{
    const [metric, service, stage] = key.split("\\u0001");
    const sum = values.reduce((a, b) => a + b, 0);
    return {{
      metric, service, stage, count: values.length,
      ave: sum / values.length,
      p99: percentile(values, 99),
      p999: percentile(values, 99.9),
      p9999: percentile(values, 99.99),
      min: Math.min(...values),
      max: Math.max(...values)
    }};
  }});
}}

function drawChart() {{
  const canvas = byId("chart");
  const ctx = canvas.getContext("2d");
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  const events = filteredEvents();
  byId("chartMeta").textContent = `${{events.length}} points`;
  if (!events.length) {{
    ctx.fillStyle = "#64748b"; ctx.font = "16px Arial"; ctx.fillText("No matching PERF events", 24, 40); return;
  }}
  const pad = {{l: 62, r: 18, t: 22, b: 42}};
  const w = canvas.width - pad.l - pad.r, h = canvas.height - pad.t - pad.b;
  const minT = Math.min(...events.map(e => e.timestamp_ms));
  const maxT = Math.max(...events.map(e => e.timestamp_ms));
  const maxY = Math.max(1, ...events.map(e => e.duration_ms)) * 1.08;
  ctx.strokeStyle = "#d7dee7"; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(pad.l, pad.t); ctx.lineTo(pad.l, pad.t + h); ctx.lineTo(pad.l + w, pad.t + h); ctx.stroke();
  ctx.fillStyle = "#475569"; ctx.font = "12px Arial";
  for (let i = 0; i <= 5; i++) {{
    const yv = maxY * i / 5;
    const y = pad.t + h - (yv / maxY) * h;
    ctx.strokeStyle = "#edf1f5"; ctx.beginPath(); ctx.moveTo(pad.l, y); ctx.lineTo(pad.l + w, y); ctx.stroke();
    ctx.fillText(formatNum(yv), 8, y + 4);
  }}
  const groups = {{}};
  for (const e of events) {{
    const key = `${{e.metric}}/${{e.service}}/${{e.stage}}`;
    (groups[key] ||= []).push(e);
  }}
  let idx = 0;
  for (const [key, rows] of Object.entries(groups).sort()) {{
    rows.sort((a, b) => a.timestamp_ms - b.timestamp_ms);
    ctx.strokeStyle = colors[idx % colors.length]; ctx.fillStyle = ctx.strokeStyle; ctx.lineWidth = 2;
    ctx.beginPath();
    rows.forEach((e, i) => {{
      const x = pad.l + ((e.timestamp_ms - minT) / Math.max(1, maxT - minT)) * w;
      const y = pad.t + h - (e.duration_ms / maxY) * h;
      if (i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
    }});
    ctx.stroke();
    rows.forEach(e => {{
      const x = pad.l + ((e.timestamp_ms - minT) / Math.max(1, maxT - minT)) * w;
      const y = pad.t + h - (e.duration_ms / maxY) * h;
      ctx.beginPath(); ctx.arc(x, y, 2.4, 0, Math.PI * 2); ctx.fill();
    }});
    ctx.fillText(key, pad.l + 8 + (idx % 2) * 360, pad.t + 15 + Math.floor(idx / 2) * 16);
    idx++;
  }}
}}

function renderTable(id, rows) {{
  const head = ["metric", "service", "stage", "count", "ave", "p99", "p999", "p9999", "min", "max"];
  const body = rows.map(r => `<tr>${{head.map(k => `<td>${{k === "count" ? r[k] : (typeof r[k] === "number" ? formatNum(r[k]) : r[k])}}</td>`).join("")}}</tr>`).join("");
  byId(id).innerHTML = `<thead><tr>${{head.map(h => `<th>${{h}}</th>`).join("")}}</tr></thead><tbody>${{body}}</tbody>`;
}}
function renderStats() {{
  const rows = statsFromEvents(filteredEvents());
  renderTable("statsTable", rows);
  renderTable("brpcTable", rows.filter(r => r.metric === "brpc"));
}}
function refresh() {{ drawChart(); renderStats(); }}
function init() {{
  fillSelect("metric", uniq(DATA.events.map(e => e.metric)));
  fillSelect("service", uniq(DATA.events.map(e => e.service)));
  fillSelect("stage", uniq(DATA.events.map(e => e.stage)));
  fillSelect("pod", uniq(DATA.events.map(e => e.pod)));
  ["metric", "service", "stage", "pod"].forEach(id => byId(id).addEventListener("change", refresh));
  refresh();
}}
init();
</script>
</body>
</html>
"""
    (out_dir / "index.html").write_text(html_text, encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--namespace", default="linquickrec")
    parser.add_argument("--remote-log-dir", default="/var/log/linquickrec")
    parser.add_argument("--out-dir", default="perf_report")
    parser.add_argument("--services", default=DEFAULT_SERVICES)
    parser.add_argument("--input-dir", default=None, help="Existing raw log directory to parse")
    parser.add_argument("--no-collect", action="store_true", help="Skip kubectl collection and parse input/raw logs")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    raw_dir = Path(args.input_dir) if args.input_dir else out_dir / "raw"
    services = [item.strip() for item in args.services.split(",") if item.strip()]

    if not args.no_collect:
        collect_logs(args.namespace, services, args.remote_log_dir, raw_dir)

    events = parse_logs(raw_dir)
    stats = compute_stats(events)
    write_data_json(out_dir, events, stats)
    write_html(out_dir, events, stats)
    print(f"Parsed {len(events)} PERF events")
    print(f"Wrote {out_dir / 'data.json'}")
    print(f"Wrote {out_dir / 'index.html'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
