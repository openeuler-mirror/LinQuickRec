#!/usr/bin/env bash
# Send requests to Proxy and optionally run a concurrent load test.
#
# Examples:
#   bash scripts/send_proxy_request.sh
#   bash scripts/send_proxy_request.sh -n 100 -c 10
#   bash scripts/send_proxy_request.sh --url=http://127.0.0.1:8080/Proxy/Recommend -n 100 -c 10
#   bash scripts/send_proxy_request.sh --k8s -n 1000 -c 50
#   bash scripts/send_proxy_request.sh --k8s --namespace=linquickrec --service=proxy-service

set -euo pipefail

PROXY_HOST="localhost"
PROXY_PORT="8080"
URL=""
USER_ID="12345"
PAYLOAD=""
REQUEST_COUNT=1
CONCURRENCY=1
CONNECT_TIMEOUT=5
MAX_TIME=30

K8S_MODE=false
K8S_NAMESPACE="linquickrec"
K8S_SERVICE="proxy-service"
K8S_SERVICE_PORT="8080"
KUBECTL="${KUBECTL:-kubectl}"
KUBE_PROXY_PORT="${KUBE_PROXY_PORT:-18001}"
KUBE_PROXY_PID=""
KUBE_PROXY_LOG=""

usage() {
    cat <<EOF
Usage: bash scripts/send_proxy_request.sh [options]

Target options:
  --host=HOST                 Proxy host for direct mode, default: ${PROXY_HOST}
  --port=PORT                 Proxy port for direct mode, default: ${PROXY_PORT}
  --url=URL                   Full Proxy Recommend URL, overrides --host/--port
  --k8s                       Send through Kubernetes Service via kubectl proxy
  --namespace=NAME            K8s namespace, default: ${K8S_NAMESPACE}
  --service=NAME              K8s Proxy Service name, default: ${K8S_SERVICE}
  --service-port=PORT         K8s Proxy Service port, default: ${K8S_SERVICE_PORT}
  --kube-proxy-port=PORT      Local kubectl proxy port, default: ${KUBE_PROXY_PORT}
  --kubectl=PATH              kubectl command, default: ${KUBECTL}

Request options:
  --user_id=ID                Request user_id, default: ${USER_ID}
  --payload=TEXT              Optional request payload
  -n, --requests=N            Total request count, default: ${REQUEST_COUNT}
  -c, --concurrency=N         Concurrent in-flight requests, default: ${CONCURRENCY}
  --connect-timeout=SECONDS   curl connect timeout, default: ${CONNECT_TIMEOUT}
  --max-time=SECONDS          curl max request time, default: ${MAX_TIME}
  -h, --help                  Show this help

Notes:
  Direct mode hits the configured URL from your machine.
  --k8s mode starts "kubectl proxy" locally and sends requests to the
  Kubernetes Service proxy URL. That lets Kubernetes route through the Service
  instead of pinning traffic to a single Proxy Pod.
EOF
}

die() {
    echo "ERROR: $*" >&2
    exit 1
}

cleanup() {
    if [[ -n "${KUBE_PROXY_PID}" ]]; then
        kill "${KUBE_PROXY_PID}" >/dev/null 2>&1 || true
        wait "${KUBE_PROXY_PID}" >/dev/null 2>&1 || true
    fi
    if [[ -n "${KUBE_PROXY_LOG}" ]]; then
        rm -f "${KUBE_PROXY_LOG}"
    fi
}
trap cleanup EXIT

