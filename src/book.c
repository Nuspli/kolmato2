#include "book.h"

static void read_checked(void *target, size_t size, size_t count, FILE *origin) {
    if (fread(target, size, count, origin) != count) {
        printf("book file is truncated or unreadable\n");
        exit(EXIT_FAILURE);
    }
}

struct book *book_entries;

struct book* read_book(struct book *book_pages, char* file_name) {

    // reads a custom opening book .dat file which was parsed by the parser into memory

    FILE *origin;
    origin = fopen(file_name, "rb");
    if (origin == NULL) {
        printf("opening book not found\n");
        exit(EXIT_FAILURE);
    }

    printf("reading book\n");
    int total = 0;

    for (int i = 0; i < BOOK_SIZE; i++) {

        // the book is made of BOOK_SIZE pages each containing an unknown number of entries
        // this is because the book cant be directly accessed by the hash as that would require too much space (2^64)
        // different hashes can share the same index (page) of the book since it is calculated by hash % BOOK_SIZE

        read_checked(&book_pages[i].num_entries, sizeof(int), 1, origin);
        book_pages[i].entries = malloc(sizeof(struct book_entry) * book_pages[i].num_entries);

        if (book_pages[i].entries == NULL) {
            printf("malloc failed\n");
            exit(EXIT_FAILURE);
        }

        for (int j = 0; j < book_pages[i].num_entries; j++) {

            // each of the entries should have a unique hash and all of the moves that have been played at that position
            // the entry also contains the corresponding amount of times the move was played

            read_checked(&book_pages[i].entries[j].hash, sizeof(u64), 1, origin);
            read_checked(&book_pages[i].entries[j].num_moves, sizeof(u8), 1, origin);
            book_pages[i].entries[j].moves = malloc(sizeof(move_t) * book_pages[i].entries[j].num_moves);
            book_pages[i].entries[j].occourences = malloc(sizeof(int) * book_pages[i].entries[j].num_moves);

            if (book_pages[i].entries[j].moves == NULL || book_pages[i].entries[j].occourences == NULL) {
                printf("malloc failed\n");
                exit(EXIT_FAILURE);
            }
            
            for (int l = 0; l < book_pages[i].entries[j].num_moves; l++) {
                read_checked(&book_pages[i].entries[j].moves[l], sizeof(move_t), 1, origin);
                read_checked(&book_pages[i].entries[j].occourences[l], sizeof(int), 1, origin);
            }
            total++;
        }
    }

    fclose(origin);
    printf("total positions in book: %d\n", total);
    return book_pages;
}

