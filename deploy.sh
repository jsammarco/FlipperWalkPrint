#!/usr/bin/env bash
set -euo pipefail

APP_ID="walkprint"
APP_FILENAME="WalkPrint.fap"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
DEFAULT_FAP_PATH="${ROOT_DIR}/dist/${APP_ID}.fap"
MOUNT_PATH=""
QFLIPPER_BIN="${QFLIPPER_BIN:-}"
FAP_PATH="$DEFAULT_FAP_PATH"

usage() {
    cat <<'EOF'
Usage: ./deploy.sh [--mount PATH] [--fap PATH] [--qflipper BIN]

Deploys the built WalkPrint.fap either to a mounted Flipper SD card or through
the qFlipper CLI if it is installed and exposes the expected storage commands.

Options:
  --mount PATH     Mounted SD-card root to copy into.
  --fap PATH       Explicit .fap path. Defaults to ./dist/walkprint.fap.
  --qflipper BIN   Explicit qFlipper binary/AppImage path.
  -h, --help       Show this help text.
EOF
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

log() {
    printf '[deploy] %s\n' "$*"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --mount)
                [[ $# -ge 2 ]] || die "--mount requires a value"
                MOUNT_PATH="$2"
                shift 2
                ;;
            --fap)
                [[ $# -ge 2 ]] || die "--fap requires a value"
                FAP_PATH="$2"
                shift 2
                ;;
            --qflipper)
                [[ $# -ge 2 ]] || die "--qflipper requires a value"
                QFLIPPER_BIN="$2"
                shift 2
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
}

ensure_fap_exists() {
    if [[ ! -f "$FAP_PATH" ]]; then
        log "Missing ${FAP_PATH}; running build.sh first"
        "${ROOT_DIR}/build.sh"
    fi

    [[ -f "$FAP_PATH" ]] || die "Built .fap not found at ${FAP_PATH}"
}

discover_qflipper() {
    local candidate
    if [[ -n "$QFLIPPER_BIN" ]]; then
        return
    fi

    for candidate in qFlipper qflipper; do
        if command -v "$candidate" >/dev/null 2>&1; then
            QFLIPPER_BIN="$(command -v "$candidate")"
            return
        fi
    done
}

deploy_to_mount() {
    local target_dir="${MOUNT_PATH}/apps/Tools"
    [[ -d "$MOUNT_PATH" ]] || die "Mount path does not exist: ${MOUNT_PATH}"

    mkdir -p "$target_dir"
    cp "$FAP_PATH" "${target_dir}/${APP_FILENAME}"
    log "Copied ${APP_FILENAME} to ${target_dir}"
}

deploy_with_qflipper() {
    [[ -x "$QFLIPPER_BIN" || -f "$QFLIPPER_BIN" ]] || die "qFlipper binary not found: ${QFLIPPER_BIN}"

    log "Trying qFlipper CLI deployment via ${QFLIPPER_BIN}"

    if ! "$QFLIPPER_BIN" cli storage mkdir /ext/apps >/dev/null 2>&1; then
        log "mkdir /ext/apps returned a non-zero status; continuing"
    fi

    if ! "$QFLIPPER_BIN" cli storage mkdir /ext/apps/Tools >/dev/null 2>&1; then
        log "mkdir /ext/apps/Tools returned a non-zero status; continuing"
    fi

    "$QFLIPPER_BIN" cli storage send "$FAP_PATH" "/ext/apps/Tools/${APP_FILENAME}"
    log "Uploaded ${APP_FILENAME} to /ext/apps/Tools via qFlipper"
}

main() {
    parse_args "$@"
    ensure_fap_exists

    if [[ -n "$MOUNT_PATH" ]]; then
        deploy_to_mount
        exit 0
    fi

    discover_qflipper
    if [[ -n "$QFLIPPER_BIN" ]]; then
        deploy_with_qflipper
        exit 0
    fi

    die "No mount path provided and qFlipper CLI was not found. Use --mount PATH or --qflipper BIN."
}

main "$@"
