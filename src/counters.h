#ifndef COUNTERS_H
#define COUNTERS_H

#include <stdbool.h>
#include <time.h>
#include <stdint.h>

extern _Thread_local int possible_calls;
extern _Thread_local int move_calls;
extern _Thread_local int eval_calls;
extern _Thread_local int quiescence_calls;

extern _Thread_local int nodes;
extern _Thread_local int quiet_nodes;
extern _Thread_local int visits;
extern _Thread_local int quiet_visits;
extern _Thread_local int transpositions;
extern _Thread_local int quiet_transpositions;

extern int full_move_count;
extern int half_move_count;

extern double max_time;
extern int max_depth;

extern bool use_book;
extern char *book_name;
extern clock_t search_start_time;

extern int game_moves_played;
extern _Thread_local int ply;

extern uint64_t game_history[1000];
extern _Thread_local uint64_t search_history[1000];

#endif