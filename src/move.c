#include "move.h"

// do_move / undo_move: one implementation for both colors. `white` is a compile time constant in every call (the functions
// are always_inline and called with literals), so the compiler removes the other color's code, there is no runtime
// color test inside. what differs between the colors:
//   sg / d    +1 for white, -1 for black: the sign of the piece codes in piece_list, and the direction pawns move in
//   own/enemy offset of the color's pieces in bits[] (0 for white, 6 for black); piece type p (0 = pawn .. 5 = king) of
//             a color is bits[offset + p], the same index is used for ZOBRIST_TABLE
//   scores    the position and material scores of black are kept negated, and the tables are indexed from white's side
//             (square ^ 56 for black), see pst_add / mat_add
//   castling  the corners and hash keys of the rights, see the constants at the top of do_move_for
#define FORCE_INLINE static inline __attribute__((always_inline))

// piece square tables (opening, endgame) of the piece types; white_position_tables[phase][type] are the same arrays
#define PST_PAWN   pawn_eval_white_opening_position, pawn_eval_white_endgame_position
#define PST_ROOK   rook_eval_white_opening_position, rook_eval_white_endgame_position
#define PST_KING   king_eval_white_opening_position, king_eval_white_endgame_position
#define PST_PIECE(type) white_position_tables[OPENING][type], white_position_tables[ENDGAME][type]

// adds (sign = 1) or removes (sign = -1) the piece square scores of a piece of color `for_white` on `sq`
FORCE_INLINE void pst_add(struct bitboards *BB, const bool for_white, const int *opening, const int *endgame, int sq, int sign) {
    if (for_white) {
        BB->white_position_opening += sign * opening[sq];
        BB->white_position_endgame += sign * endgame[sq];
    } else {
        BB->black_position_opening -= sign * opening[sq ^ 56];
        BB->black_position_endgame -= sign * endgame[sq ^ 56];
    }
}

// the same for the material of a color
FORCE_INLINE void mat_add(struct bitboards *BB, const bool for_white, int value, int sign) {
    if (for_white) BB->white_material += sign * value;
    else BB->black_material -= sign * value;
}

// the rook squares of a castling move: s = where it starts, o = where it ends
FORCE_INLINE void castle_rook_squares(const bool white, move_t move, int *s, int *o) {
    const bool king_side = m_castle(move) == KINGSIDE;
    const int base = white ? 0 : 56;
    *s = base + 7 - king_side * 7;
    *o = base + 4 - king_side * 2;
}

