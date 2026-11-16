# kolmato2

kolmato2 is a UCI chess engine written in C. It is the successor of [kolmato](https://github.com/nuspli/kolmato), the engine I wrote from
scratch in 2023 as part of my first web dev project (the original JavaScript version is still online:
[nuspli.github.io/chess/chess.html](https://nuspli.github.io/chess/chess.html)). kolmato is no longer maintained, development continues here.
kolmato2 has no graphical interface of its own, other than an interactive terminal game mode. Use it with a UCI GUI (Cute Chess, Arena, ...) or a bot framework such as lichess-bot.

**What is new compared to kolmato:** bug fixes, a stronger search and evaluation, UCI support and multithreading, tools for tuning (SPSA, Texel)
and testing (a regression suite, SPRT scripts), and a refactoring of the code for speed and readability.

**AI assistance:** kolmato was written entirely by me. kolmato2 was built on top of kolmato, developed with the help of an AI assistant (Claude), which wrote much of
the code under my direction. The search and evaluation
techniques follow the descriptions on [chessprogramming.org](https://www.chessprogramming.org) and ideas that are common to many engines.

* bitboards with magic bitboard move generation
* principal variation search with the usual pruning and reductions (see [Search](#search))
* a hand crafted evaluation whose weights were tuned with the Texel method on self-play data
* Lazy SMP multi-threading, pondering, a custom opening book format
* all search constants are in one table (`src/params.def`) and were tuned with SPSA; the tools for both tunings are included (`tools/`)

Developed and tested on Linux, x86-64, with gcc.

## Build

    make               # ./kolmato2
    make pgo           # profile guided build, about 7-10 % faster, same search results
    make PORTABLE=1    # only needs popcnt: runs on any x86-64 cpu that has it (the default build also uses BMI1/BMI2 if the build machine has them)

`make` needs gcc and a POSIX shell. Other variables: `EXTRA="-DNAME=value"` (compiler flags, for example a parameter of
`src/params.def`), `OUT=name` (name of the binary), `CC=clang`. A binary built with BMI instructions checks at startup that the cpu has them and says so
instead of crashing.

## Use

Point the GUI at the binary. UCI options:

| option | default | |
|---|---|---|
| `Hash` | 128 | transposition table size in MB |
| `Clear Hash` | | button |
| `Threads` | 1 | search threads (Lazy SMP) |
| `DeepestThread` | true | with several threads, play the move of the thread that searched deepest |
| `Move Overhead` | 50 | milliseconds that are kept back per move for communication |
| `Ponder` | false | think on the opponent's time (the GUI has to support it) |
| `OwnBook` | false | use the opening book |
| `BookFile` | `bookMax.dat` | book file |
| `BookMinGames` | 20 | a book move must have been played at least this often |
| `BookRandom` | false | sometimes (rarely) play the second or third most common book move instead of the most common one |
| `DebugLog` | | file name: logs the UCI conversation |

Console modes (`./kolmato2 -h` for all options):

    ./kolmato2 -b startpos -d 14        # best move of a position (depth 14); -t SECONDS for a time limit
    ./kolmato2 -e <fen>                 # static evaluation
    ./kolmato2 -g startpos              # play a game against it in the terminal
    ./kolmato2 -p <fen>                 # perft. depth is prompted for

### Opening book

The opening book is optional and off by default. It is included as `book/18moves.7z` (68 MB; unpacked `bookMax.dat`, 130 MB). It is a binary file in a
custom format (`src/book.c`, `src/parser.c`) with positions from games of the [Lichess Elite Database](https://database.nikonoel.fr/), each with
the moves that were played in it and how often. To use it:

    make book                         # unpacks book/18moves.7z to ./bookMax.dat (needs 7z, 7zz or 7za; or unpack it by hand)

and set `OwnBook` to true in the GUI (the `BookFile` option can point to the file if it is not in the working directory of the engine). Without a book
the engine searches from move 1. `parser.c` shows how the book was built; the games have to be converted to FEN lines first.

## Search

* iterative deepening with aspiration windows (from depth 5), principal variation search, fail-soft
* transposition table (lock-free 2 x 64 bit entries, a separate small table for the quiescence search), the principal variation is completed from the table
* null move pruning (the reduction depends on the depth and on how far the evaluation is above beta), reverse futility pruning, futility pruning,
  late move pruning, bad-capture pruning with SEE, delta pruning in the quiescence search
* late move reductions with a logarithmic table, adjusted by: pv node, evaluation not improving, expected cut node, table move is a capture, history
* internal iterative reduction, check extension
* move ordering: table move, SEE-ordered captures, killers, history indexed by side, from, to and whether the squares are attacked by the enemy
* correction history: a table indexed by the pawn structure learns the error of the static evaluation and corrects it
* the table stores the static evaluation, which saves evaluations at table hits
* time management: the soft limit is scaled after every iteration by the stability of the best move, the score trend and the share of the root
  nodes that went to the best move, and extended when the best move changes inside an iteration

## Evaluation

Material, piece square tables (middlegame and endgame, tapered), mobility, bishop pair, rook on open and half open files and on the 7th rank,
pawn structure (doubled, isolated, backward, passed, supported, phalanx), king shield and king attackers, pawn threats, outposts, a tempo bonus,
and a mop-up term for won endgames. The weights are in `src/eval.c`; they were tuned with the Texel method on 13.9 million positions from depth-9 self-play
(`tools/texel/README.md` explains the method and the workflow). A pawn is worth 90 internally, UCI scores are scaled to a pawn = 100.

## Source files

| file | content |
|---|---|
| `main.c` | entry point, command line and the console modes |
| `uci.c` | UCI loop, options, `go` handling, time allocation, pondering |
| `search.c`, `search.h`, `search_shared.h` | the search: `nega_max`, quiescence search, aspiration windows, iterative deepening |
| `timeman.c`, `threads.c` | time limits and the soft limit scaling, Lazy SMP helper threads |
| `movepick.c`, `history.c` | move ordering, killers, history, correction history |
| `evaluate.c`, `eval.c`, `eval.h`, `see.c` | evaluation, its weights, static exchange evaluation |
| `move.c`, `move_generation.c`, `magic.c`, `bitboards.c`, `check.c`, `fen.c` | board, move generation (fully legal), attack tables, check detection |
| `tables.c`, `counters.c`, `perft.c`, `book.c`, `parser.c`, `print.c`, `random.c` | transposition tables, counters, perft, opening book, helpers |
| `params.def`, `params.c`, `params.h`, `params_gen.h` | **all tunable parameters in one table**. `params_gen.h` is generated by `tools/gen_params.py` (do not edit; the compiler says so when it is stale) |

Parameters are plain constants in a normal build. `make tune` builds a version where they are UCI options for SPSA.

## Tests and tuning

`make test` runs the regression suite (needs `pip install chess`, takes 3-4 minutes): compiler warnings, node count of a fixed depth search,
perft, a fuzz test of `do_move` / `undo_move`, move generation, en passant and SEE against python-chess, symmetry of the evaluation, and pondering.
`tools/README.md` describes every tool; `tools/spsa/README.md` and `tools/texel/README.md` explain the two tunings.

## Development history: what worked and what did not

Strength changes were tested with SPRT (fastchess, 10+0.1, one thread, 16 MB hash, balanced random openings, bounds [0, 5] Elo, alpha = beta = 0.05,
`tools/sprt.sh`). The number is the Elo difference to the version without the change, "±" is the error bar printed by fastchess, in brackets the number of games.
An SPRT does not measure Elo precisely: results from a few thousand games (error bars of 7 to 10 Elo) are only good for "clearly helps" or "clearly hurts",
and self-play gains are usually larger than the gain against other engines.

### Kept

| change | result |
|---|---|
| new evaluation terms (tuned by hand) | +38.6 ± 14.0 (1656) |
| fail-soft search | +5.6 ± 4.3 |
| keep the history tables between moves of a game | +7.4 ± 5.3 |
| expected cut node reductions in LMR, table-capture reductions, null move R = 3 + depth / 3, no null move when the table says it fails | +14.3 ± 8.0 (4610) |
| more reduction when the evaluation is not improving | +10.8 ± 6.7 (6272) |
| history indexed by whether the from / to squares are attacked | +15.4 ± 8.1 (3980) |
| futility pruning rewritten | +4.3 ± 3.5 (23.6k) |
| first valid SPSA run: 18 search parameters, 30k game pairs | +9.1 ± 5.9 (7356) |
| new hash keys (the old ones, made with a weak xorshift generator, gave different positions the same 64 bit hash) + using the table score for the pruning decisions | +4.1 ± 3.3 (24.3k); the new keys alone -0.9 ± 5.6 (kept because they are correct) |
| correction history, pawn structure table only | +5.2 ± 4.0 (15.8k) |
| latest SPSA run: 34 parameters (search, time management, correction history), 52k game pairs, against the previous defaults | +21.8 ± 9.7 (2566) |

Speed work that does not change the search (the pruning decision is taken before `do_move` using an exact `gives_check`, the static evaluation is
stored in the table): about +6.7 % nodes per second, verified by identical node counts.

Three further changes were kept without a reliable number: the time management scaling, the Texel retune of the evaluation (13.9M positions) and a first SPSA run
that included the time constants. Their individual SPRT results were lost: some of the tests had been started with the wrong binaries, so the numbers recorded at the time
(each about +20 Elo) are not trustworthy. A later version containing all three beat two older builds of the engine by about +50 and +100 Elo, so the combination is a clear improvement,
but how it divides between the three is unknown.

### Tried and rejected

Everything here was implemented behind a compile flag, tested, and removed from the code afterwards to keep it readable.

| idea | result |
|---|---|
| singular extensions (extend the table move when all other moves fail low at reduced depth; no negative extension, no multi-cut) | -15.8 ± 24.5 (440), -8.7 ± 29.9 (320), rejected again later. The implementation lacks negative extensions and multi-cut, so a retry would start there |
| continuation history (piece and square of the moves 1 and 2 plies ago) for move ordering and LMR | -12.0 ± 8.5 (4054), -13.8 ± 9.0 with a second implementation |
| capture history added to the MVV-LVA capture order | -3.4 ± 5.2 |
| `improving` for reverse futility pruning (smaller margin) | -5.4 ± 6.1 |
| `improving` for late move pruning (lower limit when not improving) | +0.5 ± 2.7 (37k): no gain |
| pruning of quiet moves with SEE (`-15 * depth^2`) | -2.5 ± 4.7 |
| pruning of bad captures up to depth 6 instead of 3 | -7.8 ± 7.1 |
| late move pruning up to depth 8 | -27.5 ± 12.2 |
| aspiration window delta 14 instead of 30 | -0.7 ± 3.6 |
| no table cutoffs in pv nodes | -27.1 ± 12.0 |
| extra reduction for expected cut nodes without a table move (IIR) | -3.4 ± 5.2 |
| a second correction history table keyed by pawn structure and both king squares | -10.1 ± 7.7 (4468), -7.8 ± 6.9 (5574) |
| correction history tables for the non-pawn pieces (with the pawn table) | -8.8 ± 7.1 (4948); without the quiescence correction -15.1 ± 9.3 (3252) |
| a bundle of the history and reduction ideas above | -14.3 |
| five groups of extra evaluation terms (variants of the passed pawn, king, mobility and bishop pair terms, and one more) | rejected by SPRT, numbers not kept |
| time management with scale limits 150 / 70 | +3.9 ± 3.1 (26k): too weak, the version with other limits (+23 ± 10, 2610 games, not reliable, see above) was kept |

One idea was implemented but never tested: null move only in expected cut nodes.

What the list shows: more pruning hurt, better reductions and better move ordering helped. Larger structural changes (null move, LMR adjustments,
history) paid; local pruning tweaks did not. A smaller static evaluation error (measured with a debug build) did not predict SPRT results.

Pondering is not covered by these tests (fastchess cannot ponder); it is tested with `tools/ponder_test.py`.
