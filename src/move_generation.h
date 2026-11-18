#ifndef MOVE_GENERATION_H
#define MOVE_GENERATION_H

#include <stdbool.h>
#include "print.h"
#include "bitboards.h"
#include "magic.h"
#include "move.h"


#define same_rank(a, b) ((a / 8) == (b / 8))
#define same_file(a, b) ((a % 8) == (b % 8))

extern u64 knight_attacks[64];
extern u64 king_attacks[64];
extern u64 bishop_attacks[64][512];
extern u64 rook_attacks[64][4096];

extern u64 not_rightmost_file_mask;
extern u64 not_leftmost_file_mask;
extern u64 pawn_promotion_mask;

extern u64 rank[8];
extern u64 file[8];
extern u64 adj_files[8];
extern u64 squares_between[64][64];

void init_knight_attacks();

void init_king_attacks();

void init_sliding_piece_attacks(bool is_rook);

void init_squares_between();

void get_pins(struct bitboards *BB, u64 *pinned, u8 *pinners);

u64 generate_rook_moves(int square, u64 occupancy);

u64 generate_bishop_moves(int square, u64 occupancy);

u64 get_enemy_attack_mask(struct bitboards *BB);
                        
int get_moves(struct bitboards *BB, move_t *MOVES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked);

int get_captures(struct bitboards *BB, move_t *CAPTURES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked);

// the two halves of get_moves for the staged move picker: every capture, en passant and promotion / every other move. disjoint, together all legal moves
int get_tactical(struct bitboards *BB, move_t *MOVES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked);

int get_quiets(struct bitboards *BB, move_t *MOVES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked);

// the legal moves of the piece on one square (to test if a single move, e.g. from the table, is legal here)
int get_moves_from(struct bitboards *BB, move_t *MOVES, u64 checkers, u64 pinned, u8 *pinners, u64 attacked, int from);



#endif
