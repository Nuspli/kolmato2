#include <pthread.h>
#include <math.h>
#include "search.h"
#include "search_shared.h"
#include "history.h"
#include "movepick.h"

// the tuned search parameters (margins, reductions, time management) are in params.def. the constants below are fixed.
// each of them can be overridden when compiling with -DNAME=value, the rejected values that were tried are documented somewhere. probably in the README.
#ifndef IIR_MIN_DEPTH
#define IIR_MIN_DEPTH 4 // internal iterative reduction: no table move at this depth or more -> search one ply less
#endif
#ifndef LMP_MAX_DEPTH
#define LMP_MAX_DEPTH 3 // late move pruning is used up to this depth
#endif
#ifndef LMP_DIV
#define LMP_DIV 1 // late move pruning limit = (LMP_BASE + depth * depth) / LMP_DIV
#endif
#ifndef FUT_MAX_DEPTH
#define FUT_MAX_DEPTH 3 // futility pruning is used up to this depth
#endif
#ifndef VERIFY_TT_EVAL
#define VERIFY_TT_EVAL 0 // debug build: every static eval taken from the table is compared with evaluate() (counts and prints mismatches)
#endif

#ifndef BAD_CAPTURE_PRUNE_DEPTH
#define BAD_CAPTURE_PRUNE_DEPTH 3 // losing captures are skipped up to this depth (not at pv nodes)
#endif
#ifndef ASP_DELTA
#define ASP_DELTA 30 // half width of the first aspiration window
#endif
#ifndef ASP_MIN_DEPTH
#define ASP_MIN_DEPTH 4
#endif
#define MAX_TRACKED_CAPS 32
#define EVAL_NONE TT_NO_EVAL // no static eval at this ply (the side was in check); the same value marks "no evaluation" in a table entry
#ifndef RFP_MAX_DEPTH
#define RFP_MAX_DEPTH 6 // reverse futility pruning is used up to this depth
#endif
// correction history: the static evaluation misses things that depend on the structure (pawns, pieces) of the
// position. every node that finishes with a score that is a real bound for its static evaluation teaches a small table (keyed by
// the pawn structure and by the pieces of each side) how far the search score was from the static evaluation; the next time a
// position with the same keys is evaluated that average is added. the table keeps the RAW static evaluation (the correction
// changes during the search, a stored corrected value would not be reproducible)
#define MAX_SEARCH_PLY (MAX_PLY - 2) // hard limit: a node this far from the root returns its static evaluation (the pv tables have MAX_PLY entries)
#define MAX_EXTENSION_PLY 40   // no check extensions beyond this ply (keeps the search finite)

// how much a move is reduced depending on the depth and the index of the move in the move order
int lmr_table[64][64];

void init_lmr_table(void) {
    for (int d = 1; d < 64; d++) {
        for (int m = 1; m < 64; m++) {
            // in 1/1024 ply
            lmr_table[d][m] = (int)((LMR_BASE_X100 / 100.0 + log((double)d) * log((double)m) / (LMR_DIV_X100 / 100.0)) * 1024.0);
        }
    }
}

// does the side to move have anything but pawns and the king? without pieces null move pruning is unsafe because of zugzwang
static inline bool has_non_pawn_material(const struct bitboards *BB) {
    if (BB->color == WHITE) {
        return (BB->bits[white_knights] | BB->bits[white_bishops] | BB->bits[white_rooks] | BB->bits[white_queens]) != 0;
    }
    return (BB->bits[black_knights] | BB->bits[black_bishops] | BB->bits[black_rooks] | BB->bits[black_queens]) != 0;
}

#if VERIFY_TT_EVAL
// debug: rebuilds hash, material and piece square sums of the position from the piece list and says what differs from the incrementally updated board
// debug: remembers the position of every hash that is seen (direct mapped) and reports two different positions with the same 64 bit hash
struct seen_position { u64 hash; signed char pieces[64]; signed char color; };
static struct seen_position seen_positions[1 << 22];
static long long hash_collisions, hash_checks;
static void check_hash_collision(const struct bitboards *BB) {
    struct seen_position *s = &seen_positions[BB->hash & ((1 << 22) - 1)];
    hash_checks++;
    if (s->hash == BB->hash) {
        bool same = s->color == (signed char)BB->color;
        for (int i = 0; i < 64 && same; i++) same = s->pieces[i] == (signed char)BB->piece_list[i];
        if (!same) {
            if (++hash_collisions <= 5) {
                fprintf(stderr, "HASH COLLISION: hash %016llx, two different positions (side %d / %d)\n", (unsigned long long)BB->hash, s->color, (int)BB->color);
                for (int r = 7; r >= 0; r--) {
                    for (int pass = 0; pass < 2; pass++) {
                        fprintf(stderr, pass ? "   |  " : "   ");
                        for (int f = 7; f >= 0; f--) {
                            int p = pass ? BB->piece_list[r * 8 + f] : s->pieces[r * 8 + f];
                            fprintf(stderr, "%c", p == 0 ? '.' : "PNBRQKpnbrqk"[p > 0 ? p - 1 : -p + 5]);
                        }
                    }
                    fprintf(stderr, "\n");
                }
            }
            return;
        }
    }
    s->hash = BB->hash; s->color = (signed char)BB->color;
    for (int i = 0; i < 64; i++) s->pieces[i] = (signed char)BB->piece_list[i];
}
static void report_board_drift(const struct bitboards *BB) {
    struct bitboards copy = *BB;
    char castle[8] = "";
    if (BB->white_castle_king_side) strcat(castle, "K");
    if (BB->white_castle_queen_side) strcat(castle, "Q");
    if (BB->black_castle_king_side) strcat(castle, "k");
    if (BB->black_castle_queen_side) strcat(castle, "q");
    if (!castle[0]) strcpy(castle, "-");
    char ep[4] = "-";
    if (BB->en_passant_square >= 0) { ep[0] = 'a' + (7 - BB->en_passant_square % 8); ep[1] = '1' + BB->en_passant_square / 8; ep[2] = 0; }
    reset_boards_light(&copy);
    for (int i = 0; i < 64; i++) copy.piece_list[i] = BB->piece_list[i];
    init_boards_light(&copy, BB->color == WHITE, castle, ep);
    fprintf(stderr, "   drift: hash %s, material %s/%s, pst open %s/%s, pst end %s/%s (live %llx rebuilt %llx; ep %s castle %s)\n",
            copy.hash == BB->hash ? "ok" : "DIFF",
            copy.white_material == BB->white_material ? "ok" : "DIFF", copy.black_material == BB->black_material ? "ok" : "DIFF",
            copy.white_position_opening == BB->white_position_opening ? "ok" : "DIFF", copy.black_position_opening == BB->black_position_opening ? "ok" : "DIFF",
            copy.white_position_endgame == BB->white_position_endgame ? "ok" : "DIFF", copy.black_position_endgame == BB->black_position_endgame ? "ok" : "DIFF",
            (unsigned long long)BB->hash, (unsigned long long)copy.hash, ep, castle);
}
#endif

