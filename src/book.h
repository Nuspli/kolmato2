#ifndef BOOK_H
#define BOOK_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "bitboards.h"
#include "counters.h"
#include "move.h"

#define BOOK_SIZE 2000000

struct book_entry {
    u64 hash;
    move_t *moves;
    int *occourences;
    u8 num_moves;
};

struct book {
    struct book_entry *entries;
    int num_entries;
};

struct book *read_book(struct book *book_pages, char *file_name);

void write_book(struct book *book_pages, char *file_name);

void init_book(char *book_name);

extern struct book *book_entries;

// book move selection (book.c). the settings of the uci options BookMinGames / BookRandom
move_t get_book_move(u64 hash);
int book_position_weight(u64 hash);
extern int book_min_count;
extern bool book_random;

#endif