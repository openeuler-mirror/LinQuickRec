#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NAMESPACE="linquickrec"
OVERLAY_DIR="${SCRIPT_DIR}"

RECALL_MODE="novllm"
DISCOVERY_BACKEND="etcd"
REGISTRY=""
ACTION=""
IMAGE_TAG="latest"

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
  -r, --registry <host:port>               Private registry host:port (e.g. 192.168.0.1:5000)
                                         Default: no registry, images pulled from local
  -h, --help                             Show this help

Examples:
  $(basename "$0") start
  $(basename "$0") start -r 192.168.0.1:5000
  $(basename "$0") start --recall-mode vllm -r 192.168.0.1:5000
  $(basename "$0") start --discovery-backend discovery-server
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
            -r|--registry)
                if [[ $# -lt 2 ]]; then die "Missing value for --registry"; fi
                REGISTRY="${2%/}"; shift 2 ;;
            *) die "Unknown option: $1" ;;
        esac
    done
    if [[ -z "$ACTION" ]]; then die "Missing action (start|stop|delete)"; fi
}

prepare_pv() {
    local gen_dir="${SCRIPT_DIR}/.generated"
    mkdir -p "$gen_dir"

    local nodes
    nodes=($(kubectl get nodes -o jsonpath='{.items[*].metadata.name}' 2>/dev/null || true))
    export NODE_0="${nodes[0]:-master}"
    export NODE_1="${nodes[1]:-${NODE_0}}"

    envsubst < "${SCRIPT_DIR}/base/etcd-pv.yaml.tmpl" > "${gen_dir}/etcd-pv.yaml"
    log "PV generated for nodes: ${NODE_0}, ${NODE_1}"
}

generate_kustomize_overlay() {
    cat > "$OVERLAY_DIR/kustomization.yaml" <<KUSTOMIZE
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: ${NAMESPACE}

resources:
  - base/namespace.yaml
  - base/configmap.yaml
  - .generated/etcd-pv.yaml
  - base/etcd.yaml
  - base/kv-worker.yaml
  - base/feature.yaml
  - base/proxy.yaml
  - base/precalc.yaml
  - base/rank-master.yaml
  - base/rank-sub.yaml
KUSTOMIZE

    if [ "$DISCOVERY_BACKEND" = "discovery-server" ]; then
        cat >> "$OVERLAY_DIR/kustomization.yaml" <<COMP
  - components/discovery/deployment.yaml
  - components/discovery/service.yaml
COMP
    fi

    if [ "$RECALL_MODE" = "vllm" ]; then
        cat >> "$OVERLAY_DIR/kustomization.yaml" <<COMP
  - components/recall-vllm/deployment.yaml
  - components/recall-vllm/service.yaml
COMP
    else
        cat >> "$OVERLAY_DIR/kustomization.yaml" <<COMP
  - components/recall-novllm/deployment.yaml
  - components/recall-novllm/service.yaml
COMP
    fi

    # Image registry override (only if --registry specified)
    if [[ -n "$REGISTRY" ]]; then
        cat >> "$OVERLAY_DIR/kustomization.yaml" <<IMAGES

images:
IMAGES
        for img in proxy feature recall precalc rank-master rank-sub kv-worker etcd discovery; do
            cat >> "$OVERLAY_DIR/kustomization.yaml" <<LINE
  - name: linquickrec/${img}
    newName: ${REGISTRY}/linquickrec/${img}
LINE
        done
    fi
}

do_start() {
    log "=========================================="
    log "Deploying LinQuickRec"
    log "  recall mode:       ${RECALL_MODE}"
    log "  discovery backend: ${DISCOVERY_BACKEND}"
    log "  registry:          ${REGISTRY:-local}"
    log "=========================================="

    prepare_pv
    generate_kustomize_overlay
    kubectl apply -k "$OVERLAY_DIR"

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
    prepare_pv
    generate_kustomize_overlay
    kubectl delete -k "$OVERLAY_DIR" --ignore-not-found
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
