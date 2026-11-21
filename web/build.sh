#!/bin/sh
# web/build.sh — compile the C engine + games to WASM (Emscripten) and
# publish the static site to docs/ (GitHub Pages serves /docs).
#
# ONE C codebase (build.txt rule 1): the same engine + game sources as the
# desktop build, plus the thin web bootstrap (src/app_web.c). No CMake
# cross-compile: plain emcc invocations, one single-file JS+WASM per game.
#
# Outputs: docs/index.html docs/games/loader.js docs/games/{memory_game,
# raymarch,line_draw}.js   (loader.js is the ONLY hand-written JS: ~60
# lines that probe WebGL2, lazy-load one game bundle, and boot it on a
# canvas. Everything else is compiled from C.)
#
# Requires: emcc on PATH (CI: emsdk setup step in pages.yml).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/docs/games"

if ! command -v emcc >/dev/null 2>&1; then
    echo "web/build.sh: emcc not found (install emsdk: https://emscripten.org/docs/getting_started/downloads.html)" >&2
    exit 1
fi

# Engine sources: DERIVED from src/*.c, not listed by hand. The hand-written
# list drifted once and cost a link failure - src/time.c was missing, so the
# bundles failed with "undefined symbol: ame_loop_defaults" plus the four
# ame_time_* entry points (all of which live in src/time.c). Six modules were
# absent in total: time.c, fs.c, mesh.c, obj.c, prefab.c, debug_watch.c.
# A derived list cannot drift the same way again: a new module joins the web
# build by default, and if it cannot compile for wasm this script fails loudly
# here instead of the bundle silently shipping without it.
# Exclusions, each with its reason:
#   app_sdl.c          the desktop bootstrap; the web bundle uses app_web.c
#   app_web.c          passed explicitly per game below, not via ENGINE
#   input_asyncinput.c native-only backend (/dev/input + a thread); the web
#                      bootstrap is single-threaded - static hosts send no
#                      COOP/COEP, so there are no pthreads
#   dialogue.c         needs libfyaml, unavailable under emcc - the same rule
#                      as a desktop build without the dev package
# Generated font atlases are committed, so no host bake step is needed.
WEB_EXCLUDE="app_sdl.c app_web.c input_asyncinput.c dialogue.c"
ENGINE=""
for _f in src/*.c; do
    _b=${_f#src/}
    _skip=0
    for _e in $WEB_EXCLUDE; do
        if [ "$_b" = "$_e" ]; then _skip=1; fi
    done
    if [ "$_skip" -eq 1 ]; then continue; fi
    ENGINE="$ENGINE $_f"
done
ENGINE="$ENGINE generated/font_atlas.c generated/font_atlas_dsdf.c
    generated/font_atlas_hires.c generated/shaders_gen.c"

# Engine's OWN code stays warnings-as-errors; FP contraction stays off
# (per-binary determinism, same as desktop).
# gnu23, not c23: EM_ASM (canvas selector lookup in app_web.c) needs it;
# the SOURCES stay strictly-conforming C23, same files as desktop.
CFLAGS="-std=gnu23 -O3 -ffp-contract=off -Wall -Wextra -Werror"
INCLUDES="-Iinclude -Igenerated -Iexternal/stb"
# -sUSE_SDL=3: the Emscripten SDL3 port (window/events/audio/WebGL2).
# SINGLE_FILE: one .js per game (wasm inlined) so the static host needs
# no extra MIME/routes; MODULARIZE: factory boots on OUR canvas.
# No pthreads (static hosts send no COOP/COEP): the web bootstrap is
# single-threaded (rAF + fixed-substep accumulator in src/app_web.c).
EMFLAGS="-sUSE_SDL=3 -Wno-experimental -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sMODULARIZE=1 -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web \
    -sEXPORTED_FUNCTIONS=_main,_ame_set_running,_ame_audio_resume"

mkdir -p "$OUT"

build_game() { # name dim title w h factory -- sources...
    name=$1; dim=$2; title=$3; w=$4; h=$5; factory=$6; shift 6
    echo "web: $name ($dim, ${w}x${h})"
    # shellcheck disable=SC2086
    emcc $CFLAGS $INCLUDES \
        -DAME_"$dim"=1 -DAME_INPUT_SDL=1 \
        "-DAME_WEB_TITLE=\"$title\"" -DAME_WEB_W="$w" -DAME_WEB_H="$h" \
        $EMFLAGS "-sEXPORT_NAME=$factory" \
        "$@" -o "$OUT/$name.js"
}

cd "$ROOT"
# shellcheck disable=SC2086
build_game memory_game 3D "Memory — ame-next" 1280 720 AmeMemory \
    $ENGINE \
    examples/memory_game/mem_sim.c examples/memory_game/mem_net.c \
    examples/memory_game/mem_config.c examples/memory_game/mem_app.c \
    src/app_web.c
# shellcheck disable=SC2086
build_game raymarch 2D "Raymarch Arcade — ame-next" 1280 720 AmeRm \
    $ENGINE examples/raymarch_arcade/rm_app.c src/app_web.c
build_game line_draw 2D "Line Draw — ame-next" 800 600 AmeLine \
    $ENGINE examples/line_draw/line_app.c src/app_web.c

# Static page sources (hand-maintained, no build step of their own).
cp "$ROOT/web/index.html" "$ROOT/docs/index.html"
cp "$ROOT/web/loader.js" "$OUT/loader.js"

# Sanity: every bundle parses as JS and embeds exactly one wasm module.
for f in "$OUT"/*.js; do
    node --check "$f" || exit 1
done
echo "web: outputs:"
ls -la "$OUT" "$ROOT/docs/index.html"
