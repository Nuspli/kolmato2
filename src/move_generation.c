#include "move_generation.h"

u64 not_rightmost_file_mask = 0xFEFEFEFEFEFEFEFE;
u64 not_leftmost_file_mask = 0x7F7F7F7F7F7F7F7F;

u64 pawn_promotion_mask = 0xFF000000000000FF;

u64 rank[8] = {
    0x00000000000000FF,
    0x000000000000FF00,
    0x0000000000FF0000,
    0x00000000FF000000,
    0x000000FF00000000,
    0x0000FF0000000000,
    0x00FF000000000000,
    0xFF00000000000000
};

u64 file[8] = {
    0x0101010101010101,
    0x0202020202020202,
    0x0404040404040404,
    0x0808080808080808,
    0x1010101010101010,
    0x2020202020202020,
    0x4040404040404040,
    0x8080808080808080
};

u64 adj_files[8] = {
    0x0202020202020202,
    0x0505050505050505,
    0x0A0A0A0A0A0A0A0A,
    0x1414141414141414,
    0x2828282828282828,
    0x5050505050505050,
    0xA0A0A0A0A0A0A0A0,
    0x4040404040404040
};

u64 knight_attacks[64];
u64 king_attacks[64];
u64 bishop_attacks[64][512];
u64 rook_attacks[64][4096];

u64 squares_between[64][64];

int BLTR[64] = {
    7, 8, 9,10,11,12,13,14,
    6, 7, 8, 9,10,11,12,13,
    5, 6, 7, 8, 9,10,11,12,
    4, 5, 6, 7, 8, 9,10,11,
    3, 4, 5, 6, 7, 8, 9,10,
    2, 3, 4, 5, 6, 7, 8, 9,
    1, 2, 3, 4, 5, 6, 7, 8,
    0, 1, 2, 3, 4, 5, 6, 7
};

int TLBR[64] = {
    14,13,12,11,10,9, 8, 7,
    13,12,11,10,9, 8, 7, 6,
    12,11,10,9, 8, 7, 6, 5,
    11,10,9, 8, 7, 6, 5, 4,
    10,9, 8, 7, 6, 5, 4, 3,
    9, 8, 7, 6, 5, 4, 3, 2,
    8, 7, 6, 5, 4, 3, 2, 1,
    7, 6, 5, 4, 3, 2, 1, 0
};

void init_squares_between() {
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 64; j++) {
            u64 mask = 0;
            if (i != j) {
                int a, b;
                if (i < j) {
                    a = i;
                    b = j;
                } else {
                    a = j;
                    b = i;
                }
                if (same_file(a, b)) {
                    for (int k = a + 8; k < b; k += 8) {
                        set_bit(mask, k);
                    }
                } else if (same_rank(a, b)) {
                    for (int k = a + 1; k < b; k++) {
                        set_bit(mask, k);
                    }
                } else if (BLTR[a] == BLTR[b]) {
                    for (int k = a + 9; k < b; k += 9) {
                        set_bit(mask, k);
                    }
                } else if (TLBR[a] == TLBR[b]) {
                    for (int k = a + 7; k < b; k += 7) {
                        set_bit(mask, k);
                    }
                }
            }
            squares_between[i][j] = mask;
        }
    }
}

void init_knight_attacks() {
    for (int i = 0; i < 64; i++) {
        // knight attacks from each of the 64 squares
        u64 knightboard = 0;
        if ((i % 8) < 6 && (i / 8) != 7) {set_bit(knightboard, i+10);} // conditions are to make sure the knight does not go off the board
        if ((i % 8) > 1 && (i / 8) != 0) {set_bit(knightboard, i-10);}
        if ((i % 8) != 7 && (i / 8) < 6) {set_bit(knightboard, i+17);}
        if ((i % 8) != 0 && (i / 8) > 1) {set_bit(knightboard, i-17);}
        if ((i % 8) != 0 && (i / 8) < 6) {set_bit(knightboard, i+15);}
        if ((i % 8) != 7 && (i / 8) > 1) {set_bit(knightboard, i-15);}
        if ((i % 8) > 1 && (i / 8) != 7) {set_bit(knightboard, i+6);}
        if ((i % 8) < 6 && (i / 8) != 0) {set_bit(knightboard, i-6);}

        knight_attacks[i] = knightboard;
    }
}

