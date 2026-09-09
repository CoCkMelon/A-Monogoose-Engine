#!/bin/sh
# tools/pack.sh — Stage 0 exit (build.txt PACKAGING + deploy.txt):
# produce the packaged LINUX unit of the Memory game from a RELEASE
# build and tar it up. Release links SDL3 statically where available
# (deploy.txt); every dynamic dependency outside the base-system
# whitelist — INCLUDING deps pulled in transitively by bundled libs —
# goes into lib/ beside the binary (found via the $ORIGIN/lib rpath).
#
# usage: tools/pack.sh [extra cmake args...]
#   e.g. tools/pack.sh -DAME_SDL3_STATIC=OFF
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${AME_PACK_BUILD:-$ROOT/build-pkg}
DIST=${AME_PACK_DIST:-$ROOT/dist}
NAME=${AME_PACK_NAME:-ame-next-stage0-linux-amd64}
UNIT=$DIST/$NAME

echo "== configure + release build =="
cmake -S "$ROOT" -B "$BUILD" -GNinja -DCMAKE_BUILD_TYPE=Release "$@" >/dev/null
cmake --build "$BUILD" --target memory_game

echo "== stage unit =="
rm -rf "$UNIT"
mkdir -p "$UNIT/lib"
cp "$BUILD/examples/memory_game/memory_game" "$UNIT/memory_game"
strip "$UNIT/memory_game" 2>/dev/null || true

base_lib() { # base-system whitelist: loader, libc family (never bundle)
    case "$1" in
    ld-linux*|libc.so*|libm.so*|libgcc_s*|libpthread*|libdl*|librt*|\
    libstdc++*|linux-vdso*) return 0 ;;
    esac
    return 1
}

# Bundle the FULL dependency closure: deps of the game, then deps of
# every bundled lib, until nothing new is copied (fixpoint).
round=0
while :; do
    round=$((round + 1))
    set -- "$UNIT/memory_game"
    for f in "$UNIT"/lib/*.so*; do [ -e "$f" ] && set -- "$@" "$f"; done
    scanned=$(ldd "$@" 2>/dev/null | awk '
        /:$/ { next }                  # ldd per-file headers
        /=> \// { print $3 ; next }
        $1 ~ /^\// { print $1 }' | sort -u)
    copied=0
    for so in $scanned; do
        b=$(basename "$so")
        base_lib "$b" && continue
        if [ ! -e "$UNIT/lib/$b" ]; then
            cp -L "$so" "$UNIT/lib/$b"
            echo "   bundling $b"
            copied=1
        fi
    done
    [ "$copied" -eq 0 ] && break
done
echo "   pass 1 closure: $(ls "$UNIT/lib" 2>/dev/null | wc -l) lib(s)"

# Pass 2: some libs carry private deps via their OWN rpath/runpath
# (e.g. libpulse -> .../pulseaudio/libpulsecommon). A flat copy breaks
# those $ORIGIN paths, so resolve them at the ORIGINAL location and
# bundle what is missing; repeat to a fixpoint.
while :; do
    origs=$(ldd "$UNIT"/lib/*.so* 2>/dev/null | awk '
        /:$/ { next }                  # ldd per-file headers
        /=> \// { print $3 ; next }
        $1 ~ /^\// { print $1 }' | sort -u)
    missing=""
    for so in $origs; do
        b=$(basename "$so")
        base_lib "$b" && continue
        [ -e "$UNIT/lib/$b" ] && continue
        # candidate bundled under the same soname? only bundle if the
        # bundle does NOT already satisfy it via LD_LIBRARY_PATH/rpath
        if ldd "$UNIT/memory_game" 2>/dev/null | grep -q "=> $so"; then
            missing="$missing $so"
        fi
    done
    [ -z "$missing" ] && break
    for so in $missing; do
        b=$(basename "$so")
        cp -L "$so" "$UNIT/lib/$b"
        echo "   bundling (private-dep) $b"
    done
done
echo "   closure complete after $round round(s): $(ls "$UNIT/lib" | wc -l) lib(s)"
[ -n "$(ls -A "$UNIT/lib" 2>/dev/null)" ] || rmdir "$UNIT/lib"

cat > "$UNIT/run.sh" <<'EOS'
#!/bin/sh
# ame-next Stage 0 unit — Memory (local hot-seat, 2 players)
DIR=$(dirname "$0")
LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    exec "$DIR/memory_game" "$@"
EOS
chmod +x "$UNIT/run.sh"

cat > "$UNIT/README.txt" <<'EOS'
ame-next — Stage 0 packaged unit
Memory (the FIRST GAME, local hot-seat): 4x4 pairs, strict 2-player
alternation, two opens per turn, turn passes on every resolve, most
matches wins (tie allowed). Players share the mouse on their turns.

RUN
  ./run.sh            (or ./memory_game)
Needs a GL/EGL runtime (any desktop has it). Headless/CI:
  SDL_VIDEODRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
  LIBGL_ALWAYS_SOFTWARE=1 AME_SCREENSHOT=shot.png ./run.sh

ENV
  AME_SERVER=h:p      ONLINE (Stage 1): connect to an authoritative
                      mem_server (run: mem_server <port>) instead of
                      local hot-seat; falls back to local if unreachable
  AME_SEED=0x...      replay a specific board shuffle (deterministic,
                      local mode only - online boards come from the
                      server)
  AME_SCREENSHOT=p    write p (PNG) after 5 frames, then exit
  AME_FAKE_MOUSE=x,y  place the cursor for headless hover checks
  AME_WINDOW_W/H      window size override

Click a face-down card to open it; click again after the game ends to
replay with a fresh shuffle. Rules: docs/README.txt (FIRST GAME).
EOS

echo "== tar =="
mkdir -p "$DIST"
tar -C "$DIST" -czf "$DIST/$NAME.tar.gz" "$NAME"
sha256sum "$DIST/$NAME.tar.gz"
echo "== contents =="
tar -tzf "$DIST/$NAME.tar.gz"
echo "pack: OK -> $DIST/$NAME.tar.gz"
