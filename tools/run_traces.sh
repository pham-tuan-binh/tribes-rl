#!/usr/bin/env bash
# Golden-trace conformance: generate Java traces for a seed range and replay
# each in the C engine. Usage: tools/run_traces.sh [first_seed] [last_seed]
set -u
cd "$(dirname "$0")/.."
export PATH="/opt/homebrew/opt/openjdk/bin:$PATH"

FIRST=${1:-1001}
LAST=${2:-1020}
ROOT=$PWD

mkdir -p traces build/java
( cd reference/Tribes && javac -d "$ROOT/build/java" -cp "lib/json.jar" \
    -sourcepath "src:$ROOT/tools/java_trace" \
    "$ROOT/tools/java_trace/core/game/TraceDumper.java" 2>&1 | grep -v '^Note' )
cc -std=c11 -O2 -Wall -Wextra -o build/trace_replay tools/trace_replay.c || exit 1

pass=0; fail=0
for seed in $(seq "$FIRST" "$LAST"); do
    map="traces/map_$seed.csv"
    trace="traces/trace_$seed.jsonl"
    if [ ! -f "$trace" ]; then
        ( cd reference/Tribes && java -cp "$ROOT/build/java:lib/json.jar" \
            core.game.TraceDumper "$seed" $((seed * 7 + 3)) 5000 "$ROOT/$map" "$ROOT/$trace" ) > /dev/null
    fi
    if out=$(./build/trace_replay "$map" "$trace" 2>&1); then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "SEED $seed FAILED:"
        echo "$out" | head -4
    fi
done
echo "traces: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