_Thread_local int pv_len[MAX_PLY];

_Thread_local int pv_table[MAX_PLY][MAX_PLY];

// true as soon as the search ran out of time. every function that sees this returns immediately
// and must not store anything into the transposition table, because the values are garbage
_Atomic bool search_stopped = false; // shared by all search threads

// every search thread has its own board, ply, history, killers and principal variation (they are _Thread_local), only the
// transposition tables and the stop flag are shared. the main thread is the one that called iter_deepening: it decides when
// to stop, prints, and its result is the move that is played. the other threads (helpers) search the same position with
// their own iterative deepening to fill the shared tables with useful results (lazy SMP)
_Thread_local bool is_main_thread = true;
int search_threads = 1;                 // set by the uci option Threads
int deepest_thread = 1;                 // set by the uci option DeepestThread: play the move of the deepest thread, not always the main thread's

void reset_search() {
    static bool lmr_initialized = false;
    if (!lmr_initialized) {
        init_lmr_table();
        lmr_initialized = true;
    }
    search_stopped = false;
    threads_reset();
    history_new_search();
    table_new_search(trans_table);
    table_new_search(quiet_table);
    ply = 0;
    eval_calls = 0;
    move_calls = 0;
    possible_calls = 0;
    quiescence_calls = 0;
    nodes = 0;
    quiet_nodes = 0;
    transpositions = 0;
    quiet_transpositions = 0;
    memset(search_history, 0, sizeof(search_history));
    memset(pv_len, 0, sizeof(pv_len));
    memset(pv_table, 0, sizeof(pv_table));
}