void init_king_attacks() {
    for (int i = 0; i < 64; i++) {
        u64 kingboard = 0;
        if (i % 8 != 7) {set_bit(kingboard, i+1);}
        if (i % 8 != 0 && i / 8 != 7) {set_bit(kingboard, i+7);}
        if (i / 8 != 7) {set_bit(kingboard, i+8);}
        if (i % 8 != 7 && i / 8 != 7) {set_bit(kingboard, i+9);}
        if (i % 8 != 0) {set_bit(kingboard, i-1);}
        if (i % 8 != 7 && i / 8 != 0) {set_bit(kingboard, i-7);}
        if (i / 8 != 0) {set_bit(kingboard, i-8);}
        if (i % 8 != 0 && i / 8 != 0) {set_bit(kingboard, i-9);}

        king_attacks[i] = kingboard;
    }
}

void init_sliding_piece_attacks(bool is_rook) {
    for (int square = 0; square < 64; square++) {
        u64 mask = is_rook ? rook_attack_masks_no_edges[square] : bishop_attack_masks_no_edges[square];
        
        int bit_cnt = bit_count(mask);
        
        int occupancy_variations = 1 << bit_cnt;
        
        for (int count = 0; count < occupancy_variations; count++) {
            if (is_rook) {
                u64 occupancy = set_occupancy(count, bit_cnt, mask);
                u64 magic_index = (occupancy * rook_magics[square]) >> (64 - rook_relevant_occupancy_amount[square]);
                rook_attacks[square][magic_index] = norm_rook_attacks(square, occupancy);
            } else {
                u64 occupancy = set_occupancy(count, bit_cnt, mask);
                u64 magic_index = (occupancy * bishop_magics[square]) >> (64 - bishop_relevant_occupancy_amount[square]);
                bishop_attacks[square][magic_index] = norm_bishop_attacks(square, occupancy);
            }
        }
    }
}

u64 generate_rook_moves(int square, u64 occupancy) {
	
	occupancy &= rook_attack_masks_no_edges[square];
	occupancy *= rook_magics[square];
	occupancy >>= 64 - rook_relevant_occupancy_amount[square];
	
	return rook_attacks[square][occupancy];
}

u64 generate_bishop_moves(int square, u64 occupancy) {
	
	occupancy &= bishop_attack_masks_no_edges[square];
	occupancy *= bishop_magics[square];
	occupancy >>= 64 - bishop_relevant_occupancy_amount[square];
	
	return bishop_attacks[square][occupancy];
}

// the pieces of `us` that are pinned to their king by enemy sliders: `pinned` gets their squares, pinners[square of the pinned piece] the square of the pinner
static inline __attribute__((always_inline)) void pins_for(const bool white, struct bitboards *BB, u64 *pinned, u8 *pinners) {
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const u64 enemy_pieces = BB->bits[white ? black_pieces : white_pieces];
    const u64 my_pieces = BB->bits[white ? white_pieces : black_pieces];
    const u64 my_king = BB->bits[white_king + own];
    const u8 king_index = lsb(my_king);

    // only a slider that stands on a line with the king can pin: the squares on the king's rook / bishop lines are the candidates
    // (the masks are symmetric: a rook is on the king's rank / file if the king is on the rook's). it pins if exactly one piece of ours
    // and no enemy piece is between it and the king
    u64 candidates = ((BB->bits[white_rooks + enemy] | BB->bits[white_queens + enemy]) & rook_attack_masks_with_edges[king_index]) |
                     ((BB->bits[white_bishops + enemy] | BB->bits[white_queens + enemy]) & bishop_attack_masks_with_edges[king_index]);
    // (a square is never on a rook line and on a bishop line of the king, so no piece is listed twice)
    for (; candidates; candidates &= candidates - 1) {
        u8 b = lsb(candidates);
        const u64 between = squares_between[b][king_index];
        const u64 blockers = between & my_pieces;
        if (blockers && (blockers & (blockers - 1)) == 0 && (between & enemy_pieces) == 0) {
            *pinned |= blockers;
            pinners[lsb(blockers)] = b;
        }
    }
}

