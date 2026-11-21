#!/bin/sh
# Spec/code drift gate (docs/agents.txt convention): a change set that
# touches the game-facing public headers (include/ame/*.h) must update
# the spec docs (docs/) in the SAME change set - header changed ->
# docs/ changed. Compares a commit range: ci_drift_check.sh <base> <head>.
# A range with no base (new-branch push, all-zero before) passes here;
# the pull-request range catches those change sets instead.
set -eu
base=$1
head=$2
zero=0000000000000000000000000000000000000000
if [ -z "$base" ] || [ "$base" = "$zero" ]; then
    echo "drift: no base ref (new branch) - nothing to compare, pass"
    exit 0
fi
changed=$(git diff --name-only "$base" "$head")
headers=$(printf '%s\n' "$changed" | grep -E '^include/ame/.*\.h$' || true)
docs=$(printf '%s\n' "$changed" | grep -E '^docs/' || true)
nh=$(printf '%s\n' "$headers" | grep -c . || true)
nd=$(printf '%s\n' "$docs" | grep -c . || true)
if [ -n "$headers" ] && [ -z "$docs" ]; then
    echo "DRIFT FAIL: public header(s) changed with no docs/ update:"
    printf '%s\n' "$headers" | sed 's/^/  /'
    echo "  update the matching spec under docs/ in the same change set"
    echo "  (convention: docs/agents.txt, CI job 'drift')"
    exit 1
fi
# Spec-per-header gate (docs/agents.txt): a NEW public header must have a
# spec under docs/ in the same change set. The check above only proves that
# SOME docs path changed, which let include/ame/time.h ship with no spec at
# all. A header counts as specified when docs/<stem>.txt exists for it - and
# a header added without one now fails here instead of being discovered by
# the next reader.
# Only headers ADDED in the range are checked: existing gaps are not this
# gate's debt to pay in an unrelated change set.
added=$(git diff --name-only --diff-filter=A "$base" "$head" \
        | grep -E '^include/ame/.*\.h$' || true)
# read the docs AT $head, not from the working tree: CI runs on a clean
# checkout, but a local run must not be able to pass on uncommitted docs
specmissing=''
for h in $added; do
    b=$(basename "$h")
    stem=${b%.h}
    # specified = a spec file carrying the module's name: docs/<stem>.txt for
    # include/ame/<stem>.h (docs/time.txt for time.h). A passing MENTION of
    # the filename is not enough - "time.h" appears in handoff notes and in
    # other specs' prose, which is exactly how time.h shipped with no spec at
    # all. A header specified as a section of another doc still needs the
    # one-line docs/<stem>.txt that points at it.
    git ls-tree -r --name-only "$head" docs/ | grep -qx "docs/${stem}\.txt" \
        || specmissing="$specmissing $b"
done
if [ -n "$specmissing" ]; then
    echo "SPEC FAIL: new public header(s) with no spec under docs/:"
    printf '  %s\n' $specmissing
    echo "  add docs/<module>.txt that specifies the header (docs/agents.txt)"
    exit 1
fi

# Env-table gate (docs/env.txt): every variable the ENGINE reads from src/
# must be documented there. Undocumented env vars are hidden state - the
# exact class state-audit.txt exists to remove - so the table is checked
# rather than trusted.
# Matches every form the engine actually uses to read an environment variable:
#   getenv("X")        the plain call
#   SDL_getenv("X")    src/app_sdl.c, which leans on the SDL wrapper (it always
#                      did for the loop policy, log path, window geometry and
#                      fixed-dt; the shot-capture hook joined them when bare
#                      getenv became a hard -Werror failure under MSVC's UCRT
#                      deprecation)
#   AME_GETENV("X")    the _WIN32 shim in src/fs.c, same reason
# A gate that matched only the bare form silently stopped seeing variables as
# soon as the call form changed - the count fell from 12 to 11 with no failure,
# which is precisely how an undocumented env var slips through. Any new wrapper
# or shim around getenv MUST be added to this pattern in the same change, and
# the count printed below is the check that it was.
# The alternation is case-EXPLICIT rather than a blanket -i, because the three
# forms differ in case: getenv and SDL_getenv are lowercase, the fs.c shim macro
# is AME_GETENV in capitals. A case-insensitive pattern would also pick up the
# two "#define AME_GETENV(n)" lines, which is harmless but noise.
# Read src AT $head, not from the working tree - the docs side below already
# did (git show "$head:docs/env.txt") and the two must agree on which commit is
# being judged, or a dirty checkout can pass a gate that the pushed commit
# fails (and vice versa). git grep with a tree-ish prefixes its output, which
# the sed strips along with the call syntax. If $head is not a valid object
# (an all-zero sha on a branch-created push) this yields nothing and the gate
# passes, matching how the range check at the top of this file already behaves.
envs=$(git grep -hoE '(SDL_getenv|AME_GETENV|getenv)\("[A-Z0-9_]+"\)' "$head" -- 'src/*.c' 2>/dev/null \
       | sed 's/.*("//; s/")//' | sort -u || true)
envexists=$(git ls-tree -r --name-only "$head" docs/ | grep -c '^docs/env\.txt$' || true)
missing=''
for v in $envs; do
    if [ "$envexists" -eq 0 ] || ! git show "$head:docs/env.txt" | grep -q "\b$v\b"; then
        missing="$missing $v"
    fi
done
if [ -n "$missing" ]; then
    echo "ENV FAIL: variable(s) read in src/ but missing from docs/env.txt:"
    printf '  %s\n' $missing
    echo "  document the effect (VALUE vs MODEL) in the same change set"
    exit 1
fi
ne=$(printf '%s\n' "$envs" | grep -c . || true)
echo "drift: ok ($nh public header path(s), $nd docs path(s) in range, $ne env var(s) documented)"
exit 0
