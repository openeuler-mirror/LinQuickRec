#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
NAMESPACE="linquickrec"

# 公共资源（两种模式都需要）
COMMON_FILES=(
    "00-namespace.yaml"
    "01-configmap.yaml"
    "04-kv-worker.yaml"
    "05-feature.yaml"
    "06-proxy.yaml"
    "07-recall.yaml"
    "08-precalc.yaml"
    "09-rank-master.yaml"
    "10-rank-sub.yaml"
)

# 模式专属资源
declare -A MODE_FILES
MODE_FILES["etcd"]="02-etcd.yaml"
MODE_FILES["discovery"]="03-discovery.yaml"

# 逆序删除
DELETE_FILES=(
    "10-rank-sub.yaml"
    "09-rank-master.yaml"
    "08-precalc.yaml"
    "07-recall.yaml"
    "06-proxy.yaml"
    "05-feature.yaml"
    "04-kv-worker.yaml"
    "03-discovery.yaml"
    "02-etcd.yaml"
    "01-configmap.yaml"
    "00-namespace.yaml"
)

log()  { echo "[$(date +%H:%M:%S)] $*"; }
err()  { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }

die()  { err "$@"; exit 1; }

check_prereqs() {
    command -v kubectl >/dev/null 2>&1 || die "kubectl 未安装或不在 PATH 中"
    kubectl cluster-info >/dev/null 2>&1 || die "无法连接 K8s 集群，请检查 kubectl 配置"
    log "kubectl 可用，集群连通正常"
}

apply_mode() {
    local mode="$1"
    local mode_file="${MODE_FILES[$mode]}"

    log "===== ${mode} 模式部署 ====="

    for f in "${COMMON_FILES[@]}"; do
        # 在 discovery 模式下插入专属文件（排在 namespace/configmap 之后、服务之前）
        if [ "$f" = "04-kv-worker.yaml" ] && [ -n "$mode_file" ]; then
            log "apply ${mode_file}"
            kubectl apply -f "${SCRIPT_DIR}/${mode_file}"
        fi

        log "apply ${f}"
        kubectl apply -f "${SCRIPT_DIR}/${f}"
    done

    log "===== 部署完成 ====="
    kubectl get pods -n "${NAMESPACE}" -o wide
}

do_delete() {
    log "===== 删除所有资源 ====="

    for f in "${DELETE_FILES[@]}"; do
        log "delete ${f}"
        kubectl delete -f "${SCRIPT_DIR}/${f}" --ignore-not-found
    done

    log "===== 删除完成 ====="
}

usage() {
    cat <<EOF
用法: $0 <etcd|discovery|delete>

  etcd          etcd 模式：部署集群内 etcd (5 副本) + 所有服务
  discovery     discovery_server 模式：部署 discovery-server + 所有服务
  delete        逆序删除全部资源
EOF
    exit 1
}

main() {
    [ $# -eq 1 ] || usage

    check_prereqs

    case "$1" in
        etcd|discovery) apply_mode "$1" ;;
        delete)         do_delete ;;
        *)             usage ;;
    esac
}

main "$@"