void get_pins(struct bitboards *BB, u64 *pinned, u8 *pinners) {
    if (BB->color == WHITE) pins_for(true, BB, pinned, pinners);
    else pins_for(false, BB, pinned, pinners);
}

// all squares the enemy of the side to move attacks (the own king does not block its sliders: it could not just step back along the line)
static inline __attribute__((always_inline)) u64 enemy_attacks_for(const bool white, struct bitboards *BB) {
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    // our own king is taken off the board for the slider attacks. otherwise it would block the ray of a checking rook / bishop, and the
    // square behind the king (on the same line) would look safe, although the king leaves it when it steps back and the ray goes on
    const u64 occupied = BB->bits[all_pieces] ^ BB->bits[white_king + own];
    const u64 enemy_pawns = BB->bits[white_pawns + enemy];
    // the enemy pawns attack in the direction opposite to ours: black pawns (enemy of white) attack downwards
    u64 attacks = king_attacks[lsb(BB->bits[white_king + enemy])];
    attacks |= white ? (((enemy_pawns >> 7) & not_rightmost_file_mask) | ((enemy_pawns >> 9) & not_leftmost_file_mask))
                     : (((enemy_pawns << 7) & not_leftmost_file_mask) | ((enemy_pawns << 9) & not_rightmost_file_mask));

    for (u64 knights = BB->bits[white_knights + enemy]; knights; knights &= knights - 1) {
        attacks |= knight_attacks[lsb(knights)];
    }
    for (u64 bishops = BB->bits[white_bishops + enemy] | BB->bits[white_queens + enemy]; bishops; bishops &= bishops - 1) {
        attacks |= generate_bishop_moves(lsb(bishops), occupied);
    }
    for (u64 rooks = BB->bits[white_rooks + enemy] | BB->bits[white_queens + enemy]; rooks; rooks &= rooks - 1) {
        attacks |= generate_rook_moves(lsb(rooks), occupied);
    }
    return attacks;
}

u64 get_enemy_attack_mask(struct bitboards *BB) {
    return BB->color == WHITE ? enemy_attacks_for(true, BB) : enemy_attacks_for(false, BB);
}

#ifndef VERIFY_EP
#define VERIFY_EP 0 // debug build (-DVERIFY_EP=1): every en passant capture is also tested with ep_capture_is_legal_full and the program stops if the two differ
#endif

// the complete test: is the own king attacked after the capture? the capturing pawn moves from `from` to `to`, the enemy pawn on `captured` is removed.
// this needs no knowledge about how the move was generated, it is the reference for ep_capture_is_legal (VERIFY_EP, tools/ep_diff.py)
static inline bool ep_capture_is_legal_full(struct bitboards *BB, int from, int to, int captured) {
    bool white = captured < to;
    int own = white ? 0 : 6;
    int enemy = white ? 6 : 0;
    u64 king_bit = BB->bits[white_king + own];
    int king_index = lsb(king_bit);
    u64 occ = (BB->bits[all_pieces] ^ (1ULL << from) ^ (1ULL << captured)) | (1ULL << to);
    u64 enemy_pawns = BB->bits[white_pawns + enemy] & ~(1ULL << captured);
    u64 pawn_attackers = white ?
        ((((king_bit << 7) & not_leftmost_file_mask) | ((king_bit << 9) & not_rightmost_file_mask)) & enemy_pawns) :
        ((((king_bit >> 7) & not_rightmost_file_mask) | ((king_bit >> 9) & not_leftmost_file_mask)) & enemy_pawns);
    return (
        (king_attacks[king_index] & BB->bits[white_king + enemy]) |
        (knight_attacks[king_index] & BB->bits[white_knights + enemy]) |
        (generate_bishop_moves(king_index, occ) & (BB->bits[white_bishops + enemy] | BB->bits[white_queens + enemy])) |
        (generate_rook_moves(king_index, occ) & (BB->bits[white_rooks + enemy] | BB->bits[white_queens + enemy])) |
        pawn_attackers
    ) == 0;
}