FORCE_INLINE void do_move_for(const bool white, move_t move, struct bitboards *BB, struct undo *undo, u8 f, u8 t) {
    const int sg = white ? 1 : -1;
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const int own_pieces = white ? white_pieces : black_pieces;
    const int enemy_pieces = white ? black_pieces : white_pieces;
    const u64 from_bit = 1ULL << f;
    const u64 to_bit = 1ULL << t;

    // the castling rights that can change: the corner a rook starts from (or is captured on) and the hash key of the right
    bool *own_queen_side = white ? &BB->white_castle_queen_side : &BB->black_castle_queen_side;
    bool *own_king_side = white ? &BB->white_castle_king_side : &BB->black_castle_king_side;
    bool *enemy_queen_side = white ? &BB->black_castle_queen_side : &BB->white_castle_queen_side;
    bool *enemy_king_side = white ? &BB->black_castle_king_side : &BB->white_castle_king_side;
    const int own_queen_corner = white ? 7 : 63, own_king_corner = white ? 0 : 56;
    const int enemy_queen_corner = white ? 63 : 7, enemy_king_corner = white ? 56 : 0;
    const int own_queen_key = white ? 3 : 1, own_king_key = white ? 2 : 0;
    const int enemy_queen_key = white ? 1 : 3, enemy_king_key = white ? 0 : 2;

    if (t == BB->en_passant_square && BB->piece_list[t] == sg) {
        // en passant: remove the captured pawn
        const int cs = t - 8 * sg;
        BB->bits[white_pawns + enemy] ^= (1ULL << cs);
        BB->bits[enemy_pieces] ^= (1ULL << cs);
        BB->bits[all_pieces] ^= (1ULL << cs);

        BB->hash ^= ZOBRIST_TABLE[cs][enemy];
        BB->piece_list[cs] = 0;
        pst_add(BB, !white, PST_PAWN, cs, -1);
        mat_add(BB, !white, PAWNVALUE, -1);

    } else if (white ? undo->captured_piece < 0 : undo->captured_piece > 0) {
        // capture move
        // here we dont need to remove the bit from the occupied squares, because the capturing piece is already there
        const int cp = abs(undo->captured_piece) - 1;
        BB->bits[enemy_pieces] ^= to_bit;
        BB->bits[enemy + cp] ^= to_bit;
        BB->hash ^= ZOBRIST_TABLE[t][enemy + cp];
        pst_add(BB, !white, PST_PIECE(cp), t, -1);
        mat_add(BB, !white, material_values[cp], -1);

        // capturing a rook (or anything on its corner) invalidates the castling right of that side
        if (*enemy_queen_side && t == enemy_queen_corner) {
            *enemy_queen_side = false;
            BB->hash ^= castling_rights[enemy_queen_key];
        } else if (*enemy_king_side && t == enemy_king_corner) {
            *enemy_king_side = false;
            BB->hash ^= castling_rights[enemy_king_key];
        }
    }

    BB->bits[own_pieces] ^= (from_bit | to_bit);

    if (BB->piece_list[t] == sg) {
        // pawn move
        BB->bits[white_pawns + own] ^= from_bit;
        BB->hash ^= ZOBRIST_TABLE[f][own];
        pst_add(BB, white, PST_PAWN, f, -1);

        if (white ? t >= 56 : t <= 7) {
            // pawn promotion
            const int promo = m_promo(move);
            BB->bits[own + promo] ^= to_bit;
            BB->hash ^= ZOBRIST_TABLE[t][own + promo];
            BB->piece_list[t] = sg * (promo + 1);
            pst_add(BB, white, PST_PIECE(promo), t, 1);
            mat_add(BB, white, material_values[promo] - PAWNVALUE, 1);
        } else {
            BB->bits[white_pawns + own] ^= to_bit;
            BB->hash ^= ZOBRIST_TABLE[t][own];
            pst_add(BB, white, PST_PAWN, t, 1);
        }

    } else if (BB->piece_list[t] == 4 * sg) {
        // rook move
        BB->bits[white_rooks + own] ^= (from_bit | to_bit);
        BB->hash ^= ZOBRIST_TABLE[f][white_rooks + own] ^ ZOBRIST_TABLE[t][white_rooks + own];
        pst_add(BB, white, PST_ROOK, f, -1);
        pst_add(BB, white, PST_ROOK, t, 1);

        // a rook that leaves its corner takes the castling right of that side with it
        if (*own_queen_side && f == own_queen_corner) {
            *own_queen_side = false;
            BB->hash ^= castling_rights[own_queen_key];
        } else if (*own_king_side && f == own_king_corner) {
            *own_king_side = false;
            BB->hash ^= castling_rights[own_king_key];
        }

    } else if (BB->piece_list[t] == 6 * sg) {
        // king move
        BB->bits[white_king + own] ^= (from_bit | to_bit);
        BB->hash ^= ZOBRIST_TABLE[f][white_king + own] ^ ZOBRIST_TABLE[t][white_king + own];
        pst_add(BB, white, PST_KING, f, -1);
        pst_add(BB, white, PST_KING, t, 1);

        if (m_castle(move)) {
            // put the rook on its new square
            int s, o;
            castle_rook_squares(white, move, &s, &o);
            BB->bits[all_pieces] ^= (1ULL << s) | (1ULL << o);
            BB->bits[own_pieces] ^= (1ULL << s) | (1ULL << o);
            BB->bits[white_rooks + own] ^= (1ULL << s) | (1ULL << o);

            BB->hash ^= ZOBRIST_TABLE[o][white_rooks + own] ^ ZOBRIST_TABLE[s][white_rooks + own];
            BB->piece_list[s] = 0;
            BB->piece_list[o] = 4 * sg;
            pst_add(BB, white, PST_ROOK, s, -1);
            pst_add(BB, white, PST_ROOK, o, 1);
        }
        if (*own_queen_side) {
            *own_queen_side = false;
            BB->hash ^= castling_rights[own_queen_key];
        }
        if (*own_king_side) {
            *own_king_side = false;
            BB->hash ^= castling_rights[own_king_key];
        }

    } else {
        // any other piece
        const int pc = abs(BB->piece_list[t]) - 1;
        BB->bits[own + pc] ^= (from_bit | to_bit);
        BB->hash ^= ZOBRIST_TABLE[f][own + pc] ^ ZOBRIST_TABLE[t][own + pc];
        pst_add(BB, white, PST_PIECE(pc), f, -1);
        pst_add(BB, white, PST_PIECE(pc), t, 1);
    }

    if (BB->en_passant_square >= 0) {
        // if there was an en passant square, remove it from the hash
        BB->hash ^= ZOBRIST_TABLE[BB->en_passant_square][12];
        BB->en_passant_square = -1;
    }

    if (m_cr_ep(move) && ((BB->bits[white_pawns + enemy] & (1ULL << (t + 1))) || (BB->bits[white_pawns + enemy] & (1ULL << (t - 1))))) {
        // if the move creates an en passant square and it it captureable, add it to the hash and set the bit
        BB->en_passant_square = t - 8 * sg;
        BB->hash ^= ZOBRIST_TABLE[t - 8 * sg][12];
    }
}

