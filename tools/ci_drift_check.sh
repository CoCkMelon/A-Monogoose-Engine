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
envs=$(grep -rhoE 'getenv\("[A-Z0-9_]+"\)' src/*.c 2>/dev/null \
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