// is an en passant capture legal? the capturing pawn moves from `from` to `to`, the enemy pawn on `captured` (next to `from`, in front of `to`) is removed.
// the callers (gen_moves) already guarantee most of what the complete test (ep_capture_is_legal_full) would check:
//  * the capturing pawn is either not pinned, or `to` is on its pin line, so removing it from `from` opens no line to the king by itself;
//  * in check by a slider `to` is on the line between the checker and the king, so the check is blocked, in check by a non-slider (knight or pawn)
//    the capture is only generated if the checking piece is the pawn that is captured, and there is no en passant at all in double check;
//  * knights, pawns and the enemy king can not attack the king after the capture if they did not attack it before (or are the captured pawn).
// that leaves the lines that the removal of the two pawns opens: the rank of the two pawns (the king and a rook or queen on it with only these two
// pawns between them) and the diagonals through the captured pawn (a bishop or queen behind it). the file through the captured pawn stays
// closed by the capturing pawn, which lands directly in front of it. both tests are only done if the king is on such a line, which is rare
static inline bool ep_capture_is_legal(struct bitboards *BB, int from, int to, int captured) {
    const bool white = captured < to;
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const u64 king_bit = BB->bits[white_king + own];
    const int king_index = lsb(king_bit);
    const u64 occ = (BB->bits[all_pieces] ^ (1ULL << from) ^ (1ULL << captured)) | (1ULL << to);
    bool legal = true;

    if (king_index / 8 == captured / 8 &&
        (generate_rook_moves(king_index, occ) & (BB->bits[white_rooks + enemy] | BB->bits[white_queens + enemy]))) {
        legal = false; // discovered check along the rank
    } else if ((bishop_attack_masks_with_edges[captured] & king_bit) &&
               (generate_bishop_moves(king_index, occ) & (BB->bits[white_bishops + enemy] | BB->bits[white_queens + enemy]))) {
        legal = false; // discovered check along a diagonal through the captured pawn
    }
#if VERIFY_EP
    if (legal != ep_capture_is_legal_full(BB, from, to, captured)) {
        fprintf(stderr, "EP MISMATCH: from %d to %d captured %d fast %d\n", from, to, captured, (int)legal);
        print_board(BB);
        abort();
    }
#endif
    return legal;
}

// one implementation for both colors (and for moves / captures only). `white` and `captures_only` are compile time
// constants in every call (the functions are always_inline and only called with literals), so the compiler removes
// the unused branches and there is no runtime test of the color. squares are numbered so that white pawns move to
// higher indices: `d` is +1 for white and -1 for black, a pawn that arrived on `b` came from b - 8 * d (push) or
// b - 7 * d / b - 9 * d (captures)
#define FORCE_INLINE static inline __attribute__((always_inline))

FORCE_INLINE u64 pawn_push(bool white, u64 pawns) { return white ? pawns << 8 : pawns >> 8; }
// captures towards the left / right as seen from the lower index (white: +7 / +9, black: -7 / -9)
FORCE_INLINE u64 pawn_capture1(bool white, u64 pawns) { return white ? (pawns << 7) & not_leftmost_file_mask : (pawns >> 7) & not_rightmost_file_mask; }
FORCE_INLINE u64 pawn_capture2(bool white, u64 pawns) { return white ? (pawns << 9) & not_rightmost_file_mask : (pawns >> 9) & not_leftmost_file_mask; }