void do_move(move_t move, struct bitboards *BB, struct undo *undo) {

    move_calls++;
    ply++;

    u8 f = m_from(move);
    u8 t = m_to(move);

    undo->captured_piece = BB->piece_list[t];
    undo->en_passant_square = BB->en_passant_square;

    u64 to_bit = 1ULL << t;
    u64 from_bit = 1ULL << f;
    // flip the from bit and only flip the to bit if its not a capture
    BB->bits[all_pieces] ^= ((from_bit) | ((to_bit) & ~BB->bits[all_pieces]));
    // update the hash
    BB->hash ^= white_to_move;
    // update the piece list
    BB->piece_list[t] = BB->piece_list[f];
    BB->piece_list[f] = 0;

    undo->white_castle_king_side = BB->white_castle_king_side;
    undo->white_castle_queen_side = BB->white_castle_queen_side;
    undo->black_castle_king_side = BB->black_castle_king_side;
    undo->black_castle_queen_side = BB->black_castle_queen_side;

    // now for the specific piece bitboards
    if (BB->color == WHITE) do_move_for(true, move, BB, undo, f, t);
    else do_move_for(false, move, BB, undo, f, t);

    BB->color = -BB->color;
    search_history[ply] = BB->hash;
}

// `white` = the color that made the move that is taken back (BB->color is already flipped back to it)
FORCE_INLINE void undo_move_for(const bool white, move_t move, struct bitboards *BB, struct undo *undo, u8 f, u8 t) {
    const int sg = white ? 1 : -1;
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const int own_pieces = white ? white_pieces : black_pieces;
    const int enemy_pieces = white ? black_pieces : white_pieces;
    const u64 from_bit = 1ULL << f;
    const u64 to_bit = 1ULL << t;

    if (m_promo(move)) {
        BB->piece_list[f] = sg;
    }

    if (t == undo->en_passant_square && BB->piece_list[f] == sg) {
        // put the en passant captured pawn back
        const int cs = t - 8 * sg;
        BB->bits[white_pawns + enemy] ^= (1ULL << cs);
        BB->bits[enemy_pieces] ^= (1ULL << cs);
        BB->bits[all_pieces] ^= (1ULL << cs);

        BB->hash ^= ZOBRIST_TABLE[cs][enemy];
        BB->piece_list[cs] = -sg;
        mat_add(BB, !white, PAWNVALUE, 1);
        pst_add(BB, !white, PST_PAWN, cs, 1);

    } else if (white ? undo->captured_piece < 0 : undo->captured_piece > 0) {
        // put the captured piece back
        const int cp = abs(undo->captured_piece) - 1;
        BB->bits[enemy_pieces] ^= to_bit;
        BB->bits[all_pieces] ^= to_bit;

        BB->bits[enemy + cp] ^= to_bit;
        BB->hash ^= ZOBRIST_TABLE[t][enemy + cp];
        mat_add(BB, !white, material_values[cp], 1);
        pst_add(BB, !white, PST_PIECE(cp), t, 1);
    }

    BB->bits[own_pieces] ^= (from_bit | to_bit);

    if (BB->piece_list[f] == sg) {
        // pawn (also: the pawn that promoted)
        BB->bits[white_pawns + own] ^= from_bit;
        BB->hash ^= ZOBRIST_TABLE[f][own];
        pst_add(BB, white, PST_PAWN, f, 1);

        if (white ? t >= 56 : t <= 7) {
            const int promo = m_promo(move);
            BB->bits[own + promo] ^= to_bit;
            BB->hash ^= ZOBRIST_TABLE[t][own + promo];
            mat_add(BB, white, material_values[promo] - PAWNVALUE, -1);
            pst_add(BB, white, PST_PIECE(promo), t, -1);
        } else {
            BB->bits[white_pawns + own] ^= to_bit;
            BB->hash ^= ZOBRIST_TABLE[t][own];
            pst_add(BB, white, PST_PAWN, t, -1);
        }

    } else if (BB->piece_list[f] == 6 * sg) {
        // king (the castling rights are restored at the end)
        BB->bits[white_king + own] ^= (from_bit | to_bit);
        BB->hash ^= ZOBRIST_TABLE[f][white_king + own] ^ ZOBRIST_TABLE[t][white_king + own];
        pst_add(BB, white, PST_KING, t, -1);
        pst_add(BB, white, PST_KING, f, 1);

        if (m_castle(move)) {
            int s, o;
            castle_rook_squares(white, move, &s, &o);
            BB->bits[all_pieces] ^= (1ULL << s) | (1ULL << o);
            BB->bits[own_pieces] ^= (1ULL << s) | (1ULL << o);
            BB->bits[white_rooks + own] ^= (1ULL << s) | (1ULL << o);

            BB->hash ^= ZOBRIST_TABLE[o][white_rooks + own] ^ ZOBRIST_TABLE[s][white_rooks + own];
            BB->piece_list[s] = 4 * sg;
            BB->piece_list[o] = 0;
            pst_add(BB, white, PST_ROOK, o, -1);
            pst_add(BB, white, PST_ROOK, s, 1);
        }
    } else {
        // any other piece (the rooks too: the tables are the same arrays)
        const int pc = abs(BB->piece_list[f]) - 1;
        BB->bits[own + pc] ^= (from_bit | to_bit);
        BB->hash ^= ZOBRIST_TABLE[f][own + pc] ^ ZOBRIST_TABLE[t][own + pc];
        pst_add(BB, white, PST_PIECE(pc), t, -1);
        pst_add(BB, white, PST_PIECE(pc), f, 1);
    }

    if (BB->en_passant_square >= 0) {
        // if there is an en passant square, remove it
        BB->hash ^= ZOBRIST_TABLE[BB->en_passant_square][12];
        BB->en_passant_square = -1;
    }

    if (undo->en_passant_square >= 0) {
        // if there was an en passant square, add it back
        BB->hash ^= ZOBRIST_TABLE[undo->en_passant_square][12];
        BB->en_passant_square = undo->en_passant_square;
    }
}

