#include "counters.h"

// function call counters for benchmarking
_Thread_local int possible_calls = 0;
_Thread_local int move_calls = 0;
_Thread_local int eval_calls = 0;
_Thread_local int quiescence_calls = 0;
// search information
_Thread_local int nodes = 0;
_Thread_local int quiet_nodes = 0;
_Thread_local int visits = 0;
_Thread_local int quiet_visits = 0;
_Thread_local int transpositions = 0;
_Thread_local int quiet_transpositions = 0;
// counters for the fen output
int full_move_count = 0;
int half_move_count = 0;

double max_time;
int max_depth;

bool use_book = false;
char *book_name;
clock_t search_start_time = 0;

int game_moves_played = 0;
_Thread_local int ply = 0;

uint64_t game_history[1000] = {0};
_Thread_local uint64_t search_history[1000] = {0};