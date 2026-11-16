#ifndef BITBOARDS_H
#define BITBOARDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "counters.h"
#include "bit_operations.h"
#include "random.h"
#include "eval.h"

#define u64 uint64_t
#define u8 uint8_t

// GOOD_ZOBRIST (default 1; SPRT together with TT_EVAL_REFINE: +4.08 +- 3.28, alone -0.86 +- 5.63, i.e. no loss): the position hash keys (ZOBRIST_TABLE) were made with a 32 bit xorshift that is linear and keeps 16 of its bits, which makes
// the keys dependent on each other: completely different positions get the same 64 bit hash (several times in one depth 15 search, see
// VERIFY_TT_EVAL in search.c). with GOOD_ZOBRIST the keys come from splitmix64. the opening book (bookMax.dat) was made with the old keys, so the
// old table is kept for it: book_hash() computes the old hash of a position from scratch (only used when the book is probed)
#ifndef GOOD_ZOBRIST
#define GOOD_ZOBRIST 1
#endif

#define white_pawns 0
#define white_knights 1
#define white_bishops 2
#define white_rooks 3
#define white_queens 4
#define white_king 5
#define black_pawns 6
#define black_knights 7
#define black_bishops 8
#define black_rooks 9
#define black_queens 10
#define black_king 11
#define white_pieces 12
#define black_pieces 13
#define all_pieces 14

#define WHITE 1
#define BLACK -1

struct bitboards {

    u64 bits[15];

    u8 pinner_for_square[64];
    
    bool white_castle_king_side;
    bool white_castle_queen_side;
    
    bool black_castle_king_side;
    bool black_castle_queen_side;
    
    int en_passant_square : 7;
    
    u64 hash;
    signed int color : 2;
    int piece_list[64];

    int white_position_opening;
    int black_position_opening;
    int white_position_endgame;
    int black_position_endgame;

    int white_material;
    int black_material;


};

extern struct bitboards *bitboards;

extern u64 ZOBRIST_TABLE[64][13]; 
extern u64 white_to_move;
extern u64 castling_rights[4];

void allocate_boards();

void free_boards();

void init_zobrist();

// the hash that the opening book is indexed with (the position hash itself unless GOOD_ZOBRIST)
struct bitboards;
u64 book_hash(const struct bitboards *BITBOARDS);

void init_boards(struct bitboards *BITBOARDS, bool is_white_to_move, char* castle, char* en_pas, int fifty_move, int move_num);
void init_boards_light(struct bitboards *BITBOARDS, bool is_white_to_move, char* castle, char* en_pas);

void reset_boards(struct bitboards *BITBOARDS);
void reset_boards_light(struct bitboards *BITBOARDS);

#endif