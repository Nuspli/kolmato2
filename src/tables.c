#include "tables.h"

#if defined(__linux__) && defined(USE_HUGEPAGES)
#include <sys/mman.h>
#endif

struct table *trans_table;
struct table *quiet_table;

// mate scores are relative to the root during the search (-INF + ply of the mated node).
// in the table they are stored relative to the node itself, so that the same position
// found at a different ply still yields the correct distance to mate.
static inline int score_to_table(int score, int ply) {
    if (score >= MATE_BOUND)  return score + ply;
    if (score <= -MATE_BOUND) return score - ply;
    return score;
}

static inline int score_from_table(int score, int ply) {
    if (score >= MATE_BOUND)  return score - ply;
    if (score <= -MATE_BOUND) return score + ply;
    return score;
}

static inline struct tt_slot *bucket_for(struct table *table, u64 hash) {
    return table->entries + (hash & table->bucket_mask) * BUCKET_SIZE;
}

static inline uint32_t fold(uint64_t a) {
    return (uint32_t)((a * 0x9E3779B97F4A7C15ULL) >> 32);
}

// plain loads and stores of one word each (the compiler may not split, merge or repeat them)
static inline uint64_t load_word(const uint64_t *p) {
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}

static inline void store_word(uint64_t *p, uint64_t v) {
    __atomic_store_n(p, v, __ATOMIC_RELAXED);
}

static inline uint32_t slot_key(uint64_t a, uint64_t b) {
    return (uint32_t)b ^ fold(a);
}

static inline u8 slot_gen_flag(uint64_t b) {
    return (u8)(b >> 40);
}

static inline int slot_depth(uint64_t b) {
    return (int8_t)(b >> 32);
}

static inline struct hash_entry decode_slot(uint64_t a, uint64_t b) {
    struct hash_entry e;
    e.key = slot_key(a, b);
    e.value = (int32_t)(uint32_t)a;
    e.best_move = (move_t)(a >> 32);
    e.depth = (int8_t)(b >> 32);
    e.gen_flag = slot_gen_flag(b);
    e.static_eval = (int16_t)(b >> 48);
    return e;
}

static inline void store_slot(struct tt_slot *slot, uint32_t key, int32_t value, move_t move, int depth, u8 gen_flag, int16_t static_eval) {
    uint64_t a = (uint64_t)(uint32_t)value | ((uint64_t)move << 32);
    uint64_t b = (uint64_t)(key ^ fold(a)) | ((uint64_t)(uint8_t)(int8_t)depth << 32) | ((uint64_t)gen_flag << 40) | ((uint64_t)(uint16_t)static_eval << 48);
    store_word(&slot->a, a);
    store_word(&slot->b, b);
}

bool table_probe(struct table *table, u64 hash, int ply, struct hash_entry *out) {

    struct tt_slot *bucket = bucket_for(table, hash);
    uint32_t key = (uint32_t)(hash >> 32);

    for (int i = 0; i < BUCKET_SIZE; i++) {
        uint64_t a = load_word(&bucket[i].a);
        uint64_t b = load_word(&bucket[i].b);
        if (slot_key(a, b) == key && (slot_gen_flag(b) & 3) != NOENTRY) {
            *out = decode_slot(a, b);
            out->value = score_from_table(out->value, ply);
            return true;
        }
    }
    return false;
}

void table_store(struct table *table, u64 hash, int depth, int value, u8 flag, move_t move, int ply, int static_eval) {

    const int16_t ev = (static_eval > -32001 && static_eval < 32001) ? (int16_t)static_eval : (int16_t)TT_NO_EVAL;

    struct tt_slot *bucket = bucket_for(table, hash);
    uint32_t key = (uint32_t)(hash >> 32);
    u8 age = table->age;

    struct tt_slot *victim = NULL;
    int worst = 1 << 30;

    for (int i = 0; i < BUCKET_SIZE; i++) {
        uint64_t a = load_word(&bucket[i].a);
        uint64_t b = load_word(&bucket[i].b);
        u8 gf = slot_gen_flag(b);

        // is the position already in the bucket?
        if (slot_key(a, b) == key && (gf & 3) != NOENTRY) {
            struct hash_entry old = decode_slot(a, b);
            // keep the old move if we do not have a new one
            move_t new_move = move ? move : old.best_move;
            // the static evaluation is a property of the position: the new one if there is one, else the old one
            const int16_t keep_eval = ev != TT_NO_EVAL ? ev : old.static_eval;
            // overwrite the score unless the old one is from this search and clearly deeper
            if (flag == EXACT || depth + 3 >= old.depth || (old.gen_flag >> 2) != age) {
                store_slot(&bucket[i], key, score_to_table(value, ply), new_move, depth, (u8)(flag | (age << 2)), keep_eval);
            } else if (new_move != old.best_move || keep_eval != old.static_eval) {
                store_slot(&bucket[i], key, old.value, new_move, old.depth, old.gen_flag, keep_eval);
            }
            return;
        }

        // otherwise remember the least valuable entry: old entries are worth less, shallow entries are worth less
        int worth;
        if ((gf & 3) == NOENTRY) {
            worth = -(1 << 20);
        } else {
            int relative_age = (age - (gf >> 2)) & 63;
            worth = slot_depth(b) - 4 * relative_age;
        }
        if (worth < worst) {
            worst = worth;
            victim = &bucket[i];
        }
    }

    store_slot(victim, key, score_to_table(value, ply), move, depth, (u8)(flag | (age << 2)), ev);
}