FORCE_INLINE int add_promotions(move_t *out, int count, int from, int to) {
    out[count++] = MOVE(from, to, 4, 0, 0);
    out[count++] = MOVE(from, to, 3, 0, 0);
    out[count++] = MOVE(from, to, 2, 0, 0);
    out[count++] = MOVE(from, to, 1, 0, 0);
    return count;
}

// a pawn push or a pawn capture of the target square `to` (en passant needs the full legality test)
FORCE_INLINE int add_pawn_move(move_t *out, int count, int from, int to) {
    if (bit(to) & pawn_promotion_mask) return add_promotions(out, count, from, to);
    out[count++] = MOVE(from, to, 0, 0, 0);
    return count;
}

FORCE_INLINE int add_pawn_capture(struct bitboards *BB, move_t *out, int count, int from, int to, int ep_captured, u64 ep_bit) {
    // watch out for en passant discovered check
    if (bit(to) == ep_bit) {
        if (ep_capture_is_legal(BB, from, to, ep_captured)) out[count++] = MOVE(from, to, 0, 0, 0);
        return count;
    }
    return add_pawn_move(out, count, from, to);
}

FORCE_INLINE int add_targets(move_t *out, int count, int from, u64 targets) {
    for (; targets; targets &= targets - 1) out[count++] = MOVE(from, lsb(targets), 0, 0, 0);
    return count;
}