void write_book(struct book *book_pages, char *file_name) {

    // writes a book from memory to a .dat file
    // this is used in the parser only

    FILE *out;
    out = fopen(file_name, "wb");
    if (out == NULL) {
        printf("opening book not found\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < BOOK_SIZE; i++) {
        // for page i, write the amount of entries on that page
        // then all of the entries
        fwrite(&book_pages[i].num_entries, sizeof(int), 1, out);

        for (int j = 0; j < book_pages[i].num_entries; j++) {
            // for entry j, write the hash and the amount of moves played for that position
            // followed by all of the moves and their occourences
            fwrite(&book_pages[i].entries[j].hash, sizeof(u64), 1, out);
            fwrite(&book_pages[i].entries[j].num_moves, sizeof(u8), 1, out);

            for (int l = 0; l < book_pages[i].entries[j].num_moves; l++) {

                fwrite(&book_pages[i].entries[j].moves[l], sizeof(move_t), 1, out);
                fwrite(&book_pages[i].entries[j].occourences[l], sizeof(int), 1, out);
            }
        }
    }
    fclose(out);
    printf("finished writing book\n");
}

void init_book(char *book_name) {
    book_entries = malloc(sizeof(struct book) * BOOK_SIZE);
    book_entries = memset(book_entries, 0, sizeof(struct book) * BOOK_SIZE);
    if (book_entries == NULL) {
        printf("malloc failed\n");
        exit(EXIT_FAILURE);
    }
    // specify the filename here if you compiled your own book
    book_entries = read_book(book_entries, book_name);
    printf("book read\n");
}

static void quick_sort_array_dec(move_t moves[], int values[], int left, int right) {
    // sorts the moves decreasingly in order of their evaluation
    if (left < right) {
        // Choose pivot value
        int pivot_value = values[left + (right - left) / 2];

        // Partition the arrays
        int i = left, j = right;
        while (i <= j) {
            while (values[i] > pivot_value) {i++;}
            while (values[j] < pivot_value) {j--;}
            if (i <= j) {
                // Swap elements
                int tmp_value = values[i];
                values[i] = values[j];
                values[j] = tmp_value;
                move_t tmp_move = moves[i];
                moves[i] = moves[j];
                moves[j] = tmp_move;
                i++;
                j--;
            }
        }

        // Recursively sort the sub-arrays
        quick_sort_array_dec(moves, values, left, j);
        quick_sort_array_dec(moves, values, i, right);
    }
}

// why have two different sorting algorithms?
// because a stable one (merge sort) is used for the 1st depth to sort the moves in order to get the best move and keep
// the principal variation, and a fast one that doesnt need to be stable for move ordering

// book selection settings. the defaults keep the old behaviour of the -b/-g modes (any move, some randomness);
// the UCI mode sets them from its options (BookMinGames, BookRandom) before every probe
int book_min_count = 1; // a book move must have been played at least this many times in the source games
bool book_random = true; // false: always the most common move

// how often the book has seen this position (the sum of the occurrences of all moves played from it), 0 if the position
// is not in the book. no output, so it can be used in uci mode
int book_position_weight(u64 hash) {
    if (!book_entries) {
        return 0;
    }
    struct book page = book_entries[hash % BOOK_SIZE];
    for (int i = 0; i < page.num_entries; i++) {
        if (page.entries[i].hash == hash) {
            int sum = 0;
            for (int j = 0; j < page.entries[i].num_moves; j++) {
                sum += page.entries[i].occourences[j];
            }
            return sum;
        }
    }
    return 0;
}

move_t get_book_move(u64 hash) {

    if (use_book && game_moves_played < 20) {
        // > 20 is not necessary because the book is not that big

        if (!book_entries) {
            printf("book_entries is null\n");
            exit(EXIT_FAILURE);
        }

        struct book page = book_entries[hash % BOOK_SIZE];

        printf("book entries: %d\n", page.num_entries);

        for (int i = 0; i < page.num_entries; i++) {
            // read all entries on the page
            if (page.entries[i].hash == hash) {

                printf("num moves: %d\n", page.entries[i].num_moves);

                quick_sort_array_dec(page.entries[i].moves, page.entries[i].occourences, 0, page.entries[i].num_moves - 1);

                for (int j = 0; j < page.entries[i].num_moves; j++) {
                    printf("%s%s %d\n", notation[m_from(page.entries[i].moves[j])], notation[m_to(page.entries[i].moves[j])], page.entries[i].occourences[j]);
                }
                // only moves that were played often enough are trusted (the moves are sorted by occurrences, so
                // the qualifying moves are a prefix). if even the most common move is too rare: out of book
                int qualified = 0;
                while (qualified < page.entries[i].num_moves && page.entries[i].occourences[qualified] >= book_min_count) {
                    qualified++;
                }
                if (qualified == 0) {
                    printf("book moves too rare (best: %d games)\n", page.entries[i].occourences[0]);
                    return 0;
                }

                // randomize the move selection
                int index;
                if (!book_random) {
                    index = 0;
                } else if (qualified > 3) {
                    // moves are weighted by occourences
                    if (rand() % 100 > 10) {
                        // 90% chance to pick the first move
                        index = 0;
                    } else {
                        if (rand() % 100 > 10) {
                            // 9% chance to pick the second move
                            index = 1;
                        } else {
                            // 1% chance to pick the third move
                            index = 2;
                        }
                    }
                    printf("random index: %d\n", index);
                } else {
                    index = 0;
                }

                return page.entries[i].moves[index];
            }
        }

        printf("out of book\n");
    }
    
    // "null" move
    return 0;
}