void undo_move(move_t move, struct bitboards *BB, struct undo *undo) {

    ply--;

    u8 t = m_to(move);
    u8 f = m_from(move);

    u64 to_bit = 1ULL << t;
    u64 from_bit = 1ULL << f;

    BB->bits[all_pieces] ^= ((from_bit) | (to_bit));
    BB->hash ^= white_to_move;
    BB->color = -BB->color;

    BB->piece_list[f] = BB->piece_list[t];
    BB->piece_list[t] = undo->captured_piece;

    if (BB->color == WHITE) undo_move_for(true, move, BB, undo, f, t);
    else undo_move_for(false, move, BB, undo, f, t);

    if (undo->white_castle_queen_side && !BB->white_castle_queen_side) {
        BB->white_castle_queen_side = true;
        BB->hash ^= castling_rights[3];
    }
    if (undo->white_castle_king_side && !BB->white_castle_king_side) {
        BB->white_castle_king_side = true;
        BB->hash ^= castling_rights[2];
    }
    if (undo->black_castle_queen_side && !BB->black_castle_queen_side) {
        BB->black_castle_queen_side = true;
        BB->hash ^= castling_rights[1];
    }
    if (undo->black_castle_king_side && !BB->black_castle_king_side) {
        BB->black_castle_king_side = true;
        BB->hash ^= castling_rights[0];
    }
}

