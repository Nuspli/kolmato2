#ifndef FEN_H
#define FEN_H

#include <stdio.h>
#include "counters.h"
#include "bitboards.h"
#include "move.h"

void fen_to_position(char* fen, struct bitboards *bitboards);

void update_fen_clocks(move_t move);

#endif