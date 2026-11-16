#include "main.h"
#include <unistd.h>
#include <errno.h>

int uci_main(void);

#define DEFAULT_TIME_SECONDS 10
#define DEFAULT_DEPTH 100 // the same limit as in uci mode

// prints an error and exits. the message must be a string literal
#define die(fmt, ...) \
    do { \
        fprintf(stderr, "error: " fmt "\nrun with -h for help\n", ##__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while (0)

// every option can be written as -x, -name or --name, for example -t, -time and --time
#define ARG_IS(arg, s, l) (!strcmp((arg), "-" s) || !strcmp((arg), "-" l) || !strcmp((arg), "--" l))

// the options are chained with else if. an option with a number: the value is the next argument, and i ends up behind it
#define ARG_NUM(S, L, TARGET, PARSE, MIN, MAX) \
    else if (ARG_IS(argv[i], S, L)) { \
        if (++i >= argc) die("missing value for %s", argv[i - 1]); \
        (TARGET) = PARSE(argv[i - 1], argv[i], (MIN), (MAX)); \
        i++; \
    }

// an option with a string as value
#define ARG_STR(S, L, TARGET) \
    else if (ARG_IS(argv[i], S, L)) { \
        if (++i >= argc) die("missing value for %s", argv[i - 1]); \
        (TARGET) = argv[i++]; \
    }

enum mode {
    MODE_HELP,
    MODE_EVALUATE,
    MODE_BEST,
    MODE_GAME,
    MODE_PERFT
};

struct config {
    enum mode mode;
    char *fen[6];    // the six fields of a fen: pieces, side to move, castling, en passant, halfmove clock, move number
    double time;     // seconds per move
    int depth;
    char *book;      // NULL = no opening book
    int hash_mb;     // 0 = depends on the time limit
};

// the position of "startpos", given as the six fields of a fen
static char *startpos_fen[6] = {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", "w", "KQkq", "-", "0", "1"};

static double parse_double(const char *opt, const char *text, double min, double max) {
    char *end;
    double value = strtod(text, &end);
    if (end == text || *end != '\0' || value < min || value > max) {
        die("invalid value for %s: %s (expected a number from %g to %g)", opt, text, min, max);
    }
    return value;
}

static int parse_int(const char *opt, const char *text, double min, double max) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value < min || value > max) {
        die("invalid value for %s: %s (expected a whole number from %.0f to %.0f)", opt, text, min, max);
    }
    return (int)value;
}

// reads the position at argv[i]: "startpos" or a fen. a fen is given as separate arguments (no quotes around the whole
// thing), the first four fields are required and the two clocks are optional. a clock never starts with '-', so the
// options that may follow can not be mistaken for one. returns the index of the first argument after the position
static int parse_position(int argc, char **argv, int i, struct config *cfg) {
    if (i >= argc) die("please provide a fen string or startpos");

    if (!strcmp(argv[i], "startpos")) {
        memcpy(cfg->fen, startpos_fen, sizeof(cfg->fen));
        return i + 1;
    }

    if (i + 3 >= argc) die("a fen needs at least 4 fields (pieces, side to move, castling, en passant), or use startpos");
    for (int field = 0; field < 4; field++) {
        cfg->fen[field] = argv[i + field];
    }
    cfg->fen[4] = "0";
    cfg->fen[5] = "1";
    i += 4;
    for (int field = 4; field < 6 && i < argc && argv[i][0] != '-'; field++) {
        cfg->fen[field] = argv[i++];
    }

    if (strcmp(cfg->fen[1], "w") && strcmp(cfg->fen[1], "b")) {
        die("the side to move must be w or b, got %s", cfg->fen[1]);
    }
    return i;
}

static void parse_args(int argc, char **argv, struct config *cfg) {
    cfg->time = DEFAULT_TIME_SECONDS;
    cfg->depth = DEFAULT_DEPTH;
    cfg->book = NULL;
    cfg->hash_mb = 0;

    if (ARG_IS(argv[1], "h", "help")) {
        cfg->mode = MODE_HELP;
        return;
    }
    else if (ARG_IS(argv[1], "e", "evaluate")) cfg->mode = MODE_EVALUATE;
    else if (ARG_IS(argv[1], "b", "best")) cfg->mode = MODE_BEST;
    else if (ARG_IS(argv[1], "g", "game")) cfg->mode = MODE_GAME;
    else if (ARG_IS(argv[1], "p", "perft")) cfg->mode = MODE_PERFT;
    else die("unknown command %s", argv[1]);

    int i = parse_position(argc, argv, 2, cfg);

    // the options can be given in any order. -hash is not documented in the help, it is used by the scripts in tools/
    bool takes_options = cfg->mode == MODE_BEST || cfg->mode == MODE_GAME;
    while (i < argc) {
        if (!takes_options) die("unexpected argument %s", argv[i]);
        ARG_NUM("t", "time", cfg->time, parse_double, 0.001, 1e6)
        ARG_NUM("d", "depth", cfg->depth, parse_int, 1, DEFAULT_DEPTH)
        ARG_NUM("hash", "hash", cfg->hash_mb, parse_int, 1, 65536)
        ARG_STR("b", "book", cfg->book)
        else die("unknown option %s", argv[i]);
    }
}

// the hash tables only need to be as large as the search can fill. a new table has to be faulted in by the operating
// system page by page, which costs a lot of the time of a short search, so the size depends on the time limit: about
// 32 MB per second, at least TT_MIN_SIZE_MB and at most TT_MAX_SIZE_MB. "-hash <MB>" sets the size explicitly
static int choose_hash_size_mb(double seconds, int requested) {
    int mb = TT_MIN_SIZE_MB;
    if (requested > 0) {
        // round down to a power of 2
        mb = 4;
        while (mb * 2 <= requested && mb < 65536) {
            mb *= 2;
        }
    } else {
        while (mb < 32 * seconds && mb < TT_MAX_SIZE_MB) {
            mb *= 2;
        }
    }
    return mb;
}

static void engine_move(void);

static void load_position(const struct config *cfg) {
    fen_to_position(cfg->fen[0], bitboards);
    init_boards(bitboards, !strcmp(cfg->fen[1], "w"), cfg->fen[2], cfg->fen[3], atoi(cfg->fen[4]), atoi(cfg->fen[5]));
}

// the limits of -b and -g go into the globals the search reads
static void apply_search_limits(const struct config *cfg) {
    max_time = cfg->time;
    max_depth = cfg->depth;
    if (cfg->book) {
        use_book = true;
        book_name = cfg->book;
    }
    printf("max time: %.2f\n", max_time);
    printf("max depth: %d\n", max_depth);
    printf("use book: %d\n", use_book);
    if (use_book) {
        init_book(book_name);
    }
}

static void free_all(void) {
    if (use_book) {
        free(book_entries);
    }
    free_tables();
    free_boards();
}

static void run_evaluate(const struct config *cfg) {
    load_position(cfg);

    int light_eval = evaluate(bitboards);
    printf("light evaluation: %d\n", light_eval);

    int eval = quiescence_search(bitboards, -INF, INF, 0);
    printf("quiet evaluation: %d\n", eval);
    printf("running deeper evaluation (15ply)...\n");
    int deep_eval = nega_max(bitboards, 15, -INF, INF, true, false);
    printf("deeper evaluation: %d\n", deep_eval);
    print_board(bitboards);
}

static void run_best(const struct config *cfg) {
    load_position(cfg);
    print_board(bitboards);
    apply_search_limits(cfg);
    engine_move();
    free_all();
}

static void run_perft(const struct config *cfg) {
    // used for debugging
    load_position(cfg);
    print_board(bitboards);

    printf("depth: ");
    int depth;
    if (scanf("%d", &depth) != 1) depth = 0;

    clock_t start = clock();
    u64 bulk = perft(depth, bitboards, depth);
    clock_t end = clock();

    printf("bulk: %llu\n", (unsigned long long)bulk);
    printf("Elapsed time: %.2f milliseconds\n", (double)(end - start) / CLOCKS_PER_SEC * 1000);
}

static void run_game(const struct config *cfg) {
    load_position(cfg);
    bool is_white_to_move = !strcmp(cfg->fen[1], "w");
    print_board(bitboards);
    apply_search_limits(cfg);

    printf("play mode entered\n");
    printf("what color do you want to play as? (w/b)\n");
    char color[2];
    if (scanf("%1s", color) != 1) color[0] = 0;
    int is_player_white = strcmp(color, "w") == 0 ? 1 : 0;
    printf("you play as %s\n", is_player_white ? "white" : "black");
    printf("to make a move type it in the format: e2e4\n");
    printf("to quit type: quit [WARNING: NOT QUITTING CAN CAUSE MEMORY LEAKS]\n");
    printf("on promoting, you will be asked to enter a piece type in this format:\n");
    printf("q for queen, r for rook, b for bishop, n for knight\n");
    printf("continue? (1/0)\n");

    int cont = 0;
    if (scanf("%d", &cont) != 1) cont = 0;
    if (!cont) {
        return;
    }

    game_history[game_moves_played++] = bitboards->hash;

    if (is_white_to_move != is_player_white) {
        engine_move();
    }

    while (true) {
        printf("your move: ");
        char notation_move[6];
        if (scanf("%5s", notation_move) != 1) strcpy(notation_move, "quit");
        if (strcmp(notation_move, "quit") == 0) {
            free_all();
            return;
        }
        move_t move = build_move(notation_move, bitboards);

        move_t legal_moves[MAX_NUM_MOVES];
        int num_moves;

        u64 checkers;
        is_in_check(bitboards, &checkers);

        u64 pinned = 0;
        u8 pinners[64] = {0};
        get_pins(bitboards, &pinned, &pinners[0]);
        u64 attacks = get_enemy_attack_mask(bitboards);
        num_moves = get_moves(bitboards, &legal_moves[0], checkers, pinned, &pinners[0], attacks);

        bool is_legal = false;
        for (int i = 0; i < num_moves; i++) {
            if (legal_moves[i] == move) {
                is_legal = true;
                break;
            }
        }
        if (is_legal) {
            printf("updated board:\n");
            update_fen_clocks(move);
            struct undo undo;
            do_move(move, bitboards, &undo);

            const char *draw_because = draw_reason(bitboards);
            game_history[game_moves_played++] = bitboards->hash;

            print_board(bitboards);

            if (draw_because) {
                printf("draw by %s\n", draw_because);
                free_all();
                return;
            }

            engine_move();
        } else {
            printf("illegal move\n");
            printf("legal moves:\n");
            for (int i = 0; i < num_moves; i++) {
                printf("%s%s\n", notation[m_from(legal_moves[i])], notation[m_to(legal_moves[i])]);
            }
        }
    }
}

int main(int argc, char *argv[]) {
#if defined(__BMI__) && defined(__BMI2__)
    // built with -mbmi -mbmi2 (see the Makefile): a cpu without them would crash with an illegal instruction somewhere in the search, so say it here instead
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("bmi") || !__builtin_cpu_supports("bmi2")) {
        fprintf(stderr, "this binary was built with BMI1 / BMI2 instructions, which this cpu does not have. build it with: make PORTABLE=1\n");
        return 1;
    }
#endif

    // uci mode: started as "kolmato2 uci", or without arguments when the input is not a terminal (how chess programs
    // start engines). in this mode nothing but protocol messages may be printed
    bool uci_mode = (argc == 2 && (strcmp(argv[1], "uci") == 0 || strcmp(argv[1], "-uci") == 0 || strcmp(argv[1], "--uci") == 0)) ||
                   (argc == 1 && !isatty(STDIN_FILENO));

    struct config cfg;
    if (!uci_mode) {
        if (argc == 1) {
            print_logo();
            print_help();
            return 0;
        }
        parse_args(argc, argv, &cfg);
        if (cfg.mode == MODE_HELP) {
            print_logo();
            print_help();
            return 0;
        }
        printf("initializing...");
    }
    init_king_attacks();
    init_knight_attacks();
    init_sliding_piece_attacks(true);
    init_sliding_piece_attacks(false);
    init_squares_between();
    init_zobrist();
    init_tables(uci_mode ? choose_hash_size_mb(DEFAULT_TIME_SECONDS, 0) : choose_hash_size_mb(cfg.time, cfg.hash_mb));
    allocate_boards();

    if (uci_mode) {
        int result = uci_main();
        free_tables();
        free_boards();
        return result;
    }

    srand(time(NULL));

    printf("OK\n");

    // to find new magic numbers or to rebuild the opening book, call generate_new_magics() / parse_book() here once

    switch (cfg.mode) {
        case MODE_EVALUATE: run_evaluate(&cfg); break;
        case MODE_BEST:     run_best(&cfg);     break;
        case MODE_GAME:     run_game(&cfg);     break;
        case MODE_PERFT:    run_perft(&cfg);    break;
        default: break;
    }
    return 0;
}

