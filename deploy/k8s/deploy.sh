#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NAMESPACE="linquickrec"

RECALL_MODE="novllm"
DISCOVERY_BACKEND="etcd"
ACTION=""

log()  { echo "[$(date +%H:%M:%S)] $*"; }
err()  { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }
die()  { err "$@"; exit 1; }

usage() {
    cat <<EOF
Usage: $(basename "$0") <start|stop|delete> [options]

Actions:
  start        Deploy all services to namespace ${NAMESPACE}
  stop         Scale all deployments and statefulsets to 0 (preserve definitions)
  delete       Delete all resources in reverse order

Options:
  --recall-mode vllm|novllm              Recall deployment mode (default: novllm)
  --discovery-backend etcd|discovery-server  Service discovery backend (default: etcd)
  -h, --help                             Show this help

Examples:
  $(basename "$0") start --recall-mode novllm
  $(basename "$0") start --recall-mode vllm --discovery-backend discovery-server
  $(basename "$0") stop
  $(basename "$0") delete
EOF
    exit 0
}

check_prereqs() {
    command -v kubectl >/dev/null 2>&1 || die "kubectl not found"
    kubectl cluster-info >/dev/null 2>&1 || die "cannot connect to K8s cluster"
    log "kubectl OK, cluster reachable"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) usage ;;
            start|stop|delete)
                if [[ -n "$ACTION" ]]; then die "Only one action allowed, got '${ACTION}' and '$1'"; fi
                ACTION="$1"; shift ;;
            --recall-mode)
                if [[ $# -lt 2 ]]; then die "Missing value for --recall-mode"; fi
                case "$2" in
                    vllm|novllm) RECALL_MODE="$2"; shift 2 ;;
                    *) die "Invalid recall mode: $2 (expected vllm or novllm)" ;;
                esac ;;
            --discovery-backend)
                if [[ $# -lt 2 ]]; then die "Missing value for --discovery-backend"; fi
                case "$2" in
                    etcd|discovery-server) DISCOVERY_BACKEND="$2"; shift 2 ;;
                    *) die "Invalid discovery backend: $2 (expected etcd or discovery-server)" ;;
                esac ;;
            *) die "Unknown option: $1" ;;
        esac
    done
    if [[ -z "$ACTION" ]]; then die "Missing action (start|stop|delete)"; fi
}

do_start() {
    local recall_file="07-recall-${RECALL_MODE}.yaml"

    log "=========================================="
    log "Deploying LinQuickRec"
    log "  recall mode:       ${RECALL_MODE}"
    log "  discovery backend: ${DISCOVERY_BACKEND}"
    log "=========================================="

    kubectl apply -f "${SCRIPT_DIR}/00-namespace.yaml"
    kubectl apply -f "${SCRIPT_DIR}/01-configmap.yaml"
    kubectl apply -f "${SCRIPT_DIR}/02-etcd-pv.yaml"
    kubectl apply -f "${SCRIPT_DIR}/02-etcd.yaml"
    if [ "$DISCOVERY_BACKEND" = "discovery-server" ]; then
        kubectl apply -f "${SCRIPT_DIR}/03-discovery.yaml"
    fi
    kubectl apply -f "${SCRIPT_DIR}/04-kv-worker.yaml"
    kubectl apply -f "${SCRIPT_DIR}/05-feature.yaml"
    kubectl apply -f "${SCRIPT_DIR}/06-proxy.yaml"
    kubectl apply -f "${SCRIPT_DIR}/${recall_file}"
    kubectl apply -f "${SCRIPT_DIR}/08-precalc.yaml"
    kubectl apply -f "${SCRIPT_DIR}/09-rank-master.yaml"
    kubectl apply -f "${SCRIPT_DIR}/10-rank-sub.yaml"

    log "=========================================="
    log "Deployment complete"
    kubectl get pods -n "${NAMESPACE}" -o wide
}

do_stop() {
    log "Stopping all workloads in ${NAMESPACE}..."
    kubectl scale deployment --all --replicas=0 -n "${NAMESPACE}" 2>/dev/null || true
    kubectl scale statefulset --all --replicas=0 -n "${NAMESPACE}" 2>/dev/null || true
    log "All workloads scaled to 0"
}

do_delete() {
    log "Deleting all resources in ${NAMESPACE}..."
    kubectl delete -f "${SCRIPT_DIR}/10-rank-sub.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/09-rank-master.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/08-precalc.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/07-recall-vllm.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/07-recall-novllm.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/06-proxy.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/05-feature.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/04-kv-worker.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/03-discovery.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/02-etcd.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/02-etcd-pv.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/01-configmap.yaml" --ignore-not-found
    kubectl delete -f "${SCRIPT_DIR}/00-namespace.yaml" --ignore-not-found
    log "All resources deleted"
}

main() {
    parse_args "$@"
    check_prereqs

    case "$ACTION" in
        start)  do_start ;;
        stop)   do_stop ;;
        delete) do_delete ;;
    esac
}

main "$@"