/*
quiescence search: the normal search stops at a fixed depth, but evaluating a position in the middle of a capture sequence
is misleading (a queen that can be taken for free next move still looks like a queen). so at the end of the normal
search only captures are searched until the position is quiet, and only then it is evaluated.
the side to move does not have to capture ("stand pat"): its static evaluation is a lower bound for the score, because
it can always pick a quiet move instead. that value is returned right away when it already reaches beta, and it
raises alpha otherwise. the same alpha beta window as in nega_max is used.
captures are ordered by the static exchange evaluation (see.c), the search stops at the first losing capture,
and captures that can not bring the score up to alpha even when the captured piece is won for free are skipped
(delta pruning)
*/
int quiescence_search(struct bitboards *BITBOARDS, int alpha, int beta, int depth) {
    quiet_visits++;
    quiet_nodes++;

    if (search_stopped) {
        return 0;
    }

    // every TIME_CHECK_MASK + 1 nodes we check if the time is up
    if (!(quiet_nodes & TIME_CHECK_MASK) && should_stop()) {
        search_stopped = true;
        return 0;
    }

    int value;
    move_t table_move = 0;
    struct hash_entry entry;

    // the depth in quiescence search is meaningless (it is not a depth limit), so it is not used for the table
#if VERIFY_TT_EVAL
    check_hash_collision(BITBOARDS);
#endif
    const bool quiet_hit = table_probe(quiet_table, BITBOARDS->hash, 0, &entry);
    if (quiet_hit) {
        table_move = entry.best_move;
        int flag = entry_flag(&entry);
        if (flag == EXACT || (flag == UPPERBOUND && entry.value <= alpha) || (flag == LOWERBOUND && entry.value >= beta)) {
            quiet_transpositions++;
            return entry.value;
        }
    }

    // start with the normal evaluation since the players can not be forced to capture.
    // this is done before generating the captures because most nodes are cut off right here
    // (the table keeps the static evaluation, it is the same every time the position is reached)
    int raw_value;
    if (quiet_hit && entry.static_eval != TT_NO_EVAL) {
        value = entry.static_eval;
#if VERIFY_TT_EVAL
        if (value != evaluate(BITBOARDS)) {
            fprintf(stderr, "TT EVAL MISMATCH (quiescence): stored %d computed %d key %08x\n", value, evaluate(BITBOARDS), entry.key);
            report_board_drift(BITBOARDS);
        }
#endif
    } else {
        eval_calls++;
        value = evaluate(BITBOARDS);
    }
    raw_value = value; // what the table keeps
    {
        struct corr_idx ci;
        corr_keys(BITBOARDS, &ci);
        value = corr_clamp(value + corr_hist_get(BITBOARDS, &ci));
    }

    if (value >= beta) {
        table_store(quiet_table, BITBOARDS->hash, 0, value, LOWERBOUND, 0, 0, raw_value);
        return value;
    }

    int stand_pat = value;
    int best = stand_pat;
    if (value > alpha) {
        alpha = value;
    }

    move_t moves[MAX_NUM_MOVES];
    int move_count = 0;
    
    u64 checkers;
    is_in_check(BITBOARDS, &checkers);
    u64 pinned = 0;
    u8 pinners[64] = {0};
    get_pins(BITBOARDS, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(BITBOARDS);
    move_count = get_captures(BITBOARDS, &moves[0], checkers, pinned, &pinners[0], attacks);

    int scores[MAX_NUM_MOVES];
    score_moves(moves, scores, move_count, BITBOARDS, table_move, -1, 0);
    
    int flag = UPPERBOUND;
    move_t best_move = 0;
    
    for (int i = 0; i < move_count; i++) {

        pick_move(moves, scores, i, move_count);
        move_t move = moves[i];

        // the captures are ordered, so after the first losing capture all other ones lose material too
        if (scores[i] >= SCORE_BAD_CAPTURE && scores[i] < SCORE_GOOD_CAPTURE) {
            break;
        }

        // delta pruning: if the stand pat value plus the value of the captured piece plus a safety margin (DELTA_MARGIN) is
        // still not above alpha, even a free capture would not help, so the capture is not searched
        if (!m_promo(move)) {
            int captured = BITBOARDS->piece_list[m_to(move)];
            int gain = captured ? material_values[(captured < 0 ? -captured : captured) - 1] : PAWNVALUE;
            if (stand_pat + gain + DELTA_MARGIN <= alpha) {
                continue;
            }
        }

        struct undo undo;
        // the data that is necessary to undo the move later is stored in the undo struct
        do_move(move, BITBOARDS, &undo);
        table_prefetch(quiet_table, BITBOARDS->hash);

        // the side to move changed, so the score is negated and the window is swapped (-beta, -alpha) as in nega_max
        int value = -quiescence_search(BITBOARDS, -beta, -alpha, depth - 1);

        undo_move(move, BITBOARDS, &undo);

        if (search_stopped) {
            return 0;
        }
                
        // update alpha and try to prune

        if (value > best) {
            best = value;
        }

        if (value >= beta) {
            table_store(quiet_table, BITBOARDS->hash, 0, value, LOWERBOUND, move, 0, raw_value);
            return value;
        }

        if (value > alpha) {
            alpha = value;
            flag = EXACT;
            best_move = move;
        }
    }

    // return the best guaranteed score
    table_store(quiet_table, BITBOARDS->hash, 0, alpha, flag, best_move, 0, raw_value);
    return best;
}

bool is_repetition(struct bitboards *BITBOARDS) {
    // check if the position has already occoured during the game. only the positions since the last capture or
    // pawn move can be the same, so there is no need to look any further back (half_move_count is the number of plies since then)
    int start = game_moves_played - 1 - half_move_count;
    if (start < 0) {
        start = 0;
    }
    for (int i = start; i < game_moves_played; i++) {
        if (game_history[i] == BITBOARDS->hash) {
            return true;
        }
    }

    for (int i = 0; i < ply; i++) {
        if (search_history[i] == BITBOARDS->hash) {
            return true;
        }
    }
    return false;
}

bool is_three_fold_repetition(struct bitboards *BITBOARDS) {
    // check if the position has occured three times during the game. this is called right after a move was made, before
    // the new position is added to the game history, so the current position counts once more than what is in the history
    int count = 1;
    for (int i = 0; i < game_moves_played; i++) {
        if (game_history[i] == BITBOARDS->hash) {
            count++;
        }
    }
    return count >= 3;
}

// can neither side possibly checkmate? (bare kings, or a single knight or bishop)
bool is_insufficient_material(struct bitboards *BB) {
    if (BB->bits[white_pawns] | BB->bits[black_pawns] | BB->bits[white_rooks] | BB->bits[black_rooks] | BB->bits[white_queens] | BB->bits[black_queens]) {
        return false;
    }
    int minor_pieces = bit_count(BB->bits[white_knights] | BB->bits[white_bishops] | BB->bits[black_knights] | BB->bits[black_bishops]);
    return minor_pieces <= 1;
}

// returns why the game is drawn after the last move was made (before its position is added to the game history), or NULL
const char *draw_reason(struct bitboards *BB) {
    if (is_three_fold_repetition(BB)) {
        return "threefold repetition";
    }
    if (is_insufficient_material(BB)) {
        return "insufficient material";
    }
    return NULL;
}

// futility pruning margin: how much a quiet move can be expected to improve the evaluation at most, depending on the
// remaining depth. the more plies are left, the more can still happen (see the use in nega_max)
static inline int futility_margin_for(int depth) {
    return depth <= 0 ? 0 : FUT_BASE + FUT_PER_DEPTH * depth;
}

// the static evaluation of the node at each ply of the current line, used to find out if the position is improving
static _Thread_local int static_eval_at[MAX_SEARCH_PLY + 2]; // EVAL_NONE when the side to move was in check

int nega_max(struct bitboards *BITBOARDS, int depth, int alpha, int beta, bool allow_null_move, bool cut_node) {
    /*
    negamax with alpha beta pruning and the many extra tricks that make a chess search fast.

    negamax works like the classic minimax (both sides pick the move that is best for them), but a score is always seen
    from the side to move: what is good for one side is exactly as bad for the other, so the score of a move is the
    negated score of the position it leads to, and there is no need to treat the two players differently.

    alpha and beta are the bounds of the window that the score of this node is searched in. alpha is the score the side
    to move is already guaranteed to reach with the moves searched so far. beta is the score at which the opponent would
    avoid this position, because earlier in the tree they have a move that is better for them. when a move scores at
    least beta (a "beta cutoff" or "fail high") the other moves do not have to be searched, the opponent will not allow
    this position anyway. most of the speed comes from trying the best moves first, so that these cutoffs happen early.
    a score outside of the window is only a bound ("the score is at least / at most this"), not the exact value.

    principal variation search: the first move is searched with the full window. every other move is expected to be
    worse, so it is first searched with a null window (beta = alpha + 1), which only answers "is this move better than
    the best one so far?" and is much cheaper. only if the answer is yes, it is searched again with the full window.

    node types: a pv node (full window) is on the line that is currently believed to be best. a cut node is expected to
    fail high, so one good move is enough and the move ordering should find it first. an all node is expected to fail low,
    so all of its moves have to be looked at. the children of a cut node are all nodes and the other way round. the
    cut_node argument carries this expectation down, it decides how much the late moves are reduced.

    at its core it looks like this:

    if (depth <= 0) {
        return quiescence_search(BITBOARDS, alpha, beta, 0);
    }

    generate moves

    for each (legal) move {
        do move
        value = -nega_max(BITBOARDS, depth - 1, -beta, -alpha)
        undo move

        if (value >= beta) {
            return beta;
        }
        if (value > alpha) {
            alpha = value;
        }
    }

    return alpha
    */
    // the rest is there to search the promising moves first and to spend less time on the unpromising ones

    pv_len[ply] = ply;

    nodes++;
    visits++;

    int value;
    bool futility_pruning = false;

    if (search_stopped) {
        return 0;
    }

    if (ply >= MAX_SEARCH_PLY) {
        return evaluate(BITBOARDS);
    }

    // every TIME_CHECK_MASK + 1 nodes we check if the time is up
    if (!(nodes & TIME_CHECK_MASK) && should_stop()) {
        search_stopped = true;
        return 0;
    }

    if (is_repetition(BITBOARDS)) {
        // as repetition mostly leads to repetition we can assume that the position is a draw
        return 0;
    }

    move_t table_move = 0;
    struct hash_entry entry;
#if VERIFY_TT_EVAL
    check_hash_collision(BITBOARDS);
#endif
    /*
    transposition table: the same position can be reached by different move orders ("transpositions"). every searched
    position is stored in a hash table together with its score, the depth it was searched to and its best move. when the
    position comes up again and the stored search was at least as deep as the one we are about to do, its result can be
    reused. what the stored score means depends on how the stored node ended:
        EXACT        the score is the true value of the position
        LOWERBOUND   a move failed high (reached beta), the true value is at least the score
        UPPERBOUND   no move improved alpha (fail low), the true value is at most the score
    a bound can only be used when it lies outside of our window, as then it proves that the node can not change the result.
    otherwise the stored best move is still searched first, which makes cutoffs much more likely
    */
    bool tt_hit = table_probe(trans_table, BITBOARDS->hash, ply, &entry);

    if (tt_hit) {

        table_move = entry.best_move;

        if (entry.depth >= depth) {
            int flag = entry_flag(&entry);
            if (flag == EXACT || (flag == UPPERBOUND && entry.value <= alpha) || (flag == LOWERBOUND && entry.value >= beta)) {
                transpositions++;
                return entry.value;
            }
        }
    }

    struct undo undo;

    u64 checkers;
    bool in_check = is_in_check(BITBOARDS, &checkers);

    // check extension: when in check there are few legal moves and the search must not be cut short, otherwise forced
    // lines and mates are missed (also at depth 0 where the quiescence search would ignore the check completely)
    if (in_check && ply < MAX_EXTENSION_PLY) {
        depth++;
    }

    if (depth <= 0) {
        // when the depth limit is reached we use quiescence search to get a more accurate evaluation
        quiescence_calls++;
        return quiescence_search(BITBOARDS, alpha, beta, 0);
    }

    // a pv node (principal variation) is on the line that is currently believed to be best. these nodes are searched with
    // a full window. every other node has a null window (beta = alpha + 1), it only answers "is this at least as good as
    // alpha?", which is much cheaper to find out
    const bool is_pv = beta - alpha > 1;

    // internal iterative reduction: without a table move the move ordering is poor, so the node is probably not worth a
    // full depth search. searching it one ply less is cheap and fills the table with a move for the next iteration
    if (IIR_MIN_DEPTH > 0 && table_move == 0 && depth >= IIR_MIN_DEPTH && !in_check) {
        depth--;
    }

    // improving: our static eval is better than at our previous turn (two plies ago, the null move counts as a ply).
    // a position that gets better is more likely to hold, one that gets worse is more likely to fail, so pruning is
    // stricter when not improving. ply 1 and 2 have no usable earlier value (the root does not store one)
    bool improving = false;
    static_eval_at[ply] = EVAL_NONE;
    int raw_eval = EVAL_NONE; // the static evaluation without correction (what the table keeps)
    struct corr_idx corr_idx = {0};

    if (!in_check) {
        // the static evaluation comes from the table when the position is in it (it only depends on the position)
        int static_eval = (tt_hit && entry.static_eval != TT_NO_EVAL) ? entry.static_eval : evaluate(BITBOARDS);
        raw_eval = static_eval;
#if VERIFY_TT_EVAL
        if (tt_hit && entry.static_eval != TT_NO_EVAL && static_eval != evaluate(BITBOARDS)) {
            fprintf(stderr, "TT EVAL MISMATCH (search): stored %d computed %d key %08x ply %d\n", static_eval, evaluate(BITBOARDS), entry.key, ply);
            report_board_drift(BITBOARDS);
        }
#endif
        // correction history: the average error that the search found for positions with this pawn structure / these pieces
        corr_keys(BITBOARDS, &corr_idx);
        static_eval = corr_clamp(static_eval + corr_hist_get(BITBOARDS, &corr_idx));
        int eval = static_eval;
        static_eval_at[ply] = static_eval;
        // the table score of this position is a better estimate of its value than the static evaluation when its bound points
        // the right way (a lower bound above the evaluation, an upper bound below it). improving still uses the static evaluation
        if (tt_hit && abs(entry.value) < MATE_BOUND) {
            const int tf = entry_flag(&entry);
            if (tf == EXACT || (tf == LOWERBOUND && entry.value > eval) || (tf == UPPERBOUND && entry.value < eval)) {
                eval = entry.value;
            }
        }
        improving = ply >= 3 && static_eval_at[ply - 2] != EVAL_NONE && eval > static_eval_at[ply - 2];

        /*
        reverse futility pruning (static null move pruning): if the static evaluation is above beta by more than a margin
        that grows with the remaining depth, the opponent would almost certainly avoid this position, so the node is cut
        without searching a move. the margin stands for what the remaining search could take away at most. it is only used
        close to the leaves, and not near mate scores because the evaluation does not know about mate
        */
        if (depth <= RFP_MAX_DEPTH && abs(beta) < MATE_BOUND) {
            int eval_margin = RFP_MARGIN * depth;
            if (eval - eval_margin >= beta) {
                return eval - eval_margin;
            }
        }

        /*
        null move pruning: we pass (make no move) and let the opponent move twice in a row. if even then the opponent can
        not get the score below beta, the position is so good that any real move would be too, and the node is cut without
        searching its moves. the search after the pass is shallower than a normal one (reduced by R plies: more at a higher
        depth or when the evaluation is far above beta) and uses a null window around beta.
        it is not done
          - without a piece besides pawns and the king: in zugzwang positions every move makes things worse and passing
            would be the best choice, so "a pass is fine" proves nothing there
          - right after another null move (allow_null_move): two passes in a row just repeat the position
          - when the table says that the position is worth less than beta
        */
        bool nmp_ok = true;
        // the table already knows that this position is worth less than beta (at about the depth the null move search would reach)
        if (tt_hit && entry_flag(&entry) == UPPERBOUND && entry.value < beta && entry.depth >= depth - 3) {
            nmp_ok = false;
        }
        if (nmp_ok && depth >= 3 && allow_null_move && eval >= beta && abs(beta) < MATE_BOUND && has_non_pawn_material(BITBOARDS)) {

            int reduction = (NMP_R_BASE_Q + depth * NMP_R_SLOPE_Q) >> 8;
            if (NMP_EVAL_DIV > 0) {
                int extra = (eval - beta) / NMP_EVAL_DIV;
                reduction += extra < 3 ? extra : 3;
            }

            BITBOARDS->color = -BITBOARDS->color;
            int ep = BITBOARDS->en_passant_square;
            if (BITBOARDS->en_passant_square >= 0) {
                BITBOARDS->hash ^= ZOBRIST_TABLE[BITBOARDS->en_passant_square][12];
            }
            BITBOARDS->en_passant_square = -1;
            BITBOARDS->hash ^= white_to_move;

            // the null move counts as a ply so that distances to mate, the repetition detection and the killer moves stay consistent
            ply++;
            search_history[ply] = BITBOARDS->hash;

            value = -nega_max(BITBOARDS, depth - 1 - reduction, -beta, -beta + 1, false, !cut_node);

            ply--;

            BITBOARDS->hash ^= white_to_move;
            BITBOARDS->color = -BITBOARDS->color;
            BITBOARDS->en_passant_square = ep;
            if (BITBOARDS->en_passant_square >= 0) {
                BITBOARDS->hash ^= ZOBRIST_TABLE[BITBOARDS->en_passant_square][12];
            }

            if (search_stopped) {
                return 0;
            }

            if (value >= beta) {
                return value >= MATE_BOUND ? beta : value; // a mate score from a null move search is not trustworthy
            }
        }

        /*
        futility pruning: close to the leaves, a quiet move (no capture, no promotion, no check) rarely improves the
        evaluation by much. when the static evaluation plus a margin for the remaining depth is still not above alpha, the
        quiet moves of this node are skipped (this only sets the flag, the moves are skipped in the loop below)
        */
        if (depth <= FUT_MAX_DEPTH && abs(alpha) < MATE_BOUND && eval + futility_margin_for(depth) <= alpha) {
            futility_pruning = true;
        }
    }

    u64 pinned = 0;
    u8 pinners[64] = {0};
    get_pins(BITBOARDS, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(BITBOARDS);

#if STAGED_MOVEGEN
    // the moves are generated and scored stage by stage while the loop asks for them (movepick.c): a node that is cut off by the table move
    // or the first capture never generates its quiet moves. move_count is the number of moves that were returned (0 at the end = no legal move)
    struct move_picker picker;
    mp_init(&picker, BITBOARDS, table_move, ply, checkers, pinned, pinners, attacks);
    int move_count = 0;
#else
    move_t moves[MAX_NUM_MOVES];
    int move_count;
    move_count = get_moves(BITBOARDS, &moves[0], checkers, pinned, &pinners[0], attacks);

    int scores[MAX_NUM_MOVES];
    score_moves(moves, scores, move_count, BITBOARDS, table_move, ply, attacks);
#if STABLE_TIE
    unsigned char order[MAX_NUM_MOVES];
    for (int k = 0; k < move_count; k++) {
        order[k] = (unsigned char)k;
    }
#endif
#endif

    const int side = BITBOARDS->color == WHITE ? 0 : 1;
    move_t quiets_tried[MAX_TRACKED_QUIETS];
    int num_quiets = 0;

    int flag = UPPERBOUND;
    int best_move = 0;
    int best_value = -INF;      // best value of any searched move (fail-soft)
    bool pruned_any = false;    // a move was skipped by futility / late move pruning

    // the table move is a capture or promotion: the other (quiet) moves are less likely to be better than it
    const bool table_move_tactical = table_move != 0 && is_tactical(BITBOARDS, table_move);

#if STAGED_MOVEGEN
    for (int i = 0; ; i++) {
        int picked_bad;
        move_t move = mp_next(&picker, &picked_bad);
        if (move == 0) {
            break;
        }
        move_count = i + 1;
        bool tactical = is_tactical(BITBOARDS, move);
        // a capture that loses material according to the static exchange evaluation (the table move is never counted as one)
        bool bad_capture = picked_bad != 0;
#else
    for (int i = 0; i < move_count; i++) {

#if STABLE_TIE
        pick_move_stable(moves, scores, order, i, move_count);
#else
        pick_move(moves, scores, i, move_count);
#endif
        move_t move = moves[i];
        bool tactical = is_tactical(BITBOARDS, move);
        // a capture that loses material according to the static exchange evaluation (the table move is never counted as one)
        bool bad_capture = tactical && scores[i] >= SCORE_BAD_CAPTURE && scores[i] < SCORE_GOOD_CAPTURE;
#endif

        /*
        skipping moves before they are made. a move is skipped when
          - it is quiet and the futility pruning flag is set (see above), or
          - it is quiet and so late in the move order that it is unlikely to be good (late move pruning: the moves are
            sorted from most to least promising, so the later a move comes the less it is worth looking at; the number of
            moves that are still tried grows with the depth). not in pv nodes, and not with a mate score in the window, or
          - it is a capture that the static exchange evaluation (see.c) says loses material (not in pv nodes).
        the first move is always searched, and so is every move that gives check or is made while in check.
        whether a move gives check is found out before making it (gives_check() gives the same answer as do_move followed by
        is_in_check_light, but most of the moves that get here are pruned, so making them first would waste time)
        */
        if ((!tactical || bad_capture) && i > 0 && !in_check) {
            int lmp_limit = (LMP_BASE_Q + depth * depth * LMP_QUAD_Q) / (16 * LMP_DIV);
            bool prune = (!tactical && (futility_pruning || (!is_pv && depth <= LMP_MAX_DEPTH && i >= lmp_limit && alpha > -MATE_BOUND))) ||
                         (bad_capture && !is_pv && depth <= BAD_CAPTURE_PRUNE_DEPTH && alpha > -MATE_BOUND);
            if (prune && !gives_check(BITBOARDS, move)) {
                pruned_any = true;
                continue;
            }
        }
#if VERIFY_GIVESCHECK
        const bool verify_gives_check = gives_check(BITBOARDS, move);
#endif
        do_move(move, BITBOARDS, &undo);
#if VERIFY_GIVESCHECK
        if (verify_gives_check != is_in_check_light(BITBOARDS)) {
            fprintf(stderr, "GIVESCHECK MISMATCH (search): move %d->%d promo %d castle %d predicted %d\n", m_from(move), m_to(move), (int)m_promo(move), (int)m_castle(move), (int)verify_gives_check);
            abort();
        }
#endif
        // the child will look up its position right away, start loading it (leaf nodes go on to quiescence search)
        table_prefetch(trans_table, BITBOARDS->hash);
        if (depth <= 1) {
            table_prefetch(quiet_table, BITBOARDS->hash);
        }

        if (i == 0) {
            // the first move is searched with the full window. in a pv node its child is a pv node as well, otherwise the
            // child of a cut node is expected to be an all node and the child of an all node a cut node
            value = -nega_max(BITBOARDS, depth - 1, -beta, -alpha, true, is_pv ? false : !cut_node);
        } else {
            /*
            late move reductions: the moves are sorted from most to least promising, so a move late in the list is unlikely
            to be the best one. it is first searched with a reduced depth and a null window. only if even that search finds
            it better than alpha, it is searched again at full depth (null window), and if it is still better than alpha but
            below beta, once more with the full window to get its exact score.
            the first three moves, captures and promotions (except losing captures), and all moves in check or giving check
            are never reduced. the reduction starts from a table that grows with the depth and the index of the move
            (lmr_table, in 1/1024 ply) and is adjusted: less in pv nodes and for quiet moves with a good history,
            more when the position is not improving, in expected cut nodes and when the table move is a capture
            (the capture is probably the best move, so the quiet moves are even less likely to be)
            */
            int reduction = 0;
            if (i >= 3 && depth >= 3 && !in_check && (!tactical || bad_capture) && !is_in_check_light(BITBOARDS)) {
                reduction = lmr_table[depth < 63 ? depth : 63][i < 63 ? i : 63]; // in 1/1024 ply
                if (is_pv) {
                    reduction -= PV_LMR_Q;
                }
                // quiet moves that often caused cutoffs (high history) are reduced less, moves that were often tried
                // without success (negative history) more. history only exists for quiet moves
                if (HIST_LMR_DIV > 0 && !tactical) {
                    reduction -= (history[side][m_from(move)][m_to(move)][HIST_IDX(attacks, m_from(move), m_to(move))] / HIST_LMR_DIV) * 1024;
                }
                if (!improving && !in_check) {
                    reduction += IMP_LMR_Q;
                }
                if (!tactical) {
                    if (cut_node && !is_pv) {
                        reduction += CUTNODE_LMR_Q;
                    }
                    if (table_move_tactical) {
                        reduction += LMR_TTCAPTURE_Q;
                    }
                }
                reduction = reduction < 0 ? 0 : reduction >> 10;
                if (reduction > depth - 2) {
                    reduction = depth - 2;
                }
            }

            if (reduction > 0) {
                value = -nega_max(BITBOARDS, depth - 1 - reduction, -alpha - 1, -alpha, true, true);
            } else {
                value = alpha + 1;
            }

            if (value > alpha) {
                value = -nega_max(BITBOARDS, depth - 1, -alpha - 1, -alpha, true, !cut_node);
                
                if (value > alpha && value < beta) {
                    value = -nega_max(BITBOARDS, depth - 1, -beta, -alpha, true, false);
                }
            }
        }

        undo_move(move, BITBOARDS, &undo);

        if (search_stopped) {
            return 0;
        }

        if (value > best_value) {
            best_value = value;
        }

        if (value >= beta) {
            if (!tactical) {
                quiet_cutoff(side, depth, move, quiets_tried, num_quiets, attacks);
            }
            table_store(trans_table, BITBOARDS->hash, depth, value, LOWERBOUND, move, ply, raw_eval);
            // a lower bound only says that the static eval was too low when the score is above it; captures say little about the structure
            if (!in_check && !tactical && value > raw_eval && abs(value) < MATE_BOUND) {
                corr_hist_update(BITBOARDS, &corr_idx, value - raw_eval, depth);
            }
            return value;
        }

        if (!tactical && num_quiets < MAX_TRACKED_QUIETS) {
            quiets_tried[num_quiets++] = move;
        }

        if (value > alpha) {
            alpha = value;
            flag = EXACT;
            best_move = move;

            pv_table[ply][ply] = move;
            for (int j = ply + 1; j < pv_len[ply + 1]; j++) {
                pv_table[ply][j] = pv_table[ply + 1][j];
            }
            pv_len[ply] = pv_len[ply + 1];
        }
    }

    if (move_count == 0) {
        if (is_in_check_light(BITBOARDS)) {
            // mate
            return -INF + ply;
        } else {
            // stalemate
            return 0;
        }
    }

    // fail-soft: if every move was searched and none reached alpha, the best value found is a better upper bound than alpha.
    // with skipped (pruned) moves it would not be a real bound, so alpha is used then
    int result = alpha;
    if (flag == UPPERBOUND && !pruned_any && best_value > -INF) {
        result = best_value;
    }
    table_store(trans_table, BITBOARDS->hash, depth, result, flag, best_move, ply, raw_eval);
    // exact scores always teach, an upper bound (fail low) only when it is below the static eval. the best move must not be a capture
    if (!in_check && best_value > -MATE_BOUND && best_value < MATE_BOUND && (best_move == 0 || !is_tactical(BITBOARDS, best_move)) &&
        (flag == EXACT || best_value < raw_eval)) {
        corr_hist_update(BITBOARDS, &corr_idx, best_value - raw_eval, depth);
    }

    return result;
}

// nodes the main thread spent on each root move in the last pass of the current iteration (for the time management)
static move_t root_effort_move[MAX_NUM_MOVES];
static long long root_effort_nodes[MAX_NUM_MOVES];
static int root_effort_count = 0;

// searches the root moves in the given order and returns the best value from the point of view of the side to move.
// the first move gets the full window, all others a null window and are only searched again with the full window
// if they turn out to be better. the values of moves that fail low are therefore only upper bounds.
// the moves are sorted by their values afterwards (if a move was better than alpha), *completed is the number of moves
// that were searched completely, if the search is interrupted the interrupted move is not counted

static int search_root(struct bitboards *bitboards, move_t *possible, int *values, int num_moves, int depth, int alpha, int beta, int *completed) {

    int alpha_start = alpha;
    int best_value = -INF - 1;
    *completed = 0;
    bool best_changed = false; // a move other than the first one of the list became the best move in this pass
    if (is_main_thread) {
        root_effort_count = 0;
    }

    for (int i = 0; i < num_moves; i++) {

        visits = 0;
        quiet_visits = 0;
        long long nodes_before = (long long)(unsigned)nodes + (unsigned)quiet_nodes;

        struct undo undo;
        do_move(possible[i], bitboards, &undo);

        SAY("%s%s: ", notation[m_from(possible[i])], notation[m_to(possible[i])]);
        int t = transpositions;

        int value;
        if (i == 0) {
            value = -nega_max(bitboards, depth, -beta, -alpha, true, false);
        } else {
            value = -nega_max(bitboards, depth, -alpha - 1, -alpha, true, true);
            if (value > alpha && value < beta) {
                value = -nega_max(bitboards, depth, -beta, -alpha, true, false);
            }
        }

        undo_move(possible[i], bitboards, &undo);

        if (search_stopped) {
            // the search of this move was interrupted, so its value is meaningless
            SAY("time out\n");
            break;
        }

        SAY("%-10d quiet: %-10d eval: %d%s transpositions: %d\n", visits, quiet_visits, bitboards->color * value, (value <= alpha || value >= beta) ? " (bound)" : "", transpositions - t);

        values[i] = value;
        *completed = i + 1;
        if (is_main_thread) {
            root_effort_move[root_effort_count] = possible[i];
            root_effort_nodes[root_effort_count] = (long long)(unsigned)nodes + (unsigned)quiet_nodes - nodes_before;
            root_effort_count++;
        }

        if (value > best_value) {
            best_value = value;
        }

        if (value > alpha) {
            if (i > 0) {
                best_changed = true;
            }
            alpha = value;

            pv_table[ply][ply] = possible[i];
            for (int j = ply + 1; j < pv_len[ply + 1]; j++) {
                pv_table[ply][j] = pv_table[ply + 1][j];
            }
            pv_len[ply] = pv_len[ply + 1];
        }

        if (value >= beta) {
            // fail high, the caller has to search again with a wider window
            break;
        }

        // time is up (hard limit), or the planned time is used up (soft limit): this move was searched completely,
        // so it still counts, and the moves that were not searched yet keep the order of the last iteration
        double soft_limit = cur_soft_ms;
        if (best_changed) {
            soft_limit *= TM_CHANGE_X100 / 100.0;
        }
        if (time_is_up() || (soft_limit > 0 && search_elapsed_ms() > soft_limit)) {
            search_stopped = true;
            SAY("time out\n");
            break;
        }
    }

    // only trust the order if something was found that is better than what we expected
    if (*completed > 0 && best_value > alpha_start) {
        merge_sort_array_dec(&possible[0], values, 0, *completed - 1);
    }

    return best_value;
}

#define PV_MAX_LEN 40

// the pv that the search builds is cut short whenever a node was answered by the table. this follows the best moves
// stored in the table from the end of the pv on, so that the reported pv (and the move to ponder on) is complete.
// the search itself is not affected. every table move is checked for being legal because the table only compares 32 bits
void extend_pv_from_table(struct bitboards *bitboards) {
    int len = pv_len[0];
    if (len <= 0 || len >= PV_MAX_LEN) {
        return;
    }
    move_t played[PV_MAX_LEN + 2];
    struct undo undos[PV_MAX_LEN + 2];
    u64 seen[PV_MAX_LEN + 2];
    int made = 0;

    for (int i = 0; i < len; i++) {
        played[made] = (move_t)pv_table[0][i];
        do_move(played[made], bitboards, &undos[made]);
        seen[made] = bitboards->hash;
        made++;
    }

    while (len < PV_MAX_LEN) {
        struct hash_entry e;
        if (!table_probe(trans_table, bitboards->hash, ply, &e) || e.best_move == 0) {
            break;
        }
        move_t moves[MAX_NUM_MOVES];
        u64 checkers;
        is_in_check(bitboards, &checkers);
        u64 pinned = 0;
        u8 pinners[64] = {0};
        get_pins(bitboards, &pinned, &pinners[0]);
        u64 attacks = get_enemy_attack_mask(bitboards);
        int count = get_moves(bitboards, &moves[0], checkers, pinned, &pinners[0], attacks);
        bool legal = false;
        for (int i = 0; i < count; i++) {
            if (moves[i] == e.best_move) {
                legal = true;
                break;
            }
        }
        if (!legal) {
            break;
        }
        played[made] = e.best_move;
        do_move(played[made], bitboards, &undos[made]);
        bool repeated = false;
        for (int k = 0; k < made; k++) {
            if (seen[k] == bitboards->hash) {
                repeated = true;
                break;
            }
        }
        if (repeated) {
            undo_move(played[made], bitboards, &undos[made]);
            break;
        }
        seen[made] = bitboards->hash;
        made++;
        pv_table[0][len++] = (int)e.best_move;
    }

    for (int i = made - 1; i >= 0; i--) {
        undo_move(played[i], bitboards, &undos[i]);
    }
    pv_len[0] = len;
}

// searches one iteration with an aspiration window. once we have a score from the previous depth we expect the new one to be
// close to it, so the root is searched with a narrow window around it (ASP_DELTA on each side). a narrow window makes
// the alpha beta search cut much more. if the result falls outside of the window (fail low or fail high), the search is
// repeated with a wider window on that side. the window grows by half of its size each time, and gets infinite when a mate
// score is involved
int aspiration_search(struct bitboards *board, move_t *possible, int *values, int num_moves, int depth, int best_score, int *completed) {
    int alpha = -INF;
    int beta = INF;
    int delta = ASP_DELTA;

    if (depth >= ASP_MIN_DEPTH && abs(best_score) < MATE_BOUND) {
        alpha = best_score - delta;
        beta = best_score + delta;
    }

    int value;
    while (1) {
        value = search_root(board, possible, values, num_moves, depth, alpha, beta, completed);

        if (search_stopped) {
            break;
        }

        if (value <= alpha && alpha > -INF) {
            // fail low: the position is worse than expected
            beta = (alpha + beta) / 2;
            alpha = alpha - delta;
            if (alpha < -MATE_BOUND) alpha = -INF;
        } else if (value >= beta && beta < INF) {
            // fail high: the position is better than expected
            beta = beta + delta;
            if (beta > MATE_BOUND) beta = INF;
        } else {
            break;
        }

        delta += delta / 2;
        SAY("repeating search...\n");
    }
    return value;
}

move_t iter_deepening(move_t *possible, struct bitboards *bitboards, int num_moves) {
    /*
    iterative deepening: search to depth 1, then 2, then 3 and so on until the time is up. this looks wasteful, but a
    search to depth n costs several times more than depth n - 1, so the earlier iterations are cheap. they are worth it:
    the best move and the table entries of the last iteration give the next one a good move order (which makes the alpha
    beta cutoffs much more effective), the score of the last iteration is the center of the aspiration window, and when the
    time runs out there is always a finished iteration whose best move can be played.
    the root moves are kept sorted by their score from the previous iteration, so the best known move is searched first
    */
    int values[num_moves];

    search_start_time = clock();
    
    int best_score = 0;
    int main_depth = 0; // last iteration the main thread completed
    move_t main_depth_move = 0; // its best move
    int stable_iterations = 0; // iterations in a row with the same best move (time management)
    tm_start_search();

    SAY("move amount: %d\n", num_moves);

    if (num_moves == 1) {
        SAY("forced move: %s%s%s\n", notation[m_from(possible[0])], notation[m_to(possible[0])], promo_suffix[m_promo(possible[0])]);
        return possible[0];
    }

    pv_len[ply] = ply;

    start_helpers(bitboards, possible, num_moves);

    for (int depth = 0; depth < max_depth; depth++) {

        SAY("depth: %d - \n", depth+1);

        int completed = 0;
        aspiration_search(bitboards, possible, values, num_moves, depth, best_score, &completed);

        if (search_stopped) {
            break;
        }

        int previous_score = best_score;
        stable_iterations = (possible[0] == main_depth_move) ? stable_iterations + 1 : 0;
        best_score = values[0];
        main_depth = depth + 1;
        main_depth_move = possible[0];
        extend_pv_from_table(bitboards);

        if (iteration_hook) {
            iteration_hook(depth + 1, values[0], (const move_t *)pv_table[0], pv_len[0]);
        }

        // the scores are printed from white's point of view
        SAY("\n>>> best move: %s%s%s value: %d\n", notation[m_from(possible[0])], notation[m_to(possible[0])], promo_suffix[m_promo(possible[0])], bitboards->color * values[0]);
        SAY(">>> pv: ");

        for (int j = 0; j < pv_len[0]; j++) {
            SAY("%s%s ", notation[m_from(pv_table[0][j])], notation[m_to(pv_table[0][j])]);
        }

        SAY("\n\n");

        memset(pv_table, 0, sizeof(pv_table));
        memset(pv_len, 0, sizeof(pv_len));

        // we deliver mate and the iteration was deep enough to rule out a shorter mate (the mate takes no more plies than the
        // search depth, so every shorter line was searched): searching on cannot improve the move
        if (values[0] >= MATE_BOUND && INF - values[0] <= depth + 1) {
            break;
        }

        if (time_is_up()) {
            break;
        }

        // time management: how long the next iteration may start (timeman.c)
        double best_share = -1; // share of the nodes of this iteration that went into the best move (-1: unknown)
        {
            long long total = 0, best = 0;
            for (int k = 0; k < root_effort_count; k++) {
                total += root_effort_nodes[k];
                if (root_effort_move[k] == possible[0]) best = root_effort_nodes[k];
            }
            if (total > 0 && best > 0) {
                best_share = (double)best / total;
            }
        }
        tm_after_iteration(depth, stable_iterations, previous_score, values[0], best_share);
        if (tm_soft_limit_reached()) {
            break;
        }
    }

    // an interrupted iteration that already found a better move than the last completed one counts as one level deeper
    struct helper_choice choice;
    stop_helpers(main_depth + (possible[0] != main_depth_move && main_depth > 0 ? 1 : 0), best_score, possible[0], &choice);

    if (choice.valid) {
        SAY("deepest thread (depth %d, main thread %d) plays another move\n", choice.depth, main_depth);
        memset(pv_table, 0, sizeof(pv_table));
        memset(pv_len, 0, sizeof(pv_len));
        memcpy(pv_table[0], choice.pv, choice.len * sizeof(int));
        pv_len[0] = choice.len;
        extend_pv_from_table(bitboards);
        // make the played move the first of the root list, and report it as the result of the last iteration
        move_t chosen = (move_t)choice.pv[0];
        for (int i = 0; i < num_moves; i++) {
            if (possible[i] == chosen) {
                move_t t = possible[i];
                for (int j = i; j > 0; j--) possible[j] = possible[j - 1];
                possible[0] = t;
                break;
            }
        }
        if (iteration_hook) {
            iteration_hook(choice.depth, choice.score, (const move_t *)pv_table[0], pv_len[0]);
        }
    }

    SAY("\ntranspositions found: %d (%d quiet)\n", transpositions, quiet_transpositions);

    return possible[0];
}
