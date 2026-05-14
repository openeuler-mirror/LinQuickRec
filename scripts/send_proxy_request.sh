#!/usr/bin/env bash
# 向 Proxy 服务发送推荐请求
# 用法:
#   bash scripts/send_proxy_request.sh                      # 单次请求
#   bash scripts/send_proxy_request.sh -n 20                # 批量 20 次，统计时延
#   bash scripts/send_proxy_request.sh --user_id=99999      # 指定 user_id
#   bash scripts/send_proxy_request.sh --host=192.168.1.5   # 指定 Proxy 地址

set -euo pipefail

# ── 默认参数 ──
PROXY_HOST="localhost"
PROXY_PORT="8080"
USER_ID="12345"
PAYLOAD=""
REQUEST_COUNT=1

# ── 解析参数 ──
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host=*)    PROXY_HOST="${1#*=}"; shift ;;
        --port=*)    PROXY_PORT="${1#*=}"; shift ;;
        --user_id=*) USER_ID="${1#*=}";    shift ;;
        --payload=*) PAYLOAD="${1#*=}";    shift ;;
        -n*)         REQUEST_COUNT="${1#-n}"; shift ;;
        *)           echo "未知参数: $1"; exit 1 ;;
    esac
done

URL="http://${PROXY_HOST}:${PROXY_PORT}/Proxy/Recommend"

# ── 构造请求体 ──
build_body() {
    local uid="$1"
    local payload="$2"
    if [[ -n "$payload" ]]; then
        printf '{"user_id":%s,"payload":"%s"}' "$uid" "$payload"
    else
        printf '{"user_id":%s}' "$uid"
    fi
}

# ── 发送单次请求 ──
send_request() {
    local body
    body=$(build_body "$USER_ID" "$PAYLOAD")

    local tmp
    tmp=$(mktemp)

    local http_code latency
    http_code=$(curl -s -o "$tmp" -w "%{http_code}\n%{time_total}" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$body" \
        --connect-timeout 5 \
        --max-time 30) || true

    local lines
    lines=$(printf '%s' "$http_code")
    http_code=$(echo "$lines" | head -1)
    latency=$(echo "$lines" | tail -1)
    local response
    response=$(cat "$tmp")
    rm -f "$tmp"

    printf '%s' "$response"
    echo ""
    echo "--- HTTP $http_code | latency: ${latency}s ---"
    printf '%s\n' "$latency"
}

# ── 单次请求模式 ──
if [[ "$REQUEST_COUNT" -eq 1 ]]; then
    echo ">>> Sending request to $URL"
    echo "    user_id=$USER_ID payload=$PAYLOAD"
    send_request
    exit 0
fi

# ── 批量请求模式 ──
echo ">>> Sending ${REQUEST_COUNT} requests to $URL"
echo "    user_id=$USER_ID"

latencies=()

for i in $(seq 1 "$REQUEST_COUNT"); do
    body=$(build_body "$USER_ID" "$PAYLOAD")

    tmp=$(mktemp)
    result=$(curl -s -o "$tmp" -w "%{http_code} %{time_total}" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$body" \
        --connect-timeout 5 \
        --max-time 30 2>/dev/null) || true

    http_code=$(echo "$result" | awk '{print $1}')
    latency=$(echo "$result" | awk '{print $2}')
    rm -f "$tmp"

    if [[ "$http_code" == "200" ]]; then
        printf "  [%3d/%d] OK  %.3fs\n" "$i" "$REQUEST_COUNT" "$latency"
    else
        printf "  [%3d/%d] FAIL (HTTP %s) %.3fs\n" "$i" "$REQUEST_COUNT" "$http_code" "$latency"
    fi
    latencies+=("$latency")
done

# ── 统计时延 ──
if [[ ${#latencies[@]} -eq 0 ]]; then
    echo "没有成功的请求"
    exit 1
fi

# 排序计算统计量
IFS=$'\n' sorted=($(sort -n <<<"${latencies[*]}")); unset IFS
count=${#sorted[@]}
sum=0
for v in "${sorted[@]}"; do
    sum=$(echo "$sum + $v" | bc)
done
avg=$(echo "scale=4; $sum / $count" | bc)
min=${sorted[0]}
max=${sorted[-1]}

# P99: 第 99 百分位
p99_idx=$(echo "scale=0; ($count * 99 + 50) / 100" | bc)
p99_idx=$((p99_idx < count ? p99_idx : count - 1))
p99=${sorted[$p99_idx]}

# P95
p95_idx=$(echo "scale=0; ($count * 95 + 50) / 100" | bc)
p95_idx=$((p95_idx < count ? p95_idx : count - 1))
p95=${sorted[$p95_idx]}

echo ""
echo "===== Latency Statistics (${count} requests) ====="
printf "  Min:    %.4fs\n" "$min"
printf "  Avg:    %.4fs\n" "$avg"
printf "  P95:    %.4fs\n" "$p95"
printf "  P99:    %.4fs\n" "$p99"
printf "  Max:    %.4fs\n" "$max"