FORCE_INLINE int gen_moves(const bool white, const bool captures_only, struct bitboards *BB, move_t *out, u64 checkers, u64 pinned, u8 *pinners, u64 attacked) {
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const int d = white ? 1 : -1;
    const u64 my_pieces = BB->bits[white ? white_pieces : black_pieces];
    const u64 enemy_pieces = BB->bits[white ? black_pieces : white_pieces];
    const u64 my_pawns = BB->bits[white_pawns + own];
    const u64 occupied = BB->bits[all_pieces];
    const u64 ep_bit = BB->en_passant_square < 0 ? 0 : bit(BB->en_passant_square);
    const u64 ep_captured_bit = BB->en_passant_square < 0 ? 0 : bit(BB->en_passant_square - 8 * d); // the pawn that an en passant capture removes
    const u64 double_step_rank = white ? rank[2] : rank[5]; // where the pawns are after their first push
    const u64 king = BB->bits[white_king + own];
    const bool in_check = checkers != 0;

    possible_calls++;

    if (king == 0) {
        // without the king some of the other functions will break.
        // this should never happen, but if it does, it's better to catch it here.
        printf("\n");
        print_board(BB);
        printf("\nError: king not found\n");
        exit(1);
    }
    const u8 king_index = lsb(king);

    int count = 0;
    u64 step1 = 0, step2 = 0, capture1 = 0, capture2 = 0, knights = 0, bishops = 0, rooks = 0;
    u64 pinned_step1 = 0, pinned_step2 = 0, pinned_capture1 = 0, pinned_capture2 = 0, pinned_bishops = 0, pinned_rooks = 0;

    if (bit_count(checkers) >= 2) {
        // double check, only king moves are legal
    } else if (checkers & (BB->bits[white_knights + enemy] | BB->bits[white_pawns + enemy] | BB->bits[white_king + enemy])) {
        // single check by non-sliders, only king moves and captures are legal. an en passant capture resolves the check only if the
        // checker is the pawn that it captures (the pawn that just made its double step), see ep_capture_is_legal
        const u64 ep_evasion = (checkers & ep_captured_bit) ? ep_bit : 0;
        capture1 = pawn_capture1(white, my_pawns & ~pinned) & (checkers | ep_evasion);
        capture2 = pawn_capture2(white, my_pawns & ~pinned) & (checkers | ep_evasion);

        knights = BB->bits[white_knights + own] & ~pinned;
        bishops = (BB->bits[white_bishops + own] | BB->bits[white_queens + own]) & ~pinned;
        rooks = (BB->bits[white_rooks + own] | BB->bits[white_queens + own]) & ~pinned;
    } else if (checkers) {
        // single check by sliders, only king moves, captures and blocking moves are legal
        knights = BB->bits[white_knights + own] & ~pinned;
        bishops = (BB->bits[white_bishops + own] | BB->bits[white_queens + own]) & ~pinned;
        rooks = (BB->bits[white_rooks + own] | BB->bits[white_queens + own]) & ~pinned;

        checkers |= squares_between[lsb(checkers)][king_index];

        if (!captures_only) {
            step1 = pawn_push(white, my_pawns & ~pinned) & ~occupied;
            step2 = pawn_push(white, step1 & double_step_rank) & ~occupied & checkers;
            step1 &= checkers;
        }
        capture1 = pawn_capture1(white, my_pawns & ~pinned) & (enemy_pieces | ep_bit) & checkers;
        capture2 = pawn_capture2(white, my_pawns & ~pinned) & (enemy_pieces | ep_bit) & checkers;
    } else {
        // no checks, all moves are legal (except pinned pieces or castling through check)
        if (!captures_only) {
            step1 = pawn_push(white, my_pawns & ~pinned) & ~occupied;
            step2 = pawn_push(white, step1 & double_step_rank) & ~occupied;
        }
        capture1 = pawn_capture1(white, my_pawns & ~pinned) & (enemy_pieces | ep_bit);
        capture2 = pawn_capture2(white, my_pawns & ~pinned) & (enemy_pieces | ep_bit);

        knights = BB->bits[white_knights + own] & ~pinned;
        bishops = (BB->bits[white_bishops + own] | BB->bits[white_queens + own]) & ~pinned;
        rooks = (BB->bits[white_rooks + own] | BB->bits[white_queens + own]) & ~pinned;

        checkers = 0xFFFFFFFFFFFFFFFFULL;

        // some pinned pieces still have moves so they need to be generated here
        if (!captures_only) {
            pinned_step1 = pawn_push(white, my_pawns & pinned) & ~occupied;
            pinned_step2 = pawn_push(white, pinned_step1 & double_step_rank) & ~occupied;
        }
        pinned_capture1 = pawn_capture1(white, my_pawns & pinned) & (enemy_pieces | ep_bit);
        pinned_capture2 = pawn_capture2(white, my_pawns & pinned) & (enemy_pieces | ep_bit);

        pinned_bishops = (BB->bits[white_bishops + own] | BB->bits[white_queens + own]) & pinned;
        pinned_rooks = (BB->bits[white_rooks + own] | BB->bits[white_queens + own]) & pinned;
    }

    for (; step1; step1 &= step1 - 1) {
        u8 b = lsb(step1);
        count = add_pawn_move(out, count, b - 8 * d, b);
    }

    for (; step2; step2 &= step2 - 1) {
        u8 b = lsb(step2);
        out[count++] = MOVE(b - 16 * d, b, 0, 0, 1);
    }

    for (; capture1; capture1 &= capture1 - 1) {
        u8 b = lsb(capture1);
        count = add_pawn_capture(BB, out, count, b - 7 * d, b, b - 8 * d, ep_bit);
    }

    for (; capture2; capture2 &= capture2 - 1) {
        u8 b = lsb(capture2);
        count = add_pawn_capture(BB, out, count, b - 9 * d, b, b - 8 * d, ep_bit);
    }

    // a pinned pawn can only push along the pin; there is no promoting here since there is either a rook in front of the pawn or the king
    for (; pinned_step1; pinned_step1 &= pinned_step1 - 1) {
        u8 b = lsb(pinned_step1);
        if (bit(b) & squares_between[king_index][pinners[b - 8 * d]]) {
            out[count++] = MOVE(b - 8 * d, b, 0, 0, 0);
        }
    }

    for (; pinned_step2; pinned_step2 &= pinned_step2 - 1) {
        u8 b = lsb(pinned_step2);
        if (bit(b) & squares_between[king_index][pinners[b - 16 * d]]) {
            out[count++] = MOVE(b - 16 * d, b, 0, 0, 1);
        }
    }

    // for captures, promoting and en passant are still possible if the pawn captures in the direction of the bishop pin
    // (no discovered check to worry about: a pinned capture can only work if the pinning piece is a bishop)
    for (; pinned_capture1; pinned_capture1 &= pinned_capture1 - 1) {
        u8 b = lsb(pinned_capture1);
        int from = b - 7 * d;
        if (bit(b) & (squares_between[king_index][pinners[from]] | bit(pinners[from]))) {
            count = add_pawn_capture(BB, out, count, from, b, b - 8 * d, ep_bit);
        }
    }

    for (; pinned_capture2; pinned_capture2 &= pinned_capture2 - 1) {
        u8 b = lsb(pinned_capture2);
        int from = b - 9 * d;
        if (bit(b) & (squares_between[king_index][pinners[from]] | bit(pinners[from]))) {
            count = add_pawn_capture(BB, out, count, from, b, b - 8 * d, ep_bit);
        }
    }

    // the squares the pieces may go to: anything but own pieces, or only enemy pieces
    const u64 target = captures_only ? enemy_pieces : ~my_pieces;

    count = add_targets(out, count, king_index, king_attacks[king_index] & target & ~attacked);

    // castling (the king and the rook are on the squares that the castling rights say, the files are the same for both colors)
    if (!captures_only && !in_check) {
        const bool castle_king = white ? BB->white_castle_king_side : BB->black_castle_king_side;
        const bool castle_queen = white ? BB->white_castle_queen_side : BB->black_castle_queen_side;
        if (castle_king && (((occupied | attacked) & (bit(king_index - 1) | bit(king_index - 2))) == 0)) {
            out[count++] = MOVE(king_index, king_index - 2, 0, KINGSIDE, 0);
        }
        if (castle_queen && (((occupied | attacked) & (bit(king_index + 1) | bit(king_index + 2))) == 0) && ((occupied & bit(king_index + 3)) == 0)) {
            out[count++] = MOVE(king_index, king_index + 2, 0, QUEENSIDE, 0);
        }
    }

    for (; knights; knights &= knights - 1) {
        u8 from = lsb(knights);
        count = add_targets(out, count, from, knight_attacks[from] & target & checkers);
    }

    for (; bishops; bishops &= bishops - 1) {
        u8 from = lsb(bishops);
        count = add_targets(out, count, from, generate_bishop_moves(from, occupied) & target & checkers);
    }

    for (; rooks; rooks &= rooks - 1) {
        u8 from = lsb(rooks);
        count = add_targets(out, count, from, generate_rook_moves(from, occupied) & target & checkers);
    }

    // pinned sliders stay on the line between the king and the pinner (a pinned knight can never move)
    for (; pinned_bishops; pinned_bishops &= pinned_bishops - 1) {
        u8 from = lsb(pinned_bishops);
        count = add_targets(out, count, from, generate_bishop_moves(from, occupied) & target & (squares_between[king_index][pinners[from]] | bit(pinners[from])));
    }

    for (; pinned_rooks; pinned_rooks &= pinned_rooks - 1) {
        u8 from = lsb(pinned_rooks);
        count = add_targets(out, count, from, generate_rook_moves(from, occupied) & target & (squares_between[king_index][pinners[from]] | bit(pinners[from])));
    }

    return count;
}

int get_moves(struct bitboards *BB, move_t *MOVES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked) {
    return BB->color == WHITE ? gen_moves(true, false, BB, MOVES, checkers, pinned, pinners, attacked)
                              : gen_moves(false, false, BB, MOVES, checkers, pinned, pinners, attacked);
}

int get_captures(struct bitboards *BB, move_t *CAPTURES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked) {
    return BB->color == WHITE ? gen_moves(true, true, BB, CAPTURES, checkers, pinned, pinners, attacked)
                              : gen_moves(false, true, BB, CAPTURES, checkers, pinned, pinners, attacked);
}
