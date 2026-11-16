#ifndef HISTORY_H
#define HISTORY_H

// what the search learns about move quality: killer moves, the history heuristic and the pawn correction history (history.c).
// all of it is thread local: helper threads start empty, the main thread keeps what it learned between the moves of a game
#include <stdint.h>
#include "bitboards.h"
#include "move.h"
#include "counters.h"
#include "tables.h"
#include "params.h"

/*
killer moves: a quiet move that caused a cutoff at some ply is remembered (two per ply). the same move often works in the
neighbouring branches of the tree as well, so it is tried early there (see movepick.h).

history heuristic: a table that counts how often a quiet move (by side, from square and to square) caused a cutoff
anywhere in the tree. moves that were tried before the cutoff move and failed get a penalty. the table is used to order
quiet moves and to reduce the moves with a bad history more in late move reductions. an update moves the value towards
the limit MAX_HISTORY but more slowly the closer it is ("gravity"), so the values stay bounded and old information fades.
the last index says which of the two squares are attacked by the enemy: moving a piece out of an attack or onto an
attacked square are different kinds of moves
*/
#ifndef HIST_BONUS_CAP
#define HIST_BONUS_CAP 1200
#endif
#define MAX_KILLER_PLY 128
#define MAX_HISTORY HIST_MAX
#define MAX_TRACKED_QUIETS 64

extern _Thread_local move_t killers[MAX_KILLER_PLY][2];
#define HIST_N 4
// bit 1: the piece stands on a square the enemy attacks, bit 0: the move goes to a square the enemy attacks
#define HIST_IDX(att, f, t) ((int)((((att) >> (f)) & 1) * 2 + (((att) >> (t)) & 1)))
extern _Thread_local int history[2][64][64][HIST_N];

// history update with gravity: the closer a value is to the limit the less it grows
static inline void update_gravity(int16_t *e, int bonus) {
    int v = *e;
    v += bonus - v * (bonus < 0 ? -bonus : bonus) / MAX_HISTORY;
    *e = (int16_t)v;
}

/*
correction history: the static evaluation does not see everything (it only knows rules of thumb), but the search finds
out how wrong it was. for every pawn structure (and side to move) a running average of "search score - static
evaluation" is kept, and added to the static evaluation of later positions with the same pawn structure.
*/
#define CH_BITS 14
#define CH_SIZE (1 << CH_BITS)
// [side to move][index]: average of (search score - raw static eval) in 1/256 eval units. thread local like the other history tables
// (helper threads start empty, the main thread keeps what it learned between the moves of a game)
extern _Thread_local int16_t corr_pawn[2][CH_SIZE];

struct corr_idx {
    unsigned pawn;
};

// fibonacci hashing: the multiplier is 2^64 divided by the golden ratio (made odd). the multiplication moves the information of
// every input bit upwards, so the top CH_BITS bits of the product, which are used as the index, depend on the whole key
static inline unsigned corr_index(u64 key) {
    return (unsigned)((key * 0x9E3779B97F4A7C15ULL) >> (64 - CH_BITS));
}

// the keys are made from the bitboards (a multiplication per bitboard), so nothing has to be kept up to date in do_move.
// the two multipliers are different odd 64 bit numbers with well mixed bits (any such pair works). they must differ, otherwise a pawn
// structure and its color swapped mirror would give the same key, because the xor does not care about the order
static inline void corr_keys(const struct bitboards *BB, struct corr_idx *ci) {
    ci->pawn = corr_index((BB->bits[white_pawns] * 0xA24BAED4963EE407ULL) ^ (BB->bits[black_pawns] * 0x9FB21C651E98DF25ULL));
}

// the correction to add to the raw static evaluation (eval units, from the point of view of the side to move)
static inline int corr_hist_get(const struct bitboards *BB, const struct corr_idx *ci) {
    const int stm = BB->color == WHITE ? 0 : 1;
    return corr_pawn[stm][ci->pawn] * CH_PAWN_W / (256 * 64);
}

static inline void corr_blend(int16_t *e, int target, int rate) {
    *e = (int16_t)((*e * (256 - rate) + target * rate) / 256);
}

// diff = search score - raw static eval, depth of the search that produced the score
static inline void corr_hist_update(const struct bitboards *BB, const struct corr_idx *ci, int diff, int depth) {
    const int stm = BB->color == WHITE ? 0 : 1;
    if (diff > CH_DIFF_MAX) diff = CH_DIFF_MAX;
    if (diff < -CH_DIFF_MAX) diff = -CH_DIFF_MAX;
    const int target = diff * 256;
    int rate = depth + 1;
    if (rate > CH_RATE_MAX) rate = CH_RATE_MAX;
    corr_blend(&corr_pawn[stm][ci->pawn], target, rate);
}

// the corrected evaluation must never look like a mate score
static inline int corr_clamp(int v) {
    return v > MATE_BOUND - 1 ? MATE_BOUND - 1 : (v < -(MATE_BOUND - 1) ? -(MATE_BOUND - 1) : v);
}

void clear_history(void); // forget everything (new game)
void history_new_search(void); // start of a search

// a quiet move caused a cutoff: remember it as a killer and reward it, punish the quiet moves that were tried before it
void quiet_cutoff(int side, int depth, move_t move, const move_t *tried, int num_tried, u64 attacks);

static inline void update_history(int side, move_t move, int bonus, u64 attacks) {
    int *h = &history[side][m_from(move)][m_to(move)][HIST_IDX(attacks, m_from(move), m_to(move))];
    *h += bonus - *h * (bonus < 0 ? -bonus : bonus) / MAX_HISTORY;
}

#endif
