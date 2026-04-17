#!/usr/bin/env bash
set -euo pipefail

APP_ID="walkprint"
APP_FOLDER_NAME="walkprint"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
DIST_DIR="${ROOT_DIR}/dist"
FW_PATH="${FLIPPER_FW_PATH:-}"
USE_COPY=0

usage() {
    cat <<'EOF'
Usage: ./build.sh [--fw-path PATH] [--copy]

Builds the WalkPrint external app against a local Flipper firmware checkout.

Options:
  --fw-path PATH   Path to the flipperzero-firmware repository.
  --copy           Copy the app into applications_user instead of symlinking it.
  -h, --help       Show this help text.

Environment:
  FLIPPER_FW_PATH  Firmware repository path if --fw-path is not supplied.
EOF
}

die() {
    printf 'Error: %s\n' "$*" >&2
    exit 1
}

log() {
    printf '[build] %s\n' "$*"
}

require_path() {
    local path="$1"
    [[ -e "$path" ]] || die "Path does not exist: $path"
}

parse_args() {
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --fw-path)
                [[ $# -ge 2 ]] || die "--fw-path requires a value"
                FW_PATH="$2"
                shift 2
                ;;
            --copy)
                USE_COPY=1
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
}

discover_fw_path() {
    local candidate
    if [[ -n "$FW_PATH" ]]; then
        return
    fi

    for candidate in \
        "${ROOT_DIR}/../Momentum-Firmware" \
        "${ROOT_DIR}/../../Momentum-Firmware" \
        "${ROOT_DIR}/../flipperzero-firmware" \
        "${ROOT_DIR}/../../flipperzero-firmware" \
        "/c/Users/jasammarco.ENG/Projects/Momentum-Firmware" \
        "${HOME}/src/flipperzero-firmware"; do
        if [[ -d "$candidate" ]]; then
            FW_PATH="$candidate"
            return
        fi
    done
}

validate_fw_path() {
    require_path "$FW_PATH"
    [[ -d "${FW_PATH}/applications_user" ]] || die "Missing applications_user in ${FW_PATH}"
    [[ -f "${FW_PATH}/fbt" ]] || die "Missing fbt in ${FW_PATH}"
}

copy_tree() {
    local src="$1"
    local dst="$2"

    rm -rf "$dst"
    mkdir -p "$dst"

    if command -v rsync >/dev/null 2>&1; then
        rsync -a --delete \
            --exclude '.git' \
            --exclude 'dist' \
            --exclude '.DS_Store' \
            "${src}/" "${dst}/"
    else
        tar -C "$src" \
            --exclude='.git' \
            --exclude='dist' \
            --exclude='.DS_Store' \
            -cf - . | tar -C "$dst" -xf -
    fi
}

install_app_into_fw_tree() {
    local dst="${FW_PATH}/applications_user/${APP_FOLDER_NAME}"

    if [[ "$USE_COPY" -eq 1 ]]; then
        log "Copying app into ${dst}"
        copy_tree "$ROOT_DIR" "$dst"
    else
        log "Symlinking app into ${dst}"
        rm -rf "$dst"
        ln -s "$ROOT_DIR" "$dst"
    fi
}

run_build() {
    log "Running fbt for ${APP_ID}"
    (
        cd "$FW_PATH"
        ./fbt "fap_${APP_ID}"
    )
}

find_built_fap() {
    local candidate

    while IFS= read -r candidate; do
        if [[ -n "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done < <(find "${FW_PATH}/build" "${FW_PATH}/dist" -type f -name "${APP_ID}.fap" 2>/dev/null)

    return 1
}

stage_artifact() {
    local built_fap="$1"
    mkdir -p "$DIST_DIR"
    cp "$built_fap" "${DIST_DIR}/${APP_ID}.fap"
    log "Copied artifact to ${DIST_DIR}/${APP_ID}.fap"
}

main() {
    parse_args "$@"
    discover_fw_path
    [[ -n "$FW_PATH" ]] || die "Could not locate flipperzero-firmware. Set FLIPPER_FW_PATH or pass --fw-path."
    validate_fw_path
    install_app_into_fw_tree
    run_build

    local built_fap
    built_fap="$(find_built_fap)" || die "Build finished but ${APP_ID}.fap was not found"
    stage_artifact "$built_fap"
    log "Build complete"
}

main "$@"
