#ifndef CHECK_H
#define CHECK_H

#include "bitboards.h"
#include "move.h"
#include "move_generation.h"

bool can_capture_opponents_king(struct bitboards *BITBOARDS);

bool is_in_check(struct bitboards *BITBOARDS, u64 *checkers);

bool is_in_check_light(struct bitboards *BITBOARDS);

#endif