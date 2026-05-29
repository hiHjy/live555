#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export SYSROOT="${SYSROOT:-${HOME}/rk3568_sysroot_fixed}"
export CROSS_COMPILE="${CROSS_COMPILE:-aarch64-linux-gnu-}"

JOBS="${JOBS:-$(nproc)}"

echo "[1/2] build aarch64 live555 rtsp push module"
make -C "${SCRIPT_DIR}" clean
make -C "${SCRIPT_DIR}" -j"${JOBS}"

echo "[2/2] check output"
file "${SCRIPT_DIR}/lib/librtsp_push.a"

echo
echo "done:"
echo "  ${SCRIPT_DIR}/lib/librtsp_push.a"
echo
echo "public C header:"
echo "  ${SCRIPT_DIR}/include/live555_rtsp_push.h"
