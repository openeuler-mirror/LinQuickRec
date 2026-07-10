#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NAMESPACE="linquickrec"
OVERLAY_DIR="${SCRIPT_DIR}"

RECALL_MODE="novllm"
DISCOVERY_BACKEND="etcd"
REGISTRY=""
ACTION=""
SERVICE=""
REPLICAS=""
IMAGE_TAG="latest"

log()  { echo "[$(date +%H:%M:%S)] $*"; }
err()  { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }
die()  { err "$@"; exit 1; }

# service 映射: type:name:default_replicas
declare -A SVC_TYPE SVC_NAME SVC_REPLICAS
_svc() { local n=$1; SVC_TYPE[$n]=$2; SVC_NAME[$n]=$3; SVC_REPLICAS[$n]=$4; }
_svc etcd     statefulset etcd                                  5
_svc feature  deployment  feature-service                       1
_svc proxy    deployment  proxy-service                         1
_svc precalc  deployment  precalc-and-rank-master-service       3
_svc recall   deployment  recall-service                        2
_svc rank-sub deployment  rank-sub-service                      3
_svc perf     deployment  perf-collector-service                1
unset -f _svc

usage() {
    cat <<EOF
Usage: $(basename "$0") <start|stop|delete|restart> [service] [options]

Actions:
  start        Deploy all services / scale a service to its replicas
  stop         Scale all workloads to 0 / scale a service to 0 (preserve definitions)
  restart      Stop then start (--all) / rollout restart (single service)
  delete       Delete all resources / a single service

Service names: etcd feature proxy precalc recall rank-sub perf

Options:
  --recall-mode vllm|novllm              Recall mode (default: novllm, --all only)
  --discovery-backend etcd|discovery-server  Discovery backend (--all only)
  --replicas <N>                         Override default replicas (start <service> only)
  -r, --registry <host:port>             Private registry (--all only)
  -h, --help

Examples:
  $(basename "$0") start                         # deploy everything
  $(basename "$0") start -r 192.168.0.1:5000     # deploy with registry
  $(basename "$0") stop                           # scale all to 0
  $(basename "$0") stop feature                   # scale feature-service to 0
  $(basename "$0") start feature                  # scale feature-service to 1
  $(basename "$0") start recall --replicas 3      # scale recall-service to 3
  $(basename "$0") restart perf                   # rollout restart perf-collector
  $(basename "$0") delete proxy                   # delete proxy-service
  $(basename "$0") delete                         # delete everything
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
            start|stop|delete|restart)
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
            --replicas)
                if [[ $# -lt 2 ]]; then die "Missing value for --replicas"; fi
                if [[ ! "$2" =~ ^[0-9]+$ ]]; then die "Invalid replicas: $2 (must be a number)"; fi
                REPLICAS="$2"; shift 2 ;;
            -r|--registry)
                if [[ $# -lt 2 ]]; then die "Missing value for --registry"; fi
                REGISTRY="${2%/}"; shift 2 ;;
            etcd|feature|proxy|precalc|recall|rank-sub|perf)
                if [[ -n "$SERVICE" ]]; then die "Only one service allowed, got '${SERVICE}' and '$1'"; fi
                SERVICE="$1"; shift ;;
            *) die "Unknown option: $1" ;;
        esac
    done
    if [[ -z "$ACTION" ]]; then die "Missing action (start|stop|delete|restart)"; fi
}

generate_kustomize_overlay() {
    cat > "$OVERLAY_DIR/kustomization.yaml" <<KUSTOMIZE
apiVersion: kustomize.config.k8s.io/v1beta1
kind: Kustomization
namespace: ${NAMESPACE}

resources:
  - base/namespace.yaml
  - base/configmap.yaml
  - base/etcd-headless-svc.yaml
  - base/etcd-client-svc.yaml
  - base/etcd-statefulset.yaml
  - base/kv-worker.yaml
  - base/feature.yaml
  - base/proxy.yaml
  - base/precalc-and-rank-master.yaml
  - base/perf-collector.yaml
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
        for img in base proxy feature recall precalc-and-rank-master perf-collector rank-sub kv-worker etcd discovery; do
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
    kubectl delete daemonset --all -n "${NAMESPACE}" 2>/dev/null || true
    log "All workloads scaled to 0"
}

do_delete() {
    log "Deleting all resources in ${NAMESPACE}..."
    generate_kustomize_overlay
    kubectl delete -k "$OVERLAY_DIR" --ignore-not-found
    log "All resources deleted"
}

svc_action() {
    local type="${SVC_TYPE[$SERVICE]}"
    local name="${SVC_NAME[$SERVICE]}"
    local replicas="${REPLICAS:-${SVC_REPLICAS[$SERVICE]}}"

    [[ -n "$type" ]] || die "Unknown service: ${SERVICE}. Valid: ${!SVC_TYPE[*]}"

    log "Action: ${ACTION}  Service: ${SERVICE} (${type}/${name})"

    case "$ACTION" in
        stop)
            if [[ "$type" == "daemonset" ]]; then
                kubectl delete daemonset "$name" -n "${NAMESPACE}" 2>/dev/null || true
            else
                kubectl scale "$type" "$name" --replicas=0 -n "${NAMESPACE}"
            fi
            log "${name} scaled to 0"
            ;;
        start)
            if [[ "$type" == "daemonset" ]]; then
                log "DaemonSet cannot be started via scale; use apply"
                return 1
            fi
            kubectl scale "$type" "$name" --replicas="$replicas" -n "${NAMESPACE}"
            log "${name} scaled to ${replicas}"
            ;;
        restart)
            if [[ "$type" == "daemonset" ]]; then
                kubectl delete daemonset "$name" -n "${NAMESPACE}" 2>/dev/null || true
                log "${name} deleted (re-apply to recreate)"
            else
                kubectl rollout restart "$type" "$name" -n "${NAMESPACE}"
                log "${name} rollout restarted"
            fi
            ;;
        delete)
            kubectl delete "$type" "$name" -n "${NAMESPACE}" --ignore-not-found
            log "${name} deleted"
            ;;
    esac
}

main() {
    parse_args "$@"
    check_prereqs

    if [[ -n "$SERVICE" ]]; then
        svc_action
        return
    fi

    case "$ACTION" in
        start)  do_start ;;
        stop)   do_stop ;;
        restart) do_stop; do_start ;;
        delete) do_delete ;;
    esac
}

main "$@"
