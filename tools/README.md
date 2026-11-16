# kolmato2 tools

Run everything from the project directory (the one with the Makefile). The python scripts need `pip install chess`.

## Tests

`make test` runs `tools/regress.sh`, the whole safety net in one command. It stops at the first failure (a failing step is named in the
output) and takes about 3-4 minutes:

* `params_gen.h` is up to date with `params.def`, and the build has no compiler warnings
* the node count of a fixed depth search of 16 positions (the "bench signature") is the expected one. A change of the search logic changes
  it on purpose, a change that should not influence the search (refactoring, comments, speed) must not
* perft on standard positions, `check_flags.py`, the pondering tests
* `domove_fuzz.c`: random playouts through `do_move` / `undo_move`, a digest of the complete board state after every move is compared with a reference
* `ep_diff.py`: 15000 positions with en passant captures against python-chess
* `movegen_diff.py`, `see_diff.py`, `eval_symmetry.py`: move lists, attacker sets and the symmetry of the evaluation

An optional argument is a reference binary: it is compared with the new build on all bench positions (same nodes, same moves, `bench.py`).

| tool | what it does |
|---|---|
| `perft_test.sh <bin>` | perft on 5 standard positions (move generator node counts) |
| `bench.py <depth> <bin1> [bin2..]` | fixed depth search of 16 positions, builds side by side (time, nodes, best move, score) |
| `uci_test.py <bin>` | the UCI mode with python-chess as the client (handshake, limits, clocks, mate scores, stop, promotions) |
| `ponder_test.py <bin>`, `ponder_info_test.py <bin> <depth> <bin>` | go ponder / ponderhit / stop, and that depth, score and pv are printed after a ponderhit |
| `movetest.c` | harness that reads FENs and prints the engine's legal moves, captures, attackers and SEE values. Built with all sources except `main.c`: `eval "$(make -s print-env)"; gcc -Ofast -I$SRCDIR tools/movetest.c $SRC_NOMAIN -o movetest -lm` |
| `movegen_diff.py`, `ep_diff.py`, `see_diff.py <movetest> [n]` | compare that harness with python-chess (random playouts, en passant, SEE). Build the harness with `-DVERIFY_EP=1` to let `ep_diff.py` also check `ep_capture_is_legal` against the complete test |
| `eval_symmetry.py <movetest>` | a position and its color-flipped mirror must get the same static evaluation |
| `check_flags.py <tune bin> <flags bin> "<-D flags>"` | a `-DNAME=value` build must give the same node counts as the `-DTUNE_SEARCH` build with the same values as UCI options (catches silently ignored flags) |
| `gen_params.py` | generates `params_gen.h` from `params.def`; `--check`, `--set "<-D flags>"` (make SPSA results the defaults), `--spsa <file>` |

## Strength tests

* `sprt.sh NEW BASE [tc] [elo0] [elo1]`: SPRT of two binaries with [fastchess](https://github.com/Disservin/fastchess), the way all results in the
  README were measured (10+0.1, one thread, 16 MB hash, bounds [0, 5], balanced random openings). `gen_openings.py` makes the opening file.
  To compare a change with the version before it, build both: `make pgo OUT=kolmato2_base` before the change, `make pgo` after it.
* `spsa/`: SPSA tuning of the search constants, see `spsa/README.md`.
* `texel/`: Texel tuning of the evaluation, see `texel/README.md` (self-play data, the tuner, and how the values get into `eval.c`).
