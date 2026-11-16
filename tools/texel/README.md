# Texel tuning for kolmato2

This directory contains a tuner for the weights of kolmato2's evaluation: the material values, the piece square tables and
the other terms (mobility, passed pawns, rook files, ...). Nothing in here is taken from another engine: it is a method,
and the evaluation it tunes is yours.

    datagen.py   plays self-play games and writes positions with the result of the game they came from
    tuner.c      reads those positions and finds weights that predict the results better
    README.md    this file

## 1. The idea in one paragraph

A good evaluation should say "white is clearly better" in positions that white goes on to win, and "equal" in positions
that end in a draw. So take a large number of quiet positions from real games together with the final result of each
game, turn the evaluation into a predicted score with a smooth function, and measure how far the predictions are from
the real results. Then change the weights so that this error gets smaller. No search is needed, only the static evaluation
of each position, which makes the optimization fast (the method is named after Texel, the engine of Peter Österlund, who
described it in 2014).

## 2. The pipeline

    1. datagen.py    self-play games  ->  positions.csv   (fen,result)
    2. tuner         positions.csv    ->  features  ->  error  ->  optimizer  ->  tuned_params.txt
    3. you           paste the tuned values into eval.c, build, and test with matches (the error is only a proxy!)

### The error function

The evaluation `e` (centipawns, from white's point of view) is turned into an expected score with a sigmoid:

    p = 1 / (1 + exp(-k * e))              (p is between 0 and 1: 0.5 for e = 0, near 1 when white is far ahead)

The error is the mean squared difference between this prediction and the real result `r` (1, 0.5 or 0):

    E = (1/N) * sum over all positions of (r - p)^2

`k` decides how steep the sigmoid is, in other words how many centipawns are "a lot". It is fitted once, with the starting
weights, and then kept fixed. (Many texts write `p = 1 / (1 + 10^(-K e / 400))`; that is the same function with
`k = K ln(10) / 400`. The tuner prints both.) If `k` were allowed to change together with the weights there would be
nothing to stop all weights from growing while `k` shrinks, which is why it is fixed. For the same reason the value of a
pawn is not tuned: it is the unit everything else is measured in, and the search margins (futility, delta pruning, ...)
are written in that unit.

### Why quiet positions?

The static evaluation does not see captures that are about to happen. If a queen is hanging, the position is lost for
the side to move, but the static evaluation still counts the queen. Such positions would teach the tuner that "being
a queen up is not that good", which is wrong. `datagen.py` therefore only keeps positions where the side to move is not
in check and the move the engine actually played is not a capture, promotion or check.

### The features (how the tuner can be fast)

The evaluation is almost a weighted sum:

    eval = scale * ( constant + c_1 * w_1 + c_2 * w_2 + ... )

For a given position only a few of the weights occur, with a coefficient `c`: a material value with (white count - black
count), a piece square table entry with +1 for a white piece on that square and -1 for a black piece (split between
the opening and endgame tables by the game phase), the weight of a term like "doubled pawns" with (white count - black
count) of the pawns concerned, and so on. These coefficients are called the *features* of the position. They are computed
once, when the file is loaded. After that, the error for any set of weights is just a short dot product per position,
no chess logic needed, and about a million positions can be evaluated in a fraction of a second.

Two things are not part of the sum and are handled as constants:

* `constant`: everything the model does not know (the king terms in pure endgames, rounding to integers). It is computed
  as *real evaluation minus model evaluation* with the engine's own `evaluate()` when the position is loaded. The
  self test prints how large these constants are; if the feature extraction were wrong they would be large.
* `scale`: the engine ends `evaluate()` with `x * 17680 / (white + |black| + 10000)`. This depends on the weights too, but
  only slightly, so it is kept fixed per position.

The engine and the tuner never disagree about what a term means, because `evaluate()` itself records the term counts
(the `TRACE` lines, compiled only into the tuner with `-DTUNE`). The piece square table features are computed directly
from the board and checked against the engine by the constants.

## 3. The optimizer

### The original Texel method: local search

For every weight try +1 and -1; keep a change if the error becomes smaller; repeat until nothing improves. It is easy to
understand and keeps all weights integers, but every single trial needs the error of the whole data set, so with ~800 weights
and a million positions one pass takes a long time. `--method local` does this.

### Gradient descent with Adam (the default)

The error is a smooth function of the weights, so we can compute its slope with respect to every weight at once (the
*gradient*) and take a step downhill. For one position, with `p = sigmoid(k e)` and `d = p - r`:

    dE/de   = 2 d * p (1 - p) * k          chain rule: the square, then the sigmoid
    de/dw_j = scale * c_j                   the evaluation is linear in the weights

so the gradient of the mean error with respect to weight `j` is the average over all positions of
`2 d p (1 - p) k * scale * c_j`. One pass over the data gives the gradient of *all* weights, which is why this is
hundreds of times faster than local search. (`selftest` compares this formula with a numerical derivative.)

Plain gradient descent has a problem here: the gradient of a material value and the gradient of a table entry that
occurs in 1% of the positions differ by orders of magnitude, so no single step size suits both. **Adam** keeps a running
average of each weight's gradient (`m`) and of its square (`v`), and moves each weight by about `lr * m / sqrt(v)`. The
effect is that every weight moves at roughly the same speed (`lr`, in centipawns per iteration) in the direction its own
gradient points, whatever the size of the gradient. `lr` decays slowly (`--decay`) so that the weights settle.

Adam itself dates from about the same time as Texel tuning (2014); using it for the Texel error became common in the years
after, and it is what many engines with their own tuner do today. The tuning costs about as much as evaluating the
whole data set a few hundred times.

## 4. Overfitting, and what protects against it

With 800 weights and a limited number of positions the tuner can memorize noise: the error on the training positions keeps
falling, while the evaluation gets *worse* on other positions. Defences used here:

* **Validation set** (`--val-frac`, default 10%): positions that are never used for the gradient. The tuner prints the
  error on both sets. **Early stopping** keeps the weights from the best validation error and stops when it has not
  improved for a while (`--patience`).
* **`--min-count N`** (default 50): a weight that occurs in fewer than N training positions is not changed. A piece
  square table entry for a square a piece almost never stands on can not be learned from a handful of positions.
* **`--reg L`**: a penalty that pulls the weights back towards their starting values. It is 0 by default; try
  small values (1e-5 ... 1e-3) with small data sets.
* **More data** is the best remedy. Rough guide: a few thousand positions can tune the 20-odd scalar weights and
  the material values (`--groups m,s`); tuning all 768 table entries needs hundreds of thousands to millions.

At the end the tuned weights are rounded to integers, written into the engine, and the **real engine's `evaluate()`** is
used to compute the error on the validation positions ("REAL ENGINE" line). This is the honest number: it includes
everything the linear model approximates.

## 5. How to use it

    # 1. generate positions (plays kolmato2 against itself; use as many workers as you have cores)
    python3 tools/texel/datagen.py --kolmato2 ./kolmato2 --games 5000 --workers 6 --depth 7 --out positions.csv

    # 2. build the tuner (the engine files are compiled with -DTUNE so that evaluate() records its terms)
    eval "$(make -s print-env)"        # SRCDIR and SRC_NOMAIN: the engine files without main.c
    gcc -O2 -march=native -fopenmp -DTUNE -I$SRCDIR tools/texel/tuner.c $SRC_NOMAIN -o tuner -lm

    # 3. check that everything is consistent, then tune
    ./tuner positions.csv --selftest
    ./tuner positions.csv --method adam

    # tuned_params.txt now contains C code for the new values; paste it into eval.c

Data amounts: with 6 workers and depth 7 expect a few thousand positions per minute. 200k positions are a reasonable first
data set, 1M a good one. Positions from the same game are correlated, so more *games* is better than more positions per game.

Useful options: `--groups m,s` (only material and scalar terms, `p` adds the tables), `--k` (fix the sigmoid constant),
`--iters`, `--lr`, `--max-positions`.

### After tuning: test in play

A lower error does not automatically mean a stronger engine. The error measures how well the *static* evaluation
predicts results; what matters is the strength of the *search* using it. Always confirm with matches between the old and
the new engine (`tools/match.py`), with enough games to see the difference (a few hundred, more for small
differences). Two things often go wrong: tuning on data from a much weaker or stronger engine than the one you will play,
and tuning positions that are not quiet.

## 6. Adding a new evaluation term

1. Make its weight a global variable (see the `eval...` variables in `eval.h` / `eval.c`).
2. Count how often it occurs for each side inside `evaluate()` and add a `TRACE(F_NAME, white_count, black_count)` line
   (add `F_NAME` to the enum in `eval.h`).
3. Add one line to the `terms[]` table in `tuner.c` (the sign is -1 for a penalty that is subtracted).

Run `--selftest`: if the model does not reproduce the engine (large constants), the counts in the trace are wrong.

## 7. Is there anything better than Texel tuning?

Short answer: the *loss* is still the standard one for hand-written evaluations, and the biggest improvements since 2014
are in how it is optimized and in what the labels are. As far as I know:

* **Gradient-based optimizers (Adam)** instead of local search, as used here. Same objective, much faster, and it makes it
  practical to tune every table entry. This is the main practical advance.
* **Better labels.** Game results are noisy: a won game may contain positions that were equal. A common refinement is to mix
  the result with the score of a deeper search for the same position:
  `label = lambda * result + (1 - lambda) * sigmoid(k * search_score)`. Lambda between 0 and 1 trades noise against
  the bias of the engine that produced the scores. (Using another engine's scores as labels makes your evaluation copy
  that engine, which is the opposite of what you want; using your own engine's deeper search keeps it your own.)
* **Tuning on search results instead of the static evaluation** ("qsearch-based" Texel): evaluate each position with a
  quiescence search instead of the static evaluation, which makes noisy, non-quiet positions usable. Slower and more
  complicated; the quiet-position filter gets most of the benefit cheaply.
* **SPSA** (simultaneous perturbation stochastic approximation), used for tuning *search* parameters such as pruning
  margins and reduction tables: parameters are perturbed randomly and the engine plays games with both versions. It
  optimizes playing strength directly but needs a huge number of games, so it is used for few parameters. Texel tuning is
  for the evaluation, SPSA for the search constants.
* **Neural network evaluations (NNUE)** replace the hand-written terms completely. They are trained in the same spirit
  (positions with results and scores as labels) but with millions of weights; that is a different engine design.

For kolmato2 the sensible order is: Texel/Adam tuning of the evaluation, then SPSA or manual testing for the search
constants.

## 8. Reusing a positions file

`positions.csv` contains only the position, the result and a game number, none of the evaluation's numbers. The tuner
recomputes all features from the position with the *current* `evaluate()` every time it runs. So the same file can be used again
after the evaluation changed: add a term (section 6), run `--selftest` (the first line must still show tiny constants), and tune.
Generating the file is the expensive part and it does not need to be repeated for that.

What to keep in mind:

* **The labels belong to the engine that played the games.** Results reflect how well *that version* converted or defended
  positions. After the engine has become much stronger (or its evaluation changed a lot) it is worth generating new data with the
  new version. Old and new files can simply be concatenated (`cat a.csv b.csv > all.csv`); the game numbers keep the split between
  training and validation by game intact as long as different `--seed` values were used.
* **A new term can only be learned from positions where it matters.** Rare situations (for example bishop and knight against a
  lone king) are almost absent from game data, and the results there are not informative anyway (the side with the material
  always wins). Such terms are set by hand, like the corner driving term; their effect only ends up in each position's constant.
* **What breaks reuse:** changing how a counted term is defined while keeping its name (the self test then shows large
  constants), or changing the quiet-position filter in `datagen.py` (the file simply contains the old filter's positions).
* The sigmoid constant `k` is fitted again at every run, so nothing has to be kept from the previous run.
* A good routine: tune, then generate fresh data with the tuned engine, add it to the old data, tune again.

## Everything in one go
`tools/texel/run_texel.sh positions.csv` builds the tuner from the current sources, runs `--selftest`, runs adam (do not pass `--k` for new data) and writes the values into `eval.c`
(the old one is kept as `eval_before_texel.c` in the project directory; the script overwrites that copy on every run). Test the retune with an SPRT of `make pgo` against `make pgo OUT=kolmato2_base EVAL_C=eval_before_texel.c`.
Data: `python3 tools/texel/datagen.py --kolmato2 ./kolmato2 --games 250000 --workers 120 --depth 9 --sample-rate 1.0 --seed 5555 --out positions.csv`
(about 55 positions per game at rate 1.0, 21 at the default 0.4; 471 games/min at depth 9 on the server).
