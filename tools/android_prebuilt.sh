#!/bin/sh
# tools/android_prebuilt.sh - single-ABI wrapper around the multi-ABI
# generator (tools/android_apk_libs.sh), kept as the documented entry
# point: tools/android_prebuilt.sh [ABI] [NDK_DIR] [SDL_SRC]
set -eu
ABI=${1:-x86_64}
AME_ANDROID_ABIS="$ABI" export AME_ANDROID_ABIS
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/android_apk_libs.sh" "${2:-}" "${3:-}"