parse_count_value() {
    local arg="$1"
    local next="${2:-}"
    local opt="$3"

    if [[ "$arg" == *=* ]]; then
        printf '%s' "${arg#*=}"
    elif [[ "$arg" == "$opt" ]]; then
        [[ -n "$next" ]] || die "Missing value for ${opt}"
        printf '%s' "$next"
    else
        printf '%s' "${arg#"$opt"}"
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host=*) PROXY_HOST="${1#*=}"; shift ;;
        --port=*) PROXY_PORT="${1#*=}"; shift ;;
        --url=*) URL="${1#*=}"; shift ;;
        --user_id=*) USER_ID="${1#*=}"; shift ;;
        --payload=*) PAYLOAD="${1#*=}"; shift ;;
        --k8s) K8S_MODE=true; shift ;;
        --namespace=*) K8S_NAMESPACE="${1#*=}"; shift ;;
        --service=*) K8S_SERVICE="${1#*=}"; shift ;;
        --service-port=*) K8S_SERVICE_PORT="${1#*=}"; shift ;;
        --kube-proxy-port=*) KUBE_PROXY_PORT="${1#*=}"; shift ;;
        --kubectl=*) KUBECTL="${1#*=}"; shift ;;
        --connect-timeout=*) CONNECT_TIMEOUT="${1#*=}"; shift ;;
        --max-time=*) MAX_TIME="${1#*=}"; shift ;;
        -n|--requests)
            REQUEST_COUNT="$(parse_count_value "$1" "${2:-}" "$1")"
            shift 2
            ;;
        --requests=*)
            REQUEST_COUNT="${1#*=}"
            shift
            ;;
        -n*)
            REQUEST_COUNT="${1#-n}"
            shift
            ;;
        -c|--concurrency)
            CONCURRENCY="$(parse_count_value "$1" "${2:-}" "$1")"
            shift 2
            ;;
        --concurrency=*)
            CONCURRENCY="${1#*=}"
            shift
            ;;
        -c*)
            CONCURRENCY="${1#-c}"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "Unknown argument: $1"
            ;;
    esac
done

[[ "$REQUEST_COUNT" =~ ^[0-9]+$ ]] || die "--requests/-n must be a positive integer"
[[ "$CONCURRENCY" =~ ^[0-9]+$ ]] || die "--concurrency/-c must be a positive integer"
[[ "$REQUEST_COUNT" -gt 0 ]] || die "--requests/-n must be > 0"
[[ "$CONCURRENCY" -gt 0 ]] || die "--concurrency/-c must be > 0"

