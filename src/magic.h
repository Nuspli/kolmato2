#ifndef MAGIC_H
#define MAGIC_H

#include <stdio.h>
#include "bitboards.h"
#include "random.h"

extern int rook_relevant_occupancy_amount[64];

extern int bishop_relevant_occupancy_amount[64];

extern u64 bishop_attack_masks_no_edges[64];

extern u64 rook_attack_masks_no_edges[64];

extern u64 bishop_attack_masks_with_edges[64];

extern u64 rook_attack_masks_with_edges[64];

extern u64 rook_magics[64];

extern u64 bishop_magics[64];

u64 norm_bishop_attacks(int square, u64 occupied);

u64 norm_rook_attacks(int square, u64 occupied);

u64 set_occupancy(int index, int mask_bits, u64 attack_mask);

void generate_new_magics();

#endif