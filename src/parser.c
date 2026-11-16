// old code. todo: replace custom book with a common format

#include "parser.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "bitboards.h"
#include "book.h"
#include "move.h"
#include "fen.h"
#include "move_generation.h"
#include "print.h"
#include "check.h"

#include <errno.h>

// this is not recommended but
// in order to make your own book and run this, you need to have some additional files

// DOWNLOADED GAMES FROM: https://database.nikonoel.fr/

// the PGN has to be preparsed to FEN notation before
// example result: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1, rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1, ... ,
// 1 line is 1 entire game, it should end with a comma
// to parse the PGN to this format I used a simple python script which used the chess library


char* file_list[79] = {
    "Lichess Elite Database fens/lichess_elite_2013-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2013-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2014-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2015-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2016-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2017-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2018-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-05.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-06.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-07.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-08.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-09.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-10.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-11.pgn",
    "Lichess Elite Database fens/lichess_elite_2019-12.pgn",
    "Lichess Elite Database fens/lichess_elite_2020-01.pgn",
    "Lichess Elite Database fens/lichess_elite_2020-02.pgn",
    "Lichess Elite Database fens/lichess_elite_2020-03.pgn",
    "Lichess Elite Database fens/lichess_elite_2020-04.pgn",
    "Lichess Elite Database fens/lichess_elite_2020-05.pgn"
};

char book_file_name[64] = "bookMax.dat";

// all of the files combined is exactly 3818730 games

