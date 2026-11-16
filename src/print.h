#ifndef PRINT_H
#define PRINT_H

#include <stdio.h>
#include "bitboards.h"
#include "counters.h"

void print_logo();

void print_help();

extern char notation[64][3];
extern const char *promo_suffix[8]; // "" n b r q for m_promo()

void print_binary(u64 x);

void print_board(struct bitboards *BITBOARDS);

#endif