void clear_tables() {
    struct table *all[2] = {trans_table, quiet_table};
    for (int i = 0; i < 2; i++) {
        if (all[i]) {
            memset(all[i]->entries, 0, (all[i]->bucket_mask + 1) * BUCKET_SIZE * sizeof(struct tt_slot));
            all[i]->age = 0;
        }
    }
}

void table_new_search(struct table *table) {
    table->age = (table->age + 1) & 63;
}

static struct table *allocate_table(size_t megabytes) {

    struct table *table = calloc(1, sizeof(struct table));
    if (table == NULL) {
        printf("Error: failed to allocate memory for table\n");
        exit(1);
    }

    size_t num_entries = (megabytes * 1024 * 1024) / sizeof(struct tt_slot);
    size_t num_buckets = num_entries / BUCKET_SIZE;

    // the number of buckets must be a power of 2 so that we can use a mask
    if (num_buckets & (num_buckets - 1)) {
        printf("Error: table size must be a power of 2 megabytes\n");
        exit(1);
    }

    size_t bytes = num_buckets * BUCKET_SIZE * sizeof(struct tt_slot);

#if defined(__linux__) && defined(USE_HUGEPAGES)
    // (only compiled with -DUSE_HUGEPAGES, off by default: on some systems faulting in huge pages is slow enough
    // to cost more than it saves in short searches, so it is not the default)
    // the table is accessed at random addresses, so with normal 4 kB pages almost every access misses the TLB.
    // a 2 MB aligned anonymous mapping that is marked for transparent huge pages avoids most of that (and also
    // needs far fewer page faults). like calloc the memory is zeroed and only really allocated when it is used
    const size_t huge_page = 2 * 1024 * 1024;
    if (bytes >= huge_page) {
        void *raw = mmap(NULL, bytes + huge_page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (raw != MAP_FAILED) {
            table->raw_memory = raw;
            table->raw_size = bytes + huge_page;
            table->is_mapped = true;
            table->entries = (struct tt_slot *)(((uintptr_t)raw + huge_page - 1) & ~(uintptr_t)(huge_page - 1));
#ifdef MADV_HUGEPAGE
            madvise(table->entries, bytes, MADV_HUGEPAGE);
#endif
        }
    }
#endif

    if (table->raw_memory == NULL) {
        // calloc gives zeroed memory that the OS only maps once it is used, but it is
        // not necessarily aligned to a cache line, so we align the buckets ourselves
        table->raw_memory = calloc(bytes + 64, 1);
        if (table->raw_memory == NULL) {
            printf("Error: failed to allocate memory for table\n");
            exit(1);
        }
        table->entries = (struct tt_slot *)(((uintptr_t)table->raw_memory + 63) & ~(uintptr_t)63);
    }
    table->bucket_mask = num_buckets - 1;
    table->age = 0;

    return table;
}

static void free_table(struct table *table) {
    if (table) {
#if defined(__linux__) && defined(USE_HUGEPAGES)
        if (table->is_mapped) {
            munmap(table->raw_memory, table->raw_size);
        } else {
            free(table->raw_memory);
        }
#else
        free(table->raw_memory);
#endif
        free(table);
    }
}

void init_tables(int main_mb) {
    trans_table = allocate_table(main_mb);
    quiet_table = allocate_table(main_mb >= 4 ? main_mb / 4 : 1);
}

void free_tables() {
    free_table(trans_table);
    free_table(quiet_table);
    trans_table = NULL;
    quiet_table = NULL;
}