void parse_book() {
    // parse the opening book txt files to a binary file
    struct book* book_pgs;
    FILE *fens;
    char *line = NULL;
    int line_num = 0;
    size_t len = 0;
    int read;
    int file_counter = 0;

    book_pgs = calloc(BOOK_SIZE, sizeof(struct book));
    // read_book(book_pgs, "book45.dat"); // continue from an already parsed book

    if (book_pgs == NULL) {
        printf("malloc book failed\n");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < 79; i++) {
        fens = fopen(file_list[i], "r");
        if (fens == NULL) {
            printf("fens book not found\n");
            exit(EXIT_FAILURE);
        }

        printf("file: %d\n", file_counter);

        while ((read = getline(&line, &len, fens)) != -1) {
            if (line_num % 5000 == 0) {printf("line: %d\n", line_num);}
            line_num++;
            if (line[0] == '\n') {break;}
            char fen[90];
            int j = 0;
            u64 last_hash = 0;
            move_t tmp_moves[MAX_NUM_MOVES];
            u64 hash_list[MAX_NUM_MOVES] = {0};

            int position_counter = 0;
            
            for (int i = 0; i < (int)strlen(line); i++) {
                if (position_counter >= 20) {break;} // anything above 20 moves is too deep for the opening book
                bool is_white = true;
                char castling_str[5] = {'\0', '\0', '\0', '\0', '\0'};
                char en_passant_str[3] = {'\0', '\0', '\0'};

                if (line[i] == ' ') {
                    position_counter++;

                    fen[j] = '\0';
                    i++;
                    if (line[i] == 'w') {
                        is_white = true;
                    } else {
                        is_white = false;
                    }
                    i += 2;
                    castling_str[0] = line[i];
                    i++;
                    if (line[i] != ' ') {
                        castling_str[1] = line[i];
                        i++;
                        if (line[i] != ' ') {
                            castling_str[2] = line[i];
                            i++;
                            if (line[i] != ' ') {
                                castling_str[3] = line[i];
                                i++;
                            }
                        }
                    }
                    i++;
                    en_passant_str[0] = line[i];
                    i++;
                    if (line[i] != ' ') {
                        en_passant_str[1] = line[i];
                        i++;
                    }
                    i++;
                    if (line[i+1] != ' ') {
                        if (line[i+2] == ' ') {
                            i++;
                        } else {
                            i += 2;
                        }
                    }
                    i += 2;
                    if (line[i+1] != ',') {
                        if (line[i+2] == ',') {
                            i++;
                        } else {
                            i += 2;
                        }
                    }
                    i += 2;
                    j = 0;

                    fen_to_position(fen, bitboards);
                    init_boards_light(bitboards, is_white, castling_str, en_passant_str);

                    if (hash_list[0] != 0) {
                        {
                            if (last_hash) {
                                int y = 0;

                                while (hash_list[y] != 0) {

                                    if (hash_list[y] == bitboards->hash) {

                                        struct book page = book_pgs[last_hash % BOOK_SIZE];
                                        bool entry_found = false;
                                        for (int t = 0; t < page.num_entries; t++) {
                                            if (last_hash == page.entries[t].hash) {

                                                entry_found = true;
                                                int index = -1;
                                                for (int z = 0; z < page.entries[t].num_moves; z++) {
                                                    if (tmp_moves[y] == page.entries[t].moves[z]) {
                                                        index = z;
                                                        break;
                                                    }
                                                }
                                                if (index != -1) {
                                                    page.entries[t].occourences[index]++;
                                                } else {
                                                    page.entries[t].num_moves++;
                                                    page.entries[t].moves = realloc(page.entries[t].moves, page.entries[t].num_moves * sizeof(move_t));
                                                    page.entries[t].moves[page.entries[t].num_moves-1] = tmp_moves[y];
                                                    page.entries[t].occourences = realloc(page.entries[t].occourences, page.entries[t].num_moves * sizeof(int));
                                                    page.entries[t].occourences[page.entries[t].num_moves-1] = 1;
                                                    if (page.entries[t].moves == NULL) {
                                                        printf("realloc failed for moves\n");
                                                        write_book(book_pgs, book_file_name);
                                                        exit(EXIT_FAILURE);
                                                    }
                                                    if (page.entries[t].occourences == NULL) {
                                                        printf("realloc failed for occourences\n");
                                                        write_book(book_pgs, book_file_name);
                                                        exit(EXIT_FAILURE);
                                                    }
                                                }
                                                book_pgs[last_hash % BOOK_SIZE] = page;
                                                break;
                                            }
                                        }
                                        if (!entry_found) {

                                            if (page.num_entries == 0) {
                                                page.entries = malloc(sizeof(struct book_entry));
                                                if (page.entries == NULL) {
                                                    printf("malloc failed for new entry\n");
                                                    write_book(book_pgs, book_file_name);
                                                    exit(EXIT_FAILURE);
                                                }
                                            } else {
                                                page.entries = realloc(page.entries, (page.num_entries + 1) * sizeof(struct book_entry));
                                                if (page.entries == NULL) {
                                                    printf("realloc failed for new entry\n");
                                                    write_book(book_pgs, book_file_name);
                                                    exit(EXIT_FAILURE);
                                                }
                                            }
                                            page.entries[page.num_entries].hash = last_hash;
                                            page.entries[page.num_entries].num_moves = 1;
                                            page.entries[page.num_entries].moves = malloc(sizeof(move_t));
                                            page.entries[page.num_entries].moves[0] = tmp_moves[y];
                                            page.entries[page.num_entries].occourences = malloc(sizeof(int));
                                            page.entries[page.num_entries].occourences[0] = 1;
                                            if (page.entries[page.num_entries].moves == NULL) {
                                                printf("malloc failed for moves of new entry\n");
                                                write_book(book_pgs, book_file_name);
                                                exit(EXIT_FAILURE);
                                            }
                                            if (page.entries[page.num_entries].occourences == NULL) {
                                                printf("malloc failed for occourences of new entry\n");
                                                write_book(book_pgs, book_file_name);
                                                exit(EXIT_FAILURE);
                                            }
                                            page.num_entries++;
                                            book_pgs[last_hash % BOOK_SIZE] = page;
                                        }
                                        break;
                                    }
                                    y++;
                                }
                            }
                        }
                    }

                    int num_moves;
                    u64 checkers;
                    is_in_check(bitboards, &checkers);

                    u64 pinned = 0;
                    u8 pinners[64] = {0};
                    get_pins(bitboards, &pinned, &pinners[0]);
                    u64 attacks = get_enemy_attack_mask(bitboards);
                    num_moves = get_moves(bitboards, &tmp_moves[0], checkers, pinned, &pinners[0], attacks);

                    for (int i = 0; i < num_moves; i++) {
                        hash_list[i] = do_move_light(tmp_moves[i], bitboards);
                    }

                    last_hash = bitboards->hash;

                    reset_boards_light(bitboards);

                } else {
                    fen[j] = line[i];
                    j++;
                }
            }
        }
        file_counter++;

        fclose(fens);
    }

    printf("finished reading fens\n");

    // FMA 35 is next

    // test the book using some fen
    reset_boards(bitboards);
    // rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    fen_to_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR\0", bitboards);
    init_boards(bitboards, 1, "KQkq", "-", 0, 1);

    print_board(bitboards);
    u64 hash = bitboards->hash % BOOK_SIZE;
    struct book page = book_pgs[hash];
    bool found_hash = false;

    for (int i = 0; i < page.num_entries; i++) {
        if (page.entries[i].hash == bitboards->hash) {
            printf("found hash: %llu\n", (unsigned long long)page.entries[i].hash);
            printf("moves: %d\n", page.entries[i].num_moves);
            for (int j = 0; j < page.entries[i].num_moves; j++) {
                printf("%s%s %d\n", notation[m_from(page.entries[i].moves[j])], notation[m_to(page.entries[i].moves[j])], page.entries[i].occourences[j]);
            }
            found_hash = true;
        }
    }

    if (!found_hash) {
        printf("hash not found\n");
    }

    printf("writing...\n");
    write_book(book_pgs, book_file_name);
    free(book_pgs);

    printf("opening book again\n");
    struct book* loaded_book_pgs;
    loaded_book_pgs = calloc(BOOK_SIZE, sizeof(struct book));
    read_book(loaded_book_pgs, book_file_name);

    reset_boards(bitboards);

    // rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
    fen_to_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR\0", bitboards);
    init_boards(bitboards, 1, "KQkq", "-", 0, 1);

    print_board(bitboards);
    u64 hash2 = bitboards->hash % BOOK_SIZE;
    struct book page2 = loaded_book_pgs[hash2];
    bool found_hash2 = false;

    for (int i = 0; i < page2.num_entries; i++) {
        if (page2.entries[i].hash == bitboards->hash) {
            printf("found hash: %llu\n", (unsigned long long)page2.entries[i].hash);
            printf("moves: %d\n", page2.entries[i].num_moves);
            for (int j = 0; j < page2.entries[i].num_moves; j++) {
                printf("%s%s %d\n", notation[m_from(page2.entries[i].moves[j])], notation[m_to(page2.entries[i].moves[j])], page2.entries[i].occourences[j]);
            }

            found_hash2 = true;
        }
    }

    if (!found_hash2) {
        printf("hash not found\n");
    }

    free(loaded_book_pgs);

    exit(0);
}
