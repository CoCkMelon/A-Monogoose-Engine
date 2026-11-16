#!/bin/sh
# tools/android_apk_libs.sh - regenerate ALL native payloads of the APK:
# per-ABI libSDL3.so (pinned SDL release, NDK cross, built from source
# here when missing) + libmemory_game.so (our cmake android cross build,
# the same compile the android-cross CI job proves from scratch).
# Gradle ONLY packages what this script copies into jniLibs/.
#
# usage: tools/android_apk_libs.sh [NDK_DIR] [SDL_SRC] [JNI_OUT]
# env:   AME_ANDROID_ABIS   ABI list (default: arm64-v8a armeabi-v7a x86_64)
#        AME_SDL_PREFIX_ROOT  where per-ABI SDL installs land
#        AME_ANDROID_BUILD_ROOT  where build trees land (needs ~2 GB;
#                             defaults to /opt/dl/build-android when that
#                             dir is writable, else ./build-android-tmp)
set -eu
NDK=${1:-${ANDROID_NDK_HOME:-/opt/dl/android-ndk-r27c}}
SDL_SRC=${2:-/opt/dl/SDL-release-3.4.16}
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
JNI_OUT=${3:-"$ROOT/android/app/src/main/jniLibs"}
ABIS=${AME_ANDROID_ABIS:-"arm64-v8a armeabi-v7a x86_64"}
PREFIX_ROOT=${AME_SDL_PREFIX_ROOT:-/opt/dl/sdl3-prefixes}
BUILD_ROOT=${AME_ANDROID_BUILD_ROOT:-}
if [ -z "$BUILD_ROOT" ]; then
    if mkdir -p /opt/dl/build-android 2>/dev/null; then
        BUILD_ROOT=/opt/dl/build-android
    else
        BUILD_ROOT="$ROOT/build-android-tmp"
        mkdir -p "$BUILD_ROOT"
    fi
fi
[ -d "$NDK/build/cmake" ] || { echo "NDK missing at $NDK" >&2; exit 1; }
[ -d "$SDL_SRC/cmake" ] || { echo "SDL source missing at $SDL_SRC" >&2; exit 1; }

for ABI in $ABIS; do
    PREFIX="$PREFIX_ROOT/$ABI"
    if [ ! -f "$PREFIX/lib/libSDL3.so" ]; then
        echo "== SDL3 $ABI =="
        cmake -S "$SDL_SRC" -B "$BUILD_ROOT/sdl-$ABI" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 \
            -DCMAKE_BUILD_TYPE=Release -DSDL_TESTS=OFF \
            -DCMAKE_INSTALL_PREFIX="$PREFIX" > /dev/null
        cmake --build "$BUILD_ROOT/sdl-$ABI" > /dev/null
        cmake --install "$BUILD_ROOT/sdl-$ABI" > /dev/null
    else
        echo "== SDL3 $ABI (cached prefix) =="
    fi
    echo "== ame memory_game $ABI =="
    cmake -S "$ROOT" -B "$BUILD_ROOT/ame-$ABI" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
        -DAME_BUILD_TESTS=OFF -DAME_BUILD_BENCHES=OFF > /dev/null
    cmake --build "$BUILD_ROOT/ame-$ABI" --target memory_game > /dev/null
    mkdir -p "$JNI_OUT/$ABI"
    cp -f "$BUILD_ROOT/ame-$ABI/examples/memory_game/libmemory_game.so" "$JNI_OUT/$ABI/"
    cp -f "$PREFIX/lib/libSDL3.so" "$JNI_OUT/$ABI/"
    # ship stripped: unstripped payloads tripled the APK ("too big")
    STRIP="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
    [ -x "$STRIP" ] || STRIP="$NDK/toolchains/llvm/prebuilt/$(uname -s | tr A-Z a-z)-x86_64/bin/llvm-strip"
    "$STRIP" "$JNI_OUT/$ABI/libmemory_game.so" "$JNI_OUT/$ABI/libSDL3.so"
    echo "   -> $JNI_OUT/$ABI/"
done
echo "APK-LIBS-DONE: $ABIS"