json_escape() {
    local s="$1"
    s=${s//\\/\\\\}
    s=${s//\"/\\\"}
    s=${s//$'\n'/\\n}
    s=${s//$'\r'/\\r}
    s=${s//$'\t'/\\t}
    printf '%s' "$s"
}

build_body() {
    local uid="$1"
    local payload="$2"
    if [[ -n "$payload" ]]; then
        printf '{"user_id":%s,"payload":"%s"}' "$uid" "$(json_escape "$payload")"
    else
        printf '{"user_id":%s}' "$uid"
    fi
}

wait_for_url() {
    local url="$1"
    local deadline=$((SECONDS + 10))
    while [[ $SECONDS -lt $deadline ]]; do
        if curl -sS --max-time 1 "$url" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

start_kubectl_proxy() {
    command -v "$KUBECTL" >/dev/null 2>&1 || die "kubectl not found: ${KUBECTL}"

    KUBE_PROXY_LOG="$(mktemp)"
    "$KUBECTL" proxy --port="${KUBE_PROXY_PORT}" >"${KUBE_PROXY_LOG}" 2>&1 &
    KUBE_PROXY_PID=$!

    local health_url="http://127.0.0.1:${KUBE_PROXY_PORT}/version"
    if ! wait_for_url "$health_url"; then
        cat "${KUBE_PROXY_LOG}" >&2 || true
        die "kubectl proxy did not become ready on port ${KUBE_PROXY_PORT}"
    fi

    URL="http://127.0.0.1:${KUBE_PROXY_PORT}/api/v1/namespaces/${K8S_NAMESPACE}/services/${K8S_SERVICE}:${K8S_SERVICE_PORT}/proxy/Proxy/Recommend"
}

if $K8S_MODE; then
    start_kubectl_proxy
elif [[ -z "$URL" ]]; then
    URL="http://${PROXY_HOST}:${PROXY_PORT}/Proxy/Recommend"
fi

send_one() {
    local index="$1"
    local result_file="$2"
    local body tmp result http_code latency curl_status

    body=$(build_body "$USER_ID" "$PAYLOAD")
    tmp=$(mktemp)
    curl_status=0
    result=$(curl -s -o "$tmp" -w "%{http_code} %{time_total}" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$body" \
        --connect-timeout "$CONNECT_TIMEOUT" \
        --max-time "$MAX_TIME" 2>/dev/null) || curl_status=$?

    http_code=$(echo "$result" | awk '{print $1}')
    latency=$(echo "$result" | awk '{print $2}')
    rm -f "$tmp"

    [[ -n "$http_code" ]] || http_code="000"
    [[ -n "$latency" ]] || latency="0"

    printf '%s %s %s %s\n' "$index" "$http_code" "$latency" "$curl_status" >"$result_file"
}

print_single_response() {
    local body tmp result http_code latency curl_status response

    body=$(build_body "$USER_ID" "$PAYLOAD")
    tmp=$(mktemp)
    curl_status=0
    result=$(curl -s -o "$tmp" -w "%{http_code} %{time_total}" \
        -X POST "$URL" \
        -H "Content-Type: application/json" \
        -d "$body" \
        --connect-timeout "$CONNECT_TIMEOUT" \
        --max-time "$MAX_TIME") || curl_status=$?

    http_code=$(echo "$result" | awk '{print $1}')
    latency=$(echo "$result" | awk '{print $2}')
    response=$(cat "$tmp")
    rm -f "$tmp"

    printf '%s\n' "$response"
    echo "--- HTTP ${http_code:-000} | curl_status: ${curl_status} | latency: ${latency:-0}s ---"
}

print_stats() {
    local results_file="$1"
    local total ok fail

    total=$(wc -l <"$results_file" | tr -d ' ')
    ok=$(awk '$2 == "200" { c++ } END { print c + 0 }' "$results_file")
    fail=$((total - ok))

    echo ""
    echo "===== Result Summary ====="
    printf "  Total:       %d\n" "$total"
    printf "  OK:          %d\n" "$ok"
    printf "  Failed:      %d\n" "$fail"
    printf "  Concurrency: %d\n" "$CONCURRENCY"

    if [[ "$ok" -eq 0 ]]; then
        echo "  No successful HTTP 200 requests; latency statistics skipped."
        return
    fi

    awk '$2 == "200" { print $3 }' "$results_file" | sort -n | awk '
        { vals[NR] = $1; sum += $1 }
        END {
            count = NR
            p95_idx = int((count * 95 + 50) / 100)
            p99_idx = int((count * 99 + 50) / 100)
            if (p95_idx < 1) p95_idx = 1
            if (p99_idx < 1) p99_idx = 1
            if (p95_idx > count) p95_idx = count
            if (p99_idx > count) p99_idx = count
            printf "  Min:         %.4fs\n", vals[1]
            printf "  Avg:         %.4fs\n", sum / count
            printf "  P95:         %.4fs\n", vals[p95_idx]
            printf "  P99:         %.4fs\n", vals[p99_idx]
            printf "  Max:         %.4fs\n", vals[count]
        }'
}

echo ">>> Target: $URL"
echo "    user_id=$USER_ID requests=$REQUEST_COUNT concurrency=$CONCURRENCY"
if $K8S_MODE; then
    echo "    k8s namespace=$K8S_NAMESPACE service=$K8S_SERVICE port=$K8S_SERVICE_PORT"
fi

if [[ "$REQUEST_COUNT" -eq 1 && "$CONCURRENCY" -eq 1 ]]; then
    print_single_response
    exit 0
fi

if [[ "$CONCURRENCY" -gt "$REQUEST_COUNT" ]]; then
    CONCURRENCY="$REQUEST_COUNT"
fi

RESULT_DIR=$(mktemp -d)
RESULTS_FILE="${RESULT_DIR}/results.txt"
SEM="${RESULT_DIR}/sem"
mkfifo "$SEM"
exec 9<>"$SEM"
rm -f "$SEM"

for _ in $(seq 1 "$CONCURRENCY"); do
    printf '.\n' >&9
done

PIDS=()
for i in $(seq 1 "$REQUEST_COUNT"); do
    read -r -u 9
    {
        send_one "$i" "${RESULT_DIR}/${i}.txt"
        printf '.\n' >&9
    } &
    PIDS+=("$!")
done

for pid in "${PIDS[@]}"; do
    wait "$pid"
done
exec 9>&-

cat "${RESULT_DIR}"/*.txt | sort -n -k1,1 >"$RESULTS_FILE"

while read -r index http_code latency curl_status; do
    if [[ "$http_code" == "200" ]]; then
        printf "  [%3d/%d] OK   %.3fs\n" "$index" "$REQUEST_COUNT" "$latency"
    else
        printf "  [%3d/%d] FAIL HTTP=%s curl=%s %.3fs\n" \
            "$index" "$REQUEST_COUNT" "$http_code" "$curl_status" "$latency"
    fi
done <"$RESULTS_FILE"

print_stats "$RESULTS_FILE"
rm -rf "$RESULT_DIR"
