#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REGISTRY="192.168.84.245:5000"

IMAGES=(
    "linquickrec/etcd:latest"
    "linquickrec/discovery:latest"
    "linquickrec/kv-worker:latest"
    "linquickrec/feature:latest"
    "linquickrec/recall:latest"
    "linquickrec/precalc:latest"
    "linquickrec/rank-sub:latest"
    "linquickrec/rank-master:latest"
    "linquickrec/proxy:latest"
)

DRY_RUN=false
TARGET=""

log()  { echo "[$(date +%H:%M:%S)] $*"; }
err()  { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }

usage() {
    cat <<EOF
用法: $0 [选项] [镜像名]

将本地已有镜像推送到私仓 ${REGISTRY}
请先用 docker compose build 构建镜像。

选项:
  -n, --dry-run    仅预览，不执行推送
  -h, --help       显示帮助

镜像名（可选，不指定则处理全部）:
  etcd, discovery, kv-worker, feature, recall,
  precalc, rank-sub, rank-master, proxy

示例:
  $0                          # 推送全部已有镜像
  $0 recall                   # 仅推送 recall
  $0 --dry-run                # 预览全部操作
EOF
    exit 1
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -n|--dry-run) DRY_RUN=true; shift ;;
            -h|--help)    usage ;;
            -*)           echo "未知选项: $1"; usage ;;
            *)            TARGET="$1"; shift ;;
        esac
    done
}

filter_images() {
    if [[ -z "$TARGET" ]]; then
        return
    fi

    local filtered=()
    for img in "${IMAGES[@]}"; do
        local name="${img%%:*}"
        name="${name#linquickrec/}"
        if [[ "$name" == "$TARGET" ]]; then
            filtered+=("$img")
        fi
    done

    if [[ ${#filtered[@]} -eq 0 ]]; then
        err "未找到镜像: ${TARGET}"
        echo "可用镜像: etcd discovery kv-worker feature recall precalc rank-sub rank-master proxy" >&2
        exit 1
    fi

    IMAGES=("${filtered[@]}")
}

push_image() {
    local image="$1"
    local tagged="${REGISTRY}/${image}"

    log "--- ${image} ---"

    if ! docker image inspect "$image" >/dev/null 2>&1; then
        err "镜像不存在: ${image}，请先 docker compose build"
        return 1
    fi

    if $DRY_RUN; then
        log "[dry-run] docker tag ${image} ${tagged}"
        log "[dry-run] docker push ${tagged}"
        return
    fi

    log "标记 ${tagged} ..."
    docker tag "${image}" "${tagged}"

    log "推送 ${tagged} ..."
    docker push "${tagged}"

    log "完成 ${tagged}"
}

main() {
    parse_args "$@"
    filter_images

    log "===== 推送到 ${REGISTRY} ====="
    $DRY_RUN && log "(dry-run 模式)"
    log "镜像数量: ${#IMAGES[@]}"
    log ""

    local failed=0
    for img in "${IMAGES[@]}"; do
        push_image "$img" || ((failed++))
    done

    if [[ $failed -gt 0 ]]; then
        err "${failed} 个镜像推送失败"
        exit 1
    fi

    log "===== 全部完成 ====="
}

main "$@"
