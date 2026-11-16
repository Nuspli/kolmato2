#ifndef SEARCH_H
#define SEARCH_H

#include <time.h>
#include "bitboards.h"
#include "move.h"
#include "move_generation.h"
#include "eval.h"
#include "counters.h"
#include "tables.h"
#include "check.h"
#include "print.h"
#include "book.h"
#include "fen.h"
#include "params.h"

#define INF 100000

int evaluate(struct bitboards *BITBOARDS);

int quiescence_search(struct bitboards *BITBOARDS, int alpha, int beta, int depth);

bool is_three_fold_repetition(struct bitboards *BITBOARDS);
bool is_insufficient_material(struct bitboards *BB);
const char *draw_reason(struct bitboards *BB);
u64 attackers_to(const struct bitboards *BB, int sq, u64 occ);
int see(const struct bitboards *BB, move_t move);
float game_phase_of(const struct bitboards *BB);
move_t iter_deepening(move_t *possible, struct bitboards *bitboards, int num_moves);
void reset_search();
extern int deepest_thread; // 1: play the move of the deepest search thread
extern int search_threads; // number of search threads (lazy SMP), 1 = no helper threads
long long search_total_nodes(void); // nodes of the current search over all threads

// settings of the search that only the uci mode uses (see uci.c), the defaults are the behaviour of the other modes
#include <time.h>
// the longest line (in plies from the root) that the search and the pv arrays can hold
#define MAX_PLY 128

typedef void (*iteration_hook_t)(int depth, int score, const move_t *pv, int pv_length);
extern bool use_wall_clock;
extern struct timespec wall_start;
extern double soft_time_ms;
extern long long max_nodes;
extern bool (*stop_poll)(void);
extern iteration_hook_t iteration_hook;
double search_elapsed_ms(void);
void search_set_limits(double soft_ms, double hard_ms); // new soft / hard limit (ms, 0 = none) of the running search, used by ponderhit

int nega_max(struct bitboards *BITBOARDS, int depth, int alpha, int beta, bool allow_null_move, bool cut_node);

void init_lmr_table(void); // (re)builds the late move reduction table from the parameters

u64 perft(int depth, struct bitboards *BITBOARDS, int original_depth); // perft.c

#endif