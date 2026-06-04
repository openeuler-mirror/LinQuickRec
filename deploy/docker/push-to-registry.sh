#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
COMPOSE_FILE="${SCRIPT_DIR}/docker-compose.yml"

REGISTRY="${REGISTRY:-192.168.84.245:5000}"
DRY_RUN=false
BUILD_ONLY=false
PUSH_ONLY=false
TARGET=""

SERVICES=(
    "etcd:etcd:linquickrec/etcd:latest"
    "discovery:discovery-server:linquickrec/discovery:latest"
    "kv-worker:kv-worker:linquickrec/kv-worker:latest"
    "feature:feature-service:linquickrec/feature:latest"
    "recall:recall-service:linquickrec/recall:latest"
    "precalc:precalc-service:linquickrec/precalc:latest"
    "rank-sub:rank-sub-service:linquickrec/rank-sub:latest"
    "rank-master:rank-master-service:linquickrec/rank-master:latest"
    "proxy:proxy-service:linquickrec/proxy:latest"
)

ALIASES=(
    "discovery-server:discovery"
    "kv_worker:kv-worker"
    "kv:kv-worker"
    "feature-service:feature"
    "recall-service:recall"
    "precalc-service:precalc"
    "rank_sub:rank-sub"
    "rank-sub-service:rank-sub"
    "rank_master:rank-master"
    "rank-master-service:rank-master"
    "proxy-service:proxy"
)

log() { echo "[$(date +%H:%M:%S)] $*"; }
err() { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }

usage() {
    cat <<EOF
Usage: $(basename "$0") [options] [image]

Build Docker image(s), tag them with the private registry, and push them.

Options:
  -r, --registry REGISTRY  Private registry, default: ${REGISTRY}
  -n, --dry-run            Print commands without running them
      --build-only         Build images but do not push
      --push-only          Push existing local images without building
  -h, --help               Show this help

Images:
  all, etcd, discovery, kv-worker, feature, recall,
  precalc, rank-sub, rank-master, proxy

Examples:
  $(basename "$0")                    # build and push all images
  $(basename "$0") recall             # build and push recall only
  $(basename "$0") --registry 10.0.0.1:5000 rank-sub
  $(basename "$0") --dry-run all
EOF
}

run_cmd() {
    if $DRY_RUN; then
        log "[dry-run] $*"
        return 0
    fi
    "$@"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -r|--registry)
                if [[ $# -lt 2 ]]; then
                    err "Missing value for $1"
                    usage
                    exit 1
                fi
                REGISTRY="$2"
                shift 2
                ;;
            -n|--dry-run)
                DRY_RUN=true
                shift
                ;;
            --build-only)
                BUILD_ONLY=true
                shift
                ;;
            --push-only)
                PUSH_ONLY=true
                shift
                ;;
            -h|--help)
                usage
                exit 0
                ;;
            -*)
                err "Unknown option: $1"
                usage
                exit 1
                ;;
            *)
                if [[ -n "$TARGET" ]]; then
                    err "Only one image target is supported; got '${TARGET}' and '$1'"
                    usage
                    exit 1
                fi
                TARGET="$1"
                shift
                ;;
        esac
    done

    if $BUILD_ONLY && $PUSH_ONLY; then
        err "--build-only and --push-only cannot be used together"
        exit 1
    fi
}

canonical_target() {
    local target="$1"

    if [[ -z "$target" || "$target" == "all" ]]; then
        echo "all"
        return 0
    fi

    for row in "${SERVICES[@]}"; do
        IFS=: read -r name _ _ _ <<<"$row"
        if [[ "$target" == "$name" ]]; then
            echo "$name"
            return 0
        fi
    done

    for alias_row in "${ALIASES[@]}"; do
        IFS=: read -r alias canonical <<<"$alias_row"
        if [[ "$target" == "$alias" ]]; then
            echo "$canonical"
            return 0
        fi
    done

    err "Unknown image target: ${target}"
    echo "Available images: etcd discovery kv-worker feature recall precalc rank-sub rank-master proxy" >&2
    exit 1
}

selected_services() {
    local target
    target="$(canonical_target "$TARGET")"

    for row in "${SERVICES[@]}"; do
        IFS=: read -r name _ _ _ <<<"$row"
        if [[ "$target" == "all" || "$target" == "$name" ]]; then
            echo "$row"
        fi
    done
}

build_image() {
    local service="$1"

    log "Building compose service: ${service}"
    run_cmd docker compose -f "$COMPOSE_FILE" build "$service"
}

push_image() {
    local image="$1"
    local tagged="${REGISTRY}/${image}"

    if ! $DRY_RUN && ! docker image inspect "$image" >/dev/null 2>&1; then
        err "Local image not found after build: ${image}"
        return 1
    fi

    log "Tagging ${image} -> ${tagged}"
    run_cmd docker tag "$image" "$tagged"

    log "Pushing ${tagged}"
    run_cmd docker push "$tagged"
}

process_one() {
    local row="$1"
    local name service image_tag image_version image

    IFS=: read -r name service image_tag image_version <<<"$row"
    image="${image_tag}:${image_version}"

    log "===== ${name} ====="

    if ! $PUSH_ONLY; then
        build_image "$service" || return 1
    fi

    if ! $BUILD_ONLY; then
        push_image "$image" || return 1
    fi
}

main() {
    parse_args "$@"

    log "Repository root: ${REPO_ROOT}"
    log "Compose file: ${COMPOSE_FILE}"
    log "Registry: ${REGISTRY}"
    $DRY_RUN && log "Mode: dry-run"
    $BUILD_ONLY && log "Mode: build-only"
    $PUSH_ONLY && log "Mode: push-only"

    local failed=0
    local count=0
    while IFS= read -r row; do
        [[ -z "$row" ]] && continue
        ((count+=1))
        if ! process_one "$row"; then
            ((failed+=1))
        fi
    done < <(selected_services)

    if [[ $count -eq 0 ]]; then
        err "No image selected"
        exit 1
    fi

    if [[ $failed -gt 0 ]]; then
        err "${failed} image(s) failed"
        exit 1
    fi

    log "All done (${count} image(s))"
}

main "$@"
