#!/bin/bash
# the whole texel step in one go:  tools/texel/run_texel.sh positions.csv [more tuner options]
#   1. builds the tuner from the current sources   2. self test (the "not pure endgames" line must be tiny)
#   3. adam tuning (k is fitted on this data, do not pass --k)   4. puts the tuned values into eval.c
# the old eval.c is kept as eval_before_texel.c, so that the retune can be tested against it:
#   make pgo OUT=kolmato2_base EVAL_C=eval_before_texel.c     (and make pgo for the new one), then the SPRT as usual
set -e
cd "$(dirname "$0")/../.."
eval "$(make -s print-env)"   # SRCDIR SRC_NOMAIN
EVAL=$SRCDIR/eval.c
POS=${1:?usage: tools/texel/run_texel.sh positions.csv [tuner options]}; shift
# the engine sources by name, NOT ls *.c: eval_before_texel.c (a copy of eval.c from an earlier run) must not be linked too
gcc -O2 -march=native -fopenmp -DTUNE -I$SRCDIR tools/texel/tuner.c $SRC_NOMAIN -o tuner -lm
./tuner "$POS" --selftest | tee texel_selftest.log
./tuner "$POS" --method adam "$@" | tee texel_run.log
cp $EVAL eval_before_texel.c
python3 tools/texel/apply.py $EVAL tuned_params.txt
echo
echo "done. $EVAL now has the tuned values, the old one is eval_before_texel.c. next:  make pgo OUT=kolmato2_base EVAL_C=eval_before_texel.c   (and make pgo)"