move_t build_move(char *move, struct bitboards *BB) {
    // make a useable move struct from a string in the form of e2e4
    move_t m;
    u8 from = 7 - (move[0] - 'a') + (move[1] - '1') * 8;
    u8 to =  7 - (move[2] - 'a') + (move[3] - '1') * 8;

    u8 castle = 0;
    u8 creates_en_passant = 0;
    char promoting_char = move[4]; // the move can be given with the piece, as in e7e8q
    u8 promotes_to = 0;

    if (((BB->bits[white_pawns] & bit(from)) != 0) || ((BB->bits[black_pawns] & bit(from)) != 0)) {
        if (abs(from - to) == 16) {
            creates_en_passant = 1;
        } else if (to > 55 || to < 8) {
            if (promoting_char == 0) {
                // the piece was not part of the move, ask for it
                char answer[8] = {0};
                printf("promote to: ");
                if (scanf("%7s", answer) != 1) answer[0] = 'q';
                promoting_char = answer[0];
            }
            if (promoting_char == 'q') {
                promotes_to = 4;
            } else if (promoting_char == 'r') {
                promotes_to = 3;
            } else if (promoting_char == 'b') {
                promotes_to = 2;
            } else if (promoting_char == 'n') {
                promotes_to = 1;
            }
        }
    } else if (((BB->bits[white_king] & bit(from)) != 0) || ((BB->bits[black_king] & bit(from)) != 0)) {
        if (abs(from - to) == 2) {
            if (from > to) {
                castle = 1;
            } else {
                castle = 2;
            }
        }
    }

    m = MOVE(from, to, promotes_to, castle, creates_en_passant);

    return m;
}

