#ifndef MOVE_H
#define MOVE_H

#include <stdio.h>
#include "bitboards.h"
#include "counters.h"
#include "eval.h"
#include "print.h"

#define MAX_NUM_MOVES 218
#define KINGSIDE        1
#define QUEENSIDE       2

#define m_from(move)    ((move) &    0x3F)        // 0000 0000 0000 0011 1111
#define m_to(move)     (((move) &   0xFC0) >>  6) // 0000 0000 1111 1100 0000
#define m_promo(move)  (((move) &  0x7000) >> 12) // 0000 0111 0000 0000 0000
#define m_castle(move) (((move) & 0x18000) >> 15) // 0001 1000 0000 0000 0000
#define m_cr_ep(move)  (((move) & 0x20000) >> 17) // 0010 0000 0000 0000 0000

/*
a move is packed into the lowest 18 bits of an unsigned int (see the masks above):
    bits  0-5   from square
    bits  6-11  to square
    bits 12-14  piece a pawn promotes to (0 = no promotion)
    bits 15-16  castling: KINGSIDE or QUEENSIDE (0 = no castling)
    bit  17     the move is a double pawn push, so it may create an en passant square
*/
#define MOVE(from, to, promo, castle, cr_ep) \
    ( \
    (from)           | \
    ((to)     <<  6) | \
    ((promo)  << 12) | \
    ((castle) << 15) | \
    ((cr_ep)   << 17) \
    )

#define move_t unsigned int



struct undo {
    int  captured_piece : 4;
    bool white_castle_king_side;
    bool white_castle_queen_side;
    bool black_castle_king_side;
    bool black_castle_queen_side;
    int en_passant_square : 7;
};

void do_move(move_t move, struct bitboards *BB, struct undo *undo);

void undo_move(move_t move, struct bitboards *BB, struct undo *undo);

u64 do_move_light(move_t move, struct bitboards *BB);

move_t build_move(char *move, struct bitboards *BITBOARDS);

#endif