// the engine plays one move in the console modes (-g): book, else search
static void engine_move(void) {
    reset_search();
    // find and make the engine move
    clock_t start_time = clock();
    move_t best = 0;
    // probe the book
    move_t book_move = get_book_move(book_hash(bitboards));
    int num_moves = 0;
    if (book_move != 0) {
        best = book_move;

    } else {
        move_t possible[MAX_NUM_MOVES];
        u64 checkers;
        is_in_check(bitboards, &checkers);
        u64 pinned = 0;
        u8 pinners[64] = {0};
        get_pins(bitboards, &pinned, &pinners[0]);
        u64 attacks = get_enemy_attack_mask(bitboards);
        num_moves = get_moves(bitboards, &possible[0], checkers, pinned, &pinners[0], attacks);
        if (num_moves > 0) {
            best = iter_deepening(possible, bitboards, num_moves);
        } 
    }

    if (best != 0) { // if there is a legal move
        update_fen_clocks(best);

        struct undo undo;
        do_move(best, bitboards, &undo);

        const char *draw_because = draw_reason(bitboards);
        game_history[game_moves_played++] = bitboards->hash;

        printf("final move: %s%s%s\n", notation[m_from(best)], notation[m_to(best)], promo_suffix[m_promo(best)]);
        int s = bitboards->color;
        printf("quick evaluation: %d\n", s * quiescence_search(bitboards, -INF, INF, 0));
        printf("board after move\n");
        print_board(bitboards);

        if (draw_because) {
            printf("draw by %s\n", draw_because);
            free_tables();
            free_boards();
            if (use_book) {
                free(book_entries);
            }
            exit(0);
        }

        printf("fullMoveCount: %d\n", full_move_count);

        printf("game phase: %.2f\n", game_phase_of(bitboards));

        printf("nodes searched: %d (%d quiet)\n", nodes, quiet_nodes);
    } else {
        bitboards->color = -bitboards->color;
        if (can_capture_opponents_king(bitboards)) {
            printf("engine is mate\n");
        } else {
            printf("draw\n");
        }
        exit(0);
    }

    // log some stats
    clock_t end_time = clock();
    double elapsed_time = ((double)(end_time - start_time)) / (CLOCKS_PER_SEC / 1000); // Convert to milliseconds
    printf("Elapsed time: %.2f milliseconds\n", elapsed_time);
    printf("nodes per second: %d (%d quiet)\n", (int)(nodes/elapsed_time*1000), (int)(quiet_nodes/elapsed_time*1000));
    printf("evaluations per second: %d\n", (int)(eval_calls/elapsed_time*1000));

    // checkmate detection
    move_t othermoves[MAX_NUM_MOVES];
    int num_other_moves;

    u64 checkers;
    bool in_check = is_in_check(bitboards, &checkers);
    u64 pinned = 0;
    u8 pinners[64] = {0};
    get_pins(bitboards, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(bitboards);
    num_other_moves = get_moves(bitboards, &othermoves[0], checkers, pinned, &pinners[0], attacks);

    if (num_other_moves == 0) {
        if (in_check) {
            printf("player is mate\n");
        } else {
            printf("draw\n");
        }
        exit(0);
    }

    printf("--------------------function calls-------------------\n");
    printf("eval: %d moves: %d possible: %d quiet: %d\n", eval_calls, move_calls, possible_calls, quiescence_calls);
}
