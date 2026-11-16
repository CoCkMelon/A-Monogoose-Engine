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
echo "drift: ok ($nh public header path(s), $nd docs path(s) in range)"
exit 0