u64 do_move_light(move_t move, struct bitboards *BB) {
    // only the hash is important here
    u64 hash = BB->hash;

    u8 t = m_to(move);
    u8 f = m_from(move);

    hash ^= white_to_move;

    if (BB->color == WHITE) {
        if (t == BB->en_passant_square && BB->piece_list[f] == 1) {
            hash ^= ZOBRIST_TABLE[(t - 8)][6];

        } else if (BB->piece_list[t] < 0) {
            hash ^= ZOBRIST_TABLE[t][-(BB->piece_list[t]) + 5];
        }

        if (BB->piece_list[f] == 1) {
            hash ^= ZOBRIST_TABLE[f][0];
            if (t >= 56) {
                hash ^= ZOBRIST_TABLE[t][m_promo(move)];
            } else {
                hash ^= ZOBRIST_TABLE[t][0];
            }
        } else if (BB->piece_list[f] == 4) {
            hash ^= ZOBRIST_TABLE[f][3] ^ ZOBRIST_TABLE[t][3];
            if (BB->white_castle_queen_side && f == 7) {
                hash ^= castling_rights[3];
            } else if (BB->white_castle_king_side && f == 0) {
                hash ^= castling_rights[2];
            }
        } else if (BB->piece_list[f] == 6) {
            hash ^= ZOBRIST_TABLE[f][5] ^ ZOBRIST_TABLE[t][5];
            if (m_castle(move)) {
                int s;
                int o;
                if (m_castle(move) == KINGSIDE) {s = 0; o = 2;} else {s = 7; o = 4;}
                    hash ^= ZOBRIST_TABLE[o][3] ^ ZOBRIST_TABLE[s][3];
                }
            if (BB->white_castle_queen_side) {
                hash ^= castling_rights[3];
            }
            if (BB->white_castle_king_side) {
                hash ^= castling_rights[2];
            }
        } else {
            hash ^= ZOBRIST_TABLE[f][BB->piece_list[f] - 1] ^ ZOBRIST_TABLE[t][BB->piece_list[f] - 1];
        }

        if (BB->en_passant_square >= 0) {
            hash ^= ZOBRIST_TABLE[BB->en_passant_square][12];
        }

        if (m_cr_ep(move) && ((BB->bits[black_pawns] & (1ULL << (t + 1))) || (BB->bits[black_pawns] & (1ULL << (t - 1))))) {
            hash ^= ZOBRIST_TABLE[t - 8][12];
        }

    } else {
        // Black move
        if (t == BB->en_passant_square && BB->piece_list[f] == -1) {
            hash ^= ZOBRIST_TABLE[t + 8][0];

        } else if (BB->piece_list[t] > 0) {
            hash ^= ZOBRIST_TABLE[t][BB->piece_list[t] - 1];
        }

        if (BB->piece_list[f] == -1) {

            hash ^= ZOBRIST_TABLE[f][6];
            if (t <= 7) {
                hash ^= ZOBRIST_TABLE[t][m_promo(move) + 6];
            } else {
                hash ^= ZOBRIST_TABLE[t][6];
            }
        } else if (BB->piece_list[f] == -4) {
            hash ^= ZOBRIST_TABLE[t][9] ^ ZOBRIST_TABLE[f][9];
            if (BB->black_castle_queen_side && f == 63) {
                hash ^= castling_rights[1];
            } else if (BB->black_castle_king_side && f == 56) {
                hash ^= castling_rights[0];
            }
        } else if (BB->piece_list[f] == -6) {

            hash ^= ZOBRIST_TABLE[t][11] ^ ZOBRIST_TABLE[f][11];
            if (m_castle(move)) {
                int s;
                int o;
                if (m_castle(move) == KINGSIDE) {s = 56; o = 58;} else {s = 63; o = 60;}
                    hash ^= ZOBRIST_TABLE[o][9] ^ ZOBRIST_TABLE[s][9];
                }
            if (BB->black_castle_queen_side) {
                hash ^= castling_rights[1];
            }
            if (BB->black_castle_king_side) {
                hash ^= castling_rights[0];
            }
        } else {
            hash ^= ZOBRIST_TABLE[t][-(BB->piece_list[f]) + 5] ^ ZOBRIST_TABLE[f][-(BB->piece_list[f]) + 5];
        }

        if (BB->en_passant_square >= 0) {
            hash ^= ZOBRIST_TABLE[BB->en_passant_square][12]; BB->en_passant_square = -1;
        }

        if (m_cr_ep(move) && ((BB->bits[white_pawns] & (1ULL << (t + 1))) || (BB->bits[white_pawns] & (1ULL << (t - 1))))) {
            BB->en_passant_square = (t + 8); hash ^= ZOBRIST_TABLE[t + 8][12];
        }
    }
    
    return hash;
}
