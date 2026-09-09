#!/bin/sh
# tools/smoke.sh — clean-machine smoke test of the packaged unit
# (build.txt: "SMOKE-TESTS the packaged artifact on a clean
# machine/VM/container..., not the dev build"). Unpacks the tarball
# into an empty temp dir, audits every dynamic dependency (bundle or
# base system only — a system SDL3 path is a FAIL; that is the exact
# "missing .so on a clean machine" trap deploy.txt describes), runs
# the game headlessly, verifies the PNG, and proves replay
# determinism: same AME_SEED => byte-identical screenshots.
#
# usage: tools/smoke.sh [tarball]   (default dist/ame-next-...tar.gz)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
TAR=${1:-$ROOT/dist/ame-next-stage0-linux-amd64.tar.gz}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

TOP=$(tar -tzf "$TAR" | head -1 | cut -d/ -f1)
tar -xzf "$TAR" -C "$TMP"
cd "$TMP/$TOP"

echo "== unit contents =="
find . -type f | sort

echo "== dependency audit (bundle or base system only) =="
# audit with the SAME library search the launcher sets up (run.sh):
# LD_LIBRARY_PATH=lib — this is exactly how the unit resolves at runtime
if ! LD_LIBRARY_PATH="$PWD/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
     ldd ./memory_game | awk -v dir="$PWD" '
    { pathof() }
    function pathof(  p) {
      if ($2 == "=>") p = $3
      else if ($1 ~ /^\//) p = $1
      else return
      gsub(/\/\.\//, "/", p)          # normalize $ORIGIN artifacts
      if (index(p, dir "/lib/") == 1) { print "   bundle: " p ; return }
      name = p ; sub(/.*\//, "", name)
      if (name ~ /^(ld-linux|libc\.|libm\.|libgcc_s|libpthread|libdl|librt|libstdc\+\+)/) {
        print "   system: " p ; return }
      print "   BAD   : " p ; exit 3
    }'; then
    echo "smoke: FAIL — dependency outside bundle/base (see BAD above)"
    exit 1
fi

echo "== headless run + PNG check =="
export SDL_VIDEODRIVER=offscreen SDL_AUDIO_DRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
AME_SCREENSHOT=smoke_a.png ./run.sh
[ -s smoke_a.png ] || { echo "smoke: FAIL — no screenshot"; exit 1; }
magic=$(od -An -tx1 -N4 smoke_a.png | tr -d ' \n')
[ "$magic" = 89504e47 ] || { echo "smoke: FAIL — not a PNG"; exit 1; }
size=$(wc -c < smoke_a.png)
echo "   smoke_a.png: $size bytes"
[ "$size" -gt 10000 ] || { echo "smoke: FAIL — screenshot too small"; exit 1; }

echo "== replay determinism (Stage 0 exit: fixed seed => same game) =="
AME_SEED=0x5EED AME_SCREENSHOT=rep1.png ./run.sh >/dev/null
AME_SEED=0x5EED AME_SCREENSHOT=rep2.png ./run.sh >/dev/null
if cmp -s rep1.png rep2.png; then
    echo "   same seed: byte-identical renders"
else
    echo "smoke: FAIL — same seed produced different renders"
    exit 1
fi

echo "smoke: OK — packaged unit runs on a clean machine"
