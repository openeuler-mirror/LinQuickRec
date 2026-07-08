#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

trap 'log "Interrupted"; exit 130' INT

REGISTRY=""
DRY_RUN=false
TARGET=""

SERVICES=(
    "etcd:linquickrec/etcd:latest"
    "base:linquickrec/base:latest"
    "discovery:linquickrec/discovery:latest"
    "kv-worker:linquickrec/kv-worker:latest"
    "feature:linquickrec/feature:latest"
    "recall:linquickrec/recall:latest"
    "precalc-and-rank-master:linquickrec/precalc-and-rank-master:latest"
    "rank-sub:linquickrec/rank-sub:latest"
    "proxy:linquickrec/proxy:latest"
)

ALIASES=(
    "discovery-server:discovery"
    "kv_worker:kv-worker"
    "kv:kv-worker"
    "feature-service:feature"
    "recall-service:recall"
    "precalc-and-rank-master:precalc-and-rank-master"
    "rank_sub:rank-sub"
    "rank-sub-service:rank-sub"
    "proxy-service:proxy"
)

log() { echo "[$(date +%H:%M:%S)] $*"; }
err() { echo "[$(date +%H:%M:%S)] ERROR: $*" >&2; }

usage() {
    cat <<EOF
Usage: $(basename "$0") -r REGISTRY [image] [options]

Tag pre-built local Docker image(s) with the private registry and push them.
If image is omitted, defaults to all.

Options:
  -r, --registry REGISTRY  Private registry address (required)
      --dry-run            Print commands without running them
  -h, --help               Show this help

Images:
  all, base, etcd, discovery, kv-worker, feature, recall,
  precalc-and-rank-master, rank-sub, proxy

Examples:
  $(basename "$0") -r 192.168.0.1:5000                   # push all images (default)
  $(basename "$0") -r 192.168.0.1:5000 recall            # push recall only
  $(basename "$0") -r 192.168.0.1:5000 --dry-run all     # dry-run
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
            --dry-run)
                DRY_RUN=true
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
}

canonical_target() {
    local target="$1"

    if [[ -z "$target" || "$target" == "all" ]]; then
        echo "all"
        return 0
    fi

    for row in "${SERVICES[@]}"; do
        IFS=: read -r name _ <<<"$row"
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
    echo "Available images: base etcd discovery kv-worker feature recall precalc-and-rank-master rank-sub proxy" >&2
    exit 1
}

selected_services() {
    local target
    target="$(canonical_target "$TARGET")"

    for row in "${SERVICES[@]}"; do
        IFS=: read -r name _ <<<"$row"
        if [[ "$target" == "all" || "$target" == "$name" ]]; then
            echo "$row"
        fi
    done
}

push_image() {
    local image="$1"
    local tagged="${REGISTRY}/${image}"

    if ! $DRY_RUN && ! docker image inspect "$image" >/dev/null 2>&1; then
        err "Local image not found: ${image}"
        return 1
    fi

    log "Tagging ${image} -> ${tagged}"
    run_cmd docker tag "$image" "$tagged"

    log "Pushing ${tagged}"
    run_cmd docker push "$tagged"
}

process_one() {
    local row="$1"
    local name image

    IFS=: read -r name image <<<"$row"

    log "===== ${name} ====="
    push_image "$image" || return 1
}

main() {
    parse_args "$@"

    if [[ -z "$REGISTRY" ]]; then
        err "REGISTRY is required, use -r/--registry"
        exit 1
    fi

    log "Registry: ${REGISTRY}"
    $DRY_RUN && log "Mode: dry-run"

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
