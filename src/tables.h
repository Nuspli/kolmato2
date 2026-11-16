#ifndef TABLES_H
#define TABLES_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "move.h"
#include "bitboards.h"

// table sizes in megabytes (each entry is 16 bytes, memory is only touched when used)
#define TT_MAX_SIZE_MB   256   // default size of the main search table for long searches (see choose_hash_size_mb in main.c)
#define TT_MIN_SIZE_MB    16   // smallest default size

#define BUCKET_SIZE 4 // 4 * 16 bytes = one 64 byte cache line

#define NOENTRY 0
#define EXACT 1
#define LOWERBOUND 2
#define UPPERBOUND 3
#define INF 100000

// scores with an absolute value of at least this are mate scores ("mate in n plies")
#define MATE_BOUND (INF - 1000)

// no static evaluation stored (the side to move was in check, or the entry comes from a search that did not evaluate the position).
// real evaluations are far below this, search.c has a matching EVAL_NONE
#define TT_NO_EVAL 32001

// one table entry. the value, depth, flag and best move are always written together,
// they all belong to the position identified by key. the static evaluation belongs to the position only (it does not depend on
// the search), so a later store without one keeps the old one
struct hash_entry {
    uint32_t key;       // upper 32 bits of the position hash (the lower bits select the bucket)
    int32_t value;      // score, mate scores are stored relative to the node (see table_store)
    move_t best_move;
    int8_t depth;
    u8 gen_flag;         // bits 0-1: bound flag, bits 2-7: age of the search that wrote it
    int16_t static_eval; // static evaluation of the position from the side to move's view, TT_NO_EVAL if unknown
};

_Static_assert(sizeof(struct hash_entry) == 16, "hash_entry must be 16 bytes");

// how an entry is stored in the table. several search threads read and write the table without any locks, so an entry is
// two 64 bit words that are each read and written in one piece, and the key is stored xor-ed with a hash of the first word:
//   a = value (low 32 bits) | best move (high 32 bits)
//   b = (key ^ fold(a)) (low 32 bits) | depth (bits 32-39) | gen_flag (bits 40-47) | static evaluation (bits 48-63)
// if a reader sees the first word of one write and the second word of another, the key does not come out right and the
// entry is treated as a miss. (the same trick as the xor scheme in other engines)
struct tt_slot {
    uint64_t a;
    uint64_t b;
};

_Static_assert(sizeof(struct tt_slot) == 16, "tt_slot must be 16 bytes");

#define entry_flag(e) ((e)->gen_flag & 3)

struct table {
    struct tt_slot *entries; // 64 byte aligned (2 MB aligned when huge pages are used)
    void *raw_memory; // what was actually allocated (for free)
    size_t raw_size;
    bool is_mapped; // allocated with mmap instead of calloc
    uint64_t bucket_mask;
    u8 age;
};

extern struct table *trans_table;
extern struct table *quiet_table;

// looks up a position. returns true on a hit and copies the entry to *out.
// out->value is already converted to be relative to the given ply.
bool table_probe(struct table *table, u64 hash, int ply, struct hash_entry *out);

// stores a result. value, depth, flag and move are written together or not at all.
// if move is 0 an existing move for the same position is kept. static_eval is the static evaluation of the position
// (TT_NO_EVAL if there is none, an existing one for the same position is kept then); values outside +-32000 are not stored
void table_store(struct table *table, u64 hash, int depth, int value, u8 flag, move_t move, int ply, int static_eval);

// asks the cpu to load the bucket of a position into the cache. call this as soon as the hash of a position is
// known (right after making a move) so that the memory access overlaps with other work
static inline void table_prefetch(struct table *table, u64 hash) {
    __builtin_prefetch(table->entries + (hash & table->bucket_mask) * BUCKET_SIZE);
}

// call once at the start of every search so that entries from older searches can be replaced
void table_new_search(struct table *table);

// main_mb is the size of the main search table in megabytes (a power of 2), the quiescence search table gets a quarter of it
void init_tables(int main_mb);

// empties both tables (every page is written, so all of the memory is really allocated)
void clear_tables();

void free_tables();

#endif
