#ifndef MOVEPICK_H
#define MOVEPICK_H

/*
move ordering. the alpha beta search only skips work when it finds a good move early (a move that is good enough to cut
the node off makes the remaining moves unnecessary), so every move of a node gets a score and the search tries the
highest scores first (score_moves in movepick.c, pick_move below). the order is:
    1. the table move: the best move that an earlier search of this position found
    2. good captures and promotions, the most valuable victim first and the least valuable capturing piece first
       (a pawn that takes a queen comes before a queen that takes a pawn). a capture is "good" when the static
       exchange evaluation (see.c) says that it does not lose material
    3. the two killer moves: quiet moves that caused a cutoff in another branch at the same ply. a move that refutes
       one line often refutes its siblings as well (history.h)
    4. bad captures, which lose material
    5. all other (quiet) moves, ordered by the history heuristic (history.h: how often did the move cause cutoffs
       in earlier searches?), with the piece square tables of the evaluation as a tie breaker
this file also has the small helper functions that classify moves
*/
#include "search.h"
#include "history.h"
#include "move_generation.h"

// STAGED_MOVEGEN 1: the moves of a node are generated and scored in stages by the move picker below (table move, captures, killers,
// bad captures, quiet moves), so that a node that is cut off early never generates or scores the quiet moves. 0: all moves are generated
// and scored at once (score_moves + pick_move), the way the engine did it before.
// STABLE_TIE 1 (STAGED_MOVEGEN 0 only, a test build): moves with the same score come in the order in which they were generated. pick_move (a
// selection sort with swaps) does not keep that order: what equal scores do depends on the swaps of the earlier picks, which a staged
// generator can not reproduce. the staged picker always orders equal scores by generation order, so with STABLE_TIE the old pipeline and the
// staged one search exactly the same tree. that is how the picker is tested (same node counts)
#ifndef STAGED_MOVEGEN
#define STAGED_MOVEGEN 1
#endif
#ifndef STABLE_TIE
#define STABLE_TIE 0
#endif
// MP_SNAPSHOT 1 (test build): the quiet moves are generated and scored when the node starts, like the old pipeline does it. the staged picker
// normally scores them when the quiet stage comes up, which is after the earlier moves were searched and may have changed the history table.
// with MP_SNAPSHOT 1 the picker has to search exactly the same tree as STAGED_MOVEGEN=0 STABLE_TIE=1, which tests everything else
#ifndef MP_SNAPSHOT
#define MP_SNAPSHOT 0
#endif

#ifndef VERIFY_GIVESCHECK
#define VERIFY_GIVESCHECK 0 // debug build: every move of the search and of perft (without bulk counting) checks gives_check() against do_move + is_in_check_light
#endif

// the score of a move class, the classes are far enough apart that the score inside a class (victim value, history) never
// moves a move into another class
#define SCORE_TABLE_MOVE    2000000
#define SCORE_GOOD_CAPTURE  1000000
#define SCORE_KILLER_1       900000
#define SCORE_KILLER_2       800000
#define SCORE_BAD_CAPTURE    500000

// true if the move gives check, i.e. the side that does not move is in check after it: the same result as do_move() followed by
// is_in_check_light(), without making the move. the position before the move is legal (the side that does not move is not in check),
// so only lines that the move changes can give check: the moved piece itself (knight and pawn: its attacks; slider: its rays after the
// move, a promotion counts as the new piece), a slider of ours whose ray is opened by the square the piece left, by the captured
// en passant pawn or by the rook of a castling move (that rook is a slider on its new square). only for legal moves.
static inline bool gives_check(const struct bitboards *BB, move_t move) {
    const int f = m_from(move), t = m_to(move);
    const bool white = BB->color == WHITE;
    const int us = white ? 0 : 6, them = white ? 6 : 0;
    const int ksq = lsb(BB->bits[white_king + them]);
    const u64 king_bit = 1ULL << ksq;
    const int piece = BB->piece_list[f];
    int type = piece < 0 ? -piece : piece; // 1 pawn, 2 knight, 3 bishop, 4 rook, 5 queen, 6 king
    if (m_promo(move)) {
        type = m_promo(move) + 1;
    }
    const u64 from_bit = 1ULL << f, to_bit = 1ULL << t;
    u64 occ = (BB->bits[all_pieces] & ~from_bit) | to_bit;
    u64 bq = (BB->bits[white_bishops + us] | BB->bits[white_queens + us]) & ~from_bit;
    u64 rq = (BB->bits[white_rooks + us] | BB->bits[white_queens + us]) & ~from_bit;
    if (piece == (white ? 1 : -1) && t == BB->en_passant_square) {
        occ &= ~(1ULL << (white ? t - 8 : t + 8)); // the captured pawn disappears
    }
    if (m_castle(move)) {
        const int king_side = m_castle(move) == KINGSIDE;
        const int s = (white ? 7 : 63) - king_side * 7; // the rook goes from s to o
        const int o = (white ? 4 : 60) - king_side * 2;
        occ ^= (1ULL << s) | (1ULL << o);
        rq = (rq & ~(1ULL << s)) | (1ULL << o);
    }
    switch (type) {
        case 1: {
            const u64 attacks = white ? (((to_bit << 7) & not_leftmost_file_mask) | ((to_bit << 9) & not_rightmost_file_mask))
                                      : (((to_bit >> 7) & not_rightmost_file_mask) | ((to_bit >> 9) & not_leftmost_file_mask));
            if (attacks & king_bit) return true;
            break;
        }
        case 2:
            if (knight_attacks[t] & king_bit) return true;
            break;
        case 3: bq |= to_bit; break;
        case 4: rq |= to_bit; break;
        case 5: bq |= to_bit; rq |= to_bit; break;
        default: break; // the king never gives check itself
    }
    if (bq && (generate_bishop_moves(ksq, occ) & bq)) return true;
    if (rq && (generate_rook_moves(ksq, occ) & rq)) return true;
    return false;
}

