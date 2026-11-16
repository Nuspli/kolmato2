#include <pthread.h>
#include "search_shared.h"

#define MAX_SEARCH_THREADS 512

struct helper {
    pthread_t thread;
    bool started;
    int id; // 1 .. search_threads - 1
    struct bitboards board; // own copy of the root position
    move_t possible[MAX_NUM_MOVES];
    int num_moves;
    volatile long long nodes __attribute__((aligned(64))); // nodes searched so far (published every TIME_CHECK_MASK + 1 nodes)
    // result of the last iteration this helper completed (written by the helper only, read by the main thread after the join)
    int done_depth; // 0 = nothing completed yet
    int done_score; // from the point of view of the side to move, inside the aspiration window
    int done_len;
    int done_pv[MAX_PLY]; // done_len moves, done_pv[0] is the best move
};

static struct helper *active_helpers = NULL;
static volatile int active_helper_count = 0;
static long long finished_helper_nodes = 0; // nodes of the helpers of the current search after they have been joined
static _Thread_local struct helper *my_helper = NULL;

// all nodes of the current search, over all threads
long long search_total_nodes(void) {
    long long total = (long long)(unsigned)nodes + (unsigned)quiet_nodes + finished_helper_nodes;
    for (int i = 0; i < active_helper_count; i++) {
        total += __atomic_load_n(&active_helpers[i].nodes, __ATOMIC_RELAXED);
    }
    return total;
}

void threads_reset(void) {
    finished_helper_nodes = 0;
    active_helper_count = 0;
}

void helper_publish_nodes(void) {
    if (my_helper) {
        __atomic_store_n(&my_helper->nodes, (long long)((long long)(unsigned)nodes + (unsigned)quiet_nodes), __ATOMIC_RELAXED);
    }
}

// lazy SMP helper threads
// a helper searches the root position with its own iterative deepening. the threads do not split the work: they only share the
// transposition table, so what one of them finds (cutoffs, best moves, scores) makes the search of the others faster.
// to keep them from doing exactly the same work they skip some iterations, each thread a different pattern
// (the table is the one of the stockfish 7 lazy smp implementation)
static const int skip_size[20]  = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
static const int skip_phase[20] = { 0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7 };

static inline move_t possible_best(const struct helper *h) { return h->possible[0]; }

static void *helper_main(void *arg) {
    struct helper *h = (struct helper *)arg;
    my_helper = h;
    is_main_thread = false;
    ply = 0;
    nodes = 0;
    quiet_nodes = 0;
    pv_len[0] = 0;

    int values[MAX_NUM_MOVES];
    int best_score = 0;
    int idx = (h->id - 1) % 20;

    for (int depth = 0; depth < max_depth && !search_stopped; depth++) {
        if ((((depth + 1) + skip_phase[idx]) / skip_size[idx]) & 1) {
            continue;
        }
        int completed = 0;
        aspiration_search(&h->board, h->possible, values, h->num_moves, depth, best_score, &completed);
        if (search_stopped) {
            break;
        }
        best_score = values[0];
        // publish what this iteration found. possible[0] is the best move (the root list is sorted when a move beat alpha)
        h->done_score = values[0];
        if (pv_len[0] > 0 && (move_t)pv_table[0][0] == possible_best(h)) {
            h->done_len = pv_len[0] < MAX_PLY ? pv_len[0] : MAX_PLY;
            memcpy(h->done_pv, pv_table[0], h->done_len * sizeof(int));
        } else {
            h->done_len = 1;
            h->done_pv[0] = (int)possible_best(h);
        }
        h->done_depth = depth + 1;
        memset(pv_table, 0, sizeof(pv_table));
        memset(pv_len, 0, sizeof(pv_len));
    }

    __atomic_store_n(&h->nodes, (long long)((long long)(unsigned)nodes + (unsigned)quiet_nodes), __ATOMIC_RELAXED);
    return NULL;
}

void start_helpers(struct bitboards *board, move_t *possible, int num_moves) {
    int wanted = search_threads - 1;
    if (wanted > MAX_SEARCH_THREADS - 1) {
        wanted = MAX_SEARCH_THREADS - 1;
    }
    active_helper_count = 0;
    if (wanted <= 0) {
        return;
    }
    size_t bytes = ((wanted * sizeof(struct helper) + 63) / 64) * 64;
    active_helpers = aligned_alloc(64, bytes);
    if (!active_helpers) {
        return;
    }
    memset(active_helpers, 0, bytes);
    int created = 0;
    for (int i = 0; i < wanted; i++) {
        struct helper *h = &active_helpers[i];
        h->id = i + 1;
        h->board = *board;
        h->num_moves = num_moves;
        memcpy(h->possible, possible, num_moves * sizeof(move_t));
        if (pthread_create(&h->thread, NULL, helper_main, h) != 0) {
            break;
        }
        h->started = true;
        created++;
    }
    active_helper_count = created;
}

// main_depth: the depth the main thread completed (+1 if the interrupted iteration already changed its mind), main_move its move.
// the deepest thread has the most information, so its move is played. a thread must be strictly deeper than the main thread to
// replace it (the main thread is the one that reports, and a tie changes nothing), equal depths among helpers go to the better
// score. a mate for us in the main thread's result is never overruled
void stop_helpers(int main_depth, int main_score, move_t main_move, struct helper_choice *choice) {
    if (choice) {
        choice->valid = false;
    }
    if (!active_helpers) {
        return;
    }
    search_stopped = true;
    int count = active_helper_count;
    long long total = 0;
    for (int i = 0; i < count; i++) {
        if (active_helpers[i].started) {
            pthread_join(active_helpers[i].thread, NULL);
        }
        total += active_helpers[i].nodes;
    }
    if (choice && deepest_thread && main_score < MATE_BOUND) {
        const struct helper *best_h = NULL;
        for (int i = 0; i < count; i++) {
            const struct helper *h = &active_helpers[i];
            if (!h->started || h->done_depth <= main_depth || h->done_len < 1) {
                continue;
            }
            if (!best_h || h->done_depth > best_h->done_depth || (h->done_depth == best_h->done_depth && h->done_score > best_h->done_score)) {
                best_h = h;
            }
        }
        if (best_h && (move_t)best_h->done_pv[0] != main_move) {
            choice->valid = true;
            choice->depth = best_h->done_depth;
            choice->score = best_h->done_score;
            choice->len = best_h->done_len;
            memcpy(choice->pv, best_h->done_pv, best_h->done_len * sizeof(int));
        }
    }
    finished_helper_nodes += total;
    active_helper_count = 0;
    free(active_helpers);
    active_helpers = NULL;
}
