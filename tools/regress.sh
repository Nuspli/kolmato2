#!/bin/bash
# regression suite: run after every refactoring step.  usage: tools/regress.sh [reference_binary]
#   1. builds with -Wall -Wextra (any warning fails)   2. bench depth 13: node signature must be BENCH_NODES
#   3. perft   4. check_flags (tune build == -D flags build)   5. ponder protocol tests   6. movegen / SEE / eval symmetry diffs
# the optional reference binary is compared position by position (same nodes, same moves) with tools/bench.py
# needs python-chess (pip install chess) and make. takes about 3-4 minutes
BENCH_NODES=2708k
cd "$(dirname "$0")/.."
eval "$(make -s print-env)"   # SRCDIR SRC SRC_NOMAIN CPUFLAGS (the Makefile is the one place that lists the sources)
fail=0
step() { echo "== $1"; }
bad() { echo "FAIL: $1"; fail=1; }

step "params_gen.h matches params.def"
python3 tools/gen_params.py --check || bad "params_gen.h is stale (python3 tools/gen_params.py)"

step "build, warnings"
out=$(gcc -pthread -O2 $CPUFLAGS -Wall -Wextra $SRC -o /tmp/regress_warn -lm 2>&1 | grep -c "warning")
[ "$out" = 0 ] || bad "$out compiler warnings"
gcc -pthread -Ofast $CPUFLAGS $SRC -o /tmp/regress_bin -lm 2>&1 | grep error && bad "build"

step "bench depth 13"
if [ -n "$1" ]; then b=$(python3 tools/bench.py 13 "$1" /tmp/regress_bin); echo "$b" | tail -2
  echo "$b" | grep -q "same best move in all builds: 16/16" || bad "best moves differ from reference"
  echo "$b" | tail -2 | head -1 | awk -F'|' '{split($2,a," "); split($3,c," "); if (a[2]!=c[2]) exit 1}' || bad "node count differs from reference"
else b=$(python3 tools/bench.py 13 /tmp/regress_bin); echo "$b" | tail -2; fi
echo "$b" | grep -q "$BENCH_NODES nodes" || bad "bench signature is not $BENCH_NODES nodes"

step "perft"
bash tools/perft_test.sh /tmp/regress_bin | awk '{ if ($3 != $6) { print "MISMATCH: " $0; bad=1 } else print "ok " $1 " " $3 } END { exit bad }' || bad "perft"

step "check_flags (tune build vs -D flags build)"
FL="-DRFP_MARGIN=100 -DLMR_BASE_X100=80 -DCH_PAWN_W=40"
gcc -pthread -Ofast $CPUFLAGS -DTUNE_SEARCH $SRC -o /tmp/regress_tune -lm && gcc -pthread -Ofast $CPUFLAGS $FL $SRC -o /tmp/regress_flags -lm
python3 tools/check_flags.py /tmp/regress_tune /tmp/regress_flags "$FL" | tail -1 | grep -q "^16/16" || bad "check_flags"
[ "$(printf 'uci\nquit\n' | /tmp/regress_tune | grep -c 'option name')" = 45 ] || bad "tune build option count (45 expected)"

step "ponder tests"
python3 tools/ponder_test.py /tmp/regress_bin | tail -1 | grep -q "all ponder tests passed" || bad "ponder_test"
python3 tools/ponder_info_test.py /tmp/regress_bin 12 /tmp/regress_bin | tail -1 | grep -q "missing info 0" || bad "ponder info (depth/score/pv missing after ponderhit)"

step "do_move / undo_move fuzz (full board state + evaluation digest after every move, 3000 random playouts)"
gcc -O2 -pthread -I$SRCDIR tools/domove_fuzz.c $SRC_NOMAIN -o /tmp/regress_fuzz -lm || bad "domove_fuzz build"
/tmp/regress_fuzz 3000 7 > /tmp/regress_fuzz.txt
tail -1 /tmp/regress_fuzz.txt | grep -q "undo mismatches 0" || bad "undo_move does not restore the position"
[ "$(sha1sum < /tmp/regress_fuzz.txt | cut -d' ' -f1)" = "3d73458fd049db5f648ea0ab0b76fa9e915ae23f" ] || bad "domove_fuzz digest differs from the reference (do_move / undo_move / move generation changed behaviour)"

step "en passant stress test (15000 positions vs python-chess, VERIFY_EP build compares ep_capture_is_legal with the complete test)"
gcc -Ofast -pthread $CPUFLAGS -DVERIFY_EP=1 -I$SRCDIR tools/movetest.c $SRC_NOMAIN -o /tmp/regress_movetest_ep -lm || bad "movetest (VERIFY_EP) build"
python3 tools/ep_diff.py /tmp/regress_movetest_ep 15000 1 | tail -1 | grep -q "move-list mismatches: 0   capture-list mismatches: 0" || bad "ep_diff: the engine's move list differs from python-chess"

step "movegen / SEE / eval symmetry"
gcc -Ofast -pthread -I$SRCDIR tools/movetest.c $SRC_NOMAIN -o /tmp/regress_movetest -lm || bad "movetest build"
python3 tools/movegen_diff.py /tmp/regress_movetest 100 | tail -1 | grep -q "move-list mismatches: 0   capture-list mismatches: 0" || bad "movegen_diff"
# see_diff.py compares against the old material values (310/320/490/880): its SEE count is stale since the texel retune
# only the attacker sets are a valid check
python3 tools/see_diff.py /tmp/regress_movetest 100 | tail -1 | grep -q "attacker-set mismatches: 0" || bad "see_diff attacker sets"
python3 tools/eval_symmetry.py /tmp/regress_movetest 100 | tail -1 | grep -q "asymmetric: 0" || bad "eval_symmetry"

[ $fail = 0 ] && echo "ALL OK" || { echo "REGRESSION FAILED"; exit 1; }
