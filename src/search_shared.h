#ifndef SEARCH_SHARED_H
#define SEARCH_SHARED_H

// what search.c, timeman.c and threads.c share. only they include this file (everything else uses search.h)
#include <stdatomic.h>
#include "search.h"

#ifndef TIME_CHECK_MASK
#define TIME_CHECK_MASK 4095 // the stop check runs when (node counter & mask) == 0, so every 4096 nodes
#endif

// the main thread prints what it thinks, the helper threads stay quiet
#define SAY(...) do { if (is_main_thread) printf(__VA_ARGS__); } while (0)

// defined in search.c
extern _Thread_local int pv_len[MAX_PLY];
extern _Thread_local int pv_table[MAX_PLY][MAX_PLY];
extern _Atomic bool search_stopped; // set when the search has to end; every function that sees it returns and stores nothing
extern _Thread_local bool is_main_thread;
// one iteration of the root search with aspiration windows; *completed = moves of the root list that were searched
int aspiration_search(struct bitboards *board, move_t *possible, int *values, int num_moves, int depth, int previous_score, int *completed);
void extend_pv_from_table(struct bitboards *bitboards);

// defined in timeman.c
extern volatile double cur_soft_ms;        // the soft limit of the running search in ms (0 = none), scaled after every iteration
bool time_is_up(void);                     // the hard limit
bool should_stop(void); // called every TIME_CHECK_MASK + 1 nodes: node limit, input poll, hard limit (main thread only)
void tm_start_search(void);
// after the iteration of depth+1: scales the soft limit by how the search is going. best_share: see iter_deepening
void tm_after_iteration(int depth, int stable_iterations, int previous_score, int score, double best_share);
bool tm_soft_limit_reached(void); // no new iteration should be started

// defined in threads.c
// the result of the deepest thread, if that thread is deeper than the main thread and wants to play another move
struct helper_choice {
    bool valid;
    int depth, score, len;
    int pv[MAX_PLY];
};
void threads_reset(void); // start of a search
void start_helpers(struct bitboards *board, move_t *possible, int num_moves);
void stop_helpers(int main_depth, int main_score, move_t main_move, struct helper_choice *choice);
void helper_publish_nodes(void); // a helper thread tells how many nodes it searched (no-op in the main thread)

#endif
