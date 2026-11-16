#include "search_shared.h"

#ifndef TM_MIN_DEPTH
#define TM_MIN_DEPTH 6 // the scaling starts with the iteration of this depth (earlier iterations are too noisy, and cheap anyway)
#endif
#ifndef TM_SCORE_CLAMP
#define TM_SCORE_CLAMP 60 // score change (internal units, pawn = 90) that counts at most
#endif

/*
time and stop handling. there are two time limits:
    hard limit (max_time): the search is aborted immediately when it is reached, the unfinished iteration is lost
    soft limit (soft_time_ms): checked between iterations and between root moves. a new iteration is only started
    while the soft limit is not used up, because an iteration that can not finish is wasted time
the soft limit is scaled after every iteration depending on how the search is going (tm_after_iteration).
the defaults give the behaviour of the -b and -g modes: the limit is max_time seconds of cpu time.
the uci mode sets these (see uci.c): the gui measures real time, wants a soft limit, a node limit and a way to stop
the search with a command
*/
bool use_wall_clock = false;
struct timespec wall_start; // start of the search in real time (only used with use_wall_clock)
double soft_time_ms = 0; // 0: no soft limit
long long max_nodes = 0; // 0: no node limit
bool (*stop_poll)(void) = NULL; // called every TIME_CHECK_MASK + 1 nodes, returns true if the search has to stop
iteration_hook_t iteration_hook = NULL; // called after every completed iteration

double search_elapsed_ms(void) {
    if (use_wall_clock) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (now.tv_sec - wall_start.tv_sec) * 1000.0 + (now.tv_nsec - wall_start.tv_nsec) / 1e6;
    }
    return ((double)(clock() - search_start_time)) / (CLOCKS_PER_SEC / 1000);
}

bool time_is_up(void) {
    return search_elapsed_ms() > max_time * 1000;
}

volatile double cur_soft_ms = 0; // the soft limit of the running search: soft_time_ms, scaled after every iteration by the TM_* flags (0 = none)

// pondering: a search started with "go ponder" runs without limits; when the opponent plays the expected move (ponderhit) uci.c gives it the
// limits of a normal search. only the main thread reads these values (helpers stop through search_stopped), and this is called from the main thread (stop_poll)
void search_set_limits(double soft_ms, double hard_ms) {
    soft_time_ms = soft_ms;
    max_time = hard_ms > 0 ? hard_ms / 1000.0 : 1e9;
    cur_soft_ms = soft_ms;
}

bool should_stop(void) {
    if (!is_main_thread) {
        // helpers do not look at the clock or the input, they stop when the main thread sets search_stopped
        helper_publish_nodes();
        return false;
    }
    if (max_nodes > 0 && search_total_nodes() >= max_nodes) {
        return true;
    }
    if (stop_poll && stop_poll()) {
        return true;
    }
    return time_is_up();
}

void tm_start_search(void) {
    cur_soft_ms = soft_time_ms;
}

// the next iteration usually takes two to three times as long as this one, so if more than about half of the time that was planned for
// this move is gone, it would probably not be finished (soft limit). the soft limit is scaled by how the search is going, from the
// iteration of depth TM_MIN_DEPTH on
void tm_after_iteration(int depth, int stable_iterations, int previous_score, int score, double best_share) {
    if (soft_time_ms <= 0 || depth + 1 < TM_MIN_DEPTH) {
        return;
    }
    double scale = 1.0;
    {
        int st = TM_STAB_BASE_X100 - TM_STAB_STEP_X100 * stable_iterations;
        scale *= (st > TM_STAB_MIN_X100 ? st : TM_STAB_MIN_X100) / 100.0;
    }
    if (depth > 0 && abs(previous_score) < MATE_BOUND && abs(score) < MATE_BOUND) {
        int drop = previous_score - score; // > 0: the score fell
        if (drop > TM_SCORE_CLAMP) drop = TM_SCORE_CLAMP;
        if (drop < -TM_SCORE_CLAMP) drop = -TM_SCORE_CLAMP;
        scale *= 1.0 + (drop > 0 ? drop * TM_SCORE_DROP_X1000 : drop * TM_SCORE_RISE_X1000) / 1000.0;
    }
    if (best_share >= 0) {
        scale *= 1.0 + (0.5 - best_share) * TM_NODES_SLOPE_X100 / 100.0;
    }
    if (scale < TM_MIN_X100 / 100.0) scale = TM_MIN_X100 / 100.0;
    if (scale > TM_MAX_X100 / 100.0) scale = TM_MAX_X100 / 100.0;
    double limit = soft_time_ms * scale;
    if (limit > max_time * 950.0) limit = max_time * 950.0; // stay below the hard limit
    cur_soft_ms = limit;
    SAY("time management: stable %d scale %.2f soft %.0f ms\n", stable_iterations, scale, limit);
}

bool tm_soft_limit_reached(void) {
    return cur_soft_ms > 0 && search_elapsed_ms() > cur_soft_ms * (TM_GATE_X100 / 100.0);
}