// captures (including en passant) and promotions
static inline bool is_tactical(const struct bitboards *BB, move_t move) {
    int t = m_to(move);
    if (BB->piece_list[t] || m_promo(move)) {
        return true;
    }
    int piece = BB->piece_list[m_from(move)];
    return (piece == 1 || piece == -1) && t == BB->en_passant_square;
}

void score_moves(const move_t *moves, int *scores, int num_moves, const struct bitboards *BB, move_t table_move, int ply_index, u64 attacks);
void merge_sort_array_dec(move_t moves[], int values[], int left, int right); // sorts the moves by decreasing value

// moves the best remaining move to position i. this is cheaper than sorting everything
// because most nodes are cut off after the first few moves
static inline void pick_move(move_t *moves, int *scores, int i, int num_moves) {
    int best = i;
    for (int j = i + 1; j < num_moves; j++) {
        if (scores[j] > scores[best]) {
            best = j;
        }
    }
    if (best != i) {
        move_t m = moves[i]; moves[i] = moves[best]; moves[best] = m;
        int v = scores[i]; scores[i] = scores[best]; scores[best] = v;
    }
}

// like pick_move, but moves with the same score come in the order of order[] (the position of the move in the generated list)
static inline void pick_move_stable(move_t *moves, int *scores, unsigned char *order, int i, int num_moves) {
    int best = i;
    for (int j = i + 1; j < num_moves; j++) {
        if (scores[j] > scores[best] || (scores[j] == scores[best] && order[j] < order[best])) {
            best = j;
        }
    }
    if (best != i) {
        move_t m = moves[i]; moves[i] = moves[best]; moves[best] = m;
        int v = scores[i]; scores[i] = scores[best]; scores[best] = v;
        unsigned char o = order[i]; order[i] = order[best]; order[best] = o;
    }
}

/*
the staged move picker. mp_next returns the moves of a node one at a time in the same order that score_moves + pick_move give, and 0 when
there are no more. the stages:
    table move (only if it is legal here: the table compares only some bits of the hash, so the move may belong to another position)
    good captures and promotions, best first (the static exchange evaluation of a capture that gives up a more valuable piece is only worked
        out when the capture comes up; a losing one is set aside for the bad capture stage)
    the two killer moves (only if they are legal and quiet here)
    bad captures
    quiet moves, ordered by history and piece square tables
*bad is set to 1 for a move of the bad capture stage (a capture that loses material according to the static exchange evaluation).
the order inside a group of equal scores is the order of generation (see STABLE_TIE)
*/
struct move_picker {
    struct bitboards *BB;
    u64 checkers, pinned, attacks;
    const u8 *pinners;
    move_t tt, k1, k2;
    int stage;
    int n;    // moves left at the front of the list
    int nbad; // bad captures that were set aside at the back of the list
    move_t moves[MAX_NUM_MOVES];
    int scores[MAX_NUM_MOVES];
    unsigned char order[MAX_NUM_MOVES]; // where the move was in the generated list (decides between equal scores)
#if MP_SNAPSHOT
    move_t snap_moves[MAX_NUM_MOVES];
    int snap_scores[MAX_NUM_MOVES];
    unsigned char snap_order[MAX_NUM_MOVES];
    int snap_n;
#endif
};

// ply_index < 0 or >= MAX_KILLER_PLY: no killers
static inline void mp_init(struct move_picker *mp, struct bitboards *BB, move_t table_move, int ply_index, u64 checkers, u64 pinned, const u8 *pinners, u64 attacks) {
    mp->BB = BB;
    mp->checkers = checkers;
    mp->pinned = pinned;
    mp->pinners = pinners;
    mp->attacks = attacks;
    mp->tt = table_move;
    const bool use_killers = ply_index >= 0 && ply_index < MAX_KILLER_PLY;
    mp->k1 = use_killers ? killers[ply_index][0] : 0;
    mp->k2 = use_killers ? killers[ply_index][1] : 0;
    mp->stage = 0;
    mp->n = 0;
    mp->nbad = 0;
}

move_t mp_next(struct move_picker *mp, int *bad);

#endif
