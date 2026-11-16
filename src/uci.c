// UCI (universal chess interface) mode of kolmato2.
//
// Start the engine with "kolmato2 uci" (or without arguments when its input is not a terminal, which is how
// chess GUIs and lichess-bot start engines) and talk to it with the commands of the UCI protocol:
//
//   uci | isready | setoption name <name> [value <value>] | ucinewgame | position ... | go ... | stop | ponderhit | quit
//
// Everything the engine prints for the other modes (board diagrams, the list of root moves) is thrown away in this mode,
// only protocol messages are written. The search only needs a few small hooks (see search.h): it asks us every
// TIME_CHECK_MASK + 1 nodes (see timeman.c) whether it has to stop, and tells us after every iteration what it found.

#include "main.h"
#include "history.h"
#include <unistd.h>
#include <sys/select.h>
#include <stdarg.h>
#include <strings.h>

#define ENGINE_NAME "kolmato2"
#define ENGINE_AUTHOR "kolmus"

static int hash_mb = 128;                   // option Hash: size of the main table, the quiescence table gets a quarter of it
static int move_overhead_ms = 50;           // option Move Overhead: time that is lost between the engine and the gui for every move
static bool own_book = false;               // option OwnBook
static int book_min_games = 20;             // option BookMinGames: a book move must have been played at least this often
static bool book_random_option = false;     // option BookRandom: false = always the most common book move
static char book_file[256] = "bookMax.dat"; // option BookFile
static bool ponder_option = false;          // option Ponder: the gui may think on the opponent's time, bestmove then carries the move to ponder on
static bool book_loaded = false;
static bool tables_ready = false; // the tables were allocated with the current size and have been cleared

static FILE *out = NULL; // the real standard output (stdout itself is redirected to /dev/null)
static FILE *debug_log = NULL;              // option DebugLog: every command received (>>) and every line sent (<<), with the time

static void log_line(const char *direction, const char *text) {
    if (!debug_log) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    fprintf(debug_log, "%lld.%03ld %s %s\n", (long long)now.tv_sec, now.tv_nsec / 1000000, direction, text);
    fflush(debug_log);
}

static void send(const char *format, ...) {
    char text[8192];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    fprintf(out, "%s\n", text);
    fflush(out);
    log_line("<<", text);
}

// input. commands are read with read() and our own buffer instead of stdio, because the search has to look for a "stop"
// command while it is running, without blocking and without losing other commands that arrived in the meantime
static char inbuf[1 << 16];
static int inlen = 0;
static bool input_closed = false;
static bool quit_requested = false;
static bool stop_received = false;

// pondering (see go_command): "go ponder" searches the position after the expected reply without limits, "ponderhit" turns that search into a normal one
static bool pondering = false; // a go ponder command is being handled
static bool ponder_hit = false; // ... and its ponderhit has arrived
static long long ponder_node_base = 0; // nodes searched before the ponderhit (not counted in nps)
static bool ponder_book_found = false; // the book has a move for the position after the ponderhit: play it, do not search on
static move_t ponder_book = 0;

// reads what is available (block: wait until something arrives). returns false at the end of the input
static bool fill_input(bool block) {
    if (input_closed) {
        return false;
    }
    if (inlen >= (int)sizeof(inbuf) - 1) {
        return true;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    struct timeval none = {0, 0};
    int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, block ? NULL : &none);
    if (ready <= 0) {
        return true;
    }
    ssize_t n = read(STDIN_FILENO, inbuf + inlen, sizeof(inbuf) - 1 - inlen);
    if (n <= 0) {
        input_closed = true;
        return false;
    }
    inlen += (int)n;
    return true;
}

static void remove_from_buffer(int start, int length) {
    memmove(inbuf + start, inbuf + start + length, inlen - start - length);
    inlen -= length;
}

// takes the first complete line out of the buffer
static bool pop_line(char *line, size_t capacity) {
    char *newline = memchr(inbuf, '\n', inlen);
    int length;
    if (newline) {
        length = (int)(newline - inbuf);
    } else if (input_closed && inlen > 0) {
        length = inlen; // the last line had no newline
    } else {
        return false;
    }
    size_t copy = (size_t)length < capacity - 1 ? (size_t)length : capacity - 1;
    memcpy(line, inbuf, copy);
    line[copy] = 0;
    while (copy > 0 && (line[copy - 1] == '\r' || line[copy - 1] == ' ')) {
        line[--copy] = 0;
    }
    remove_from_buffer(0, newline ? length + 1 : length);
    return true;
}

static bool read_command(char *line, size_t capacity) {
    while (!pop_line(line, capacity)) {
        if (!fill_input(true)) {
            return false;
        }
    }
    log_line(">>", line);
    return true;
}

static void ensure_ready(void);
static bool apply_ponder_hit(void);

// called by the search every TIME_CHECK_MASK + 1 nodes: looks at the commands that arrived. "stop" and "quit" end the search,
// "isready" is answered at once (the protocol requires that), everything else stays in the buffer for later
static bool uci_poll(void) {
    fill_input(false);
    int position = 0;
    while (position < inlen) {
        char *newline = memchr(inbuf + position, '\n', inlen - position);
        if (!newline) {
            break;
        }
        int length = (int)(newline - (inbuf + position));
        char word[32] = {0};
        for (int i = 0, w = 0; i < length && w < 31; i++) {
            if (inbuf[position + i] != '\r' && inbuf[position + i] != ' ') {
                word[w++] = inbuf[position + i];
            }
        }
        if (!strcmp(word, "stop")) {
            remove_from_buffer(position, length + 1);
            stop_received = true;
            return true;
        } else if (!strcmp(word, "quit")) {
            remove_from_buffer(position, length + 1);
            quit_requested = true;
            return true;
        } else if (!strcmp(word, "isready")) {
            remove_from_buffer(position, length + 1);
            send("readyok");
        } else if (!strcmp(word, "ponderhit")) {
            remove_from_buffer(position, length + 1);
            if (pondering && !ponder_hit && apply_ponder_hit()) {
                return true; // the book answers: end the search
            }
        } else {
            position += length + 1;
        }
    }
    if (input_closed) { // the gui has gone away
        quit_requested = true;
        return true;
    }
    return false;
}

static void move_to_text(move_t move, char *text) {
    static const char promotion[8] = {0, 'n', 'b', 'r', 'q', 0, 0, 0};
    int length = snprintf(text, 8, "%s%s", notation[m_from(move)], notation[m_to(move)]);
    if (m_promo(move)) {
        text[length++] = promotion[m_promo(move)];
        text[length] = 0;
    }
}

static int generate_legal_moves(move_t *moves) {
    u64 checkers;
    is_in_check(bitboards, &checkers);
    u64 pinned = 0;
    u8 pinners[64] = {0};
    get_pins(bitboards, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(bitboards);
    return get_moves(bitboards, moves, checkers, pinned, &pinners[0], attacks);
}

static int square_from_text(const char *text) {
    for (int sq = 0; sq < 64; sq++) {
        if (notation[sq][0] == text[0] && notation[sq][1] == text[1]) {
            return sq;
        }
    }
    return -1;
}

// plays a move given as text (e2e4, e7e8q) on the board. returns false if it is not a legal move
static bool play_move(const char *text) {
    if (strlen(text) < 4) {
        return false;
    }
    int from = square_from_text(text), to = square_from_text(text + 2);
    int promotion = 0;
    if (text[4]) {
        promotion = text[4] == 'n' ? 1 : text[4] == 'b' ? 2 : text[4] == 'r' ? 3 : text[4] == 'q' ? 4 : -1;
    }
    if (from < 0 || to < 0 || promotion < 0) {
        return false;
    }
    move_t moves[MAX_NUM_MOVES];
    int count = generate_legal_moves(moves);
    for (int i = 0; i < count; i++) {
        if ((int)m_from(moves[i]) == from && (int)m_to(moves[i]) == to && (int)m_promo(moves[i]) == promotion) {
            // the fifty move counter starts again after a pawn move or a capture
            int piece = bitboards->piece_list[from];
            if (piece == 1 || piece == -1 || bitboards->piece_list[to] != 0) {
                half_move_count = 0;
            } else {
                half_move_count++;
            }
            full_move_count++;
            struct undo undo;
            do_move(moves[i], bitboards, &undo);
            if (game_moves_played < 999) {
                game_history[game_moves_played++] = bitboards->hash;
            }
            ply = 0;
            return true;
        }
    }
    return false;
}

// position [startpos | fen <fen>] [moves <move> ...]
static void set_position(char *arguments) {
    char *save = NULL;
    char *token = strtok_r(arguments, " \t", &save);
    char board[128] = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", color[4] = "w", castle[8] = "KQkq", ep[8] = "-";
    int half = 0, full = 1;

    if (token && !strcmp(token, "fen")) {
        // the fen has up to 6 fields, the last two (the move counters) are sometimes missing
        char *fields[6] = {0};
        int n = 0;
        while ((token = strtok_r(NULL, " \t", &save)) && strcmp(token, "moves") != 0 && n < 6) {
            fields[n++] = token;
        }
        if (n < 4) {
            send("info string invalid fen");
            return;
        }
        snprintf(board, sizeof(board), "%s", fields[0]);
        snprintf(color, sizeof(color), "%s", fields[1]);
        snprintf(castle, sizeof(castle), "%s", fields[2]);
        snprintf(ep, sizeof(ep), "%s", fields[3]);
        half = n > 4 ? atoi(fields[4]) : 0;
        full = n > 5 ? atoi(fields[5]) : 1;
    } else {
        token = strtok_r(NULL, " \t", &save); // after "startpos"
    }

    memset(bitboards, 0, sizeof(*bitboards));
    bitboards->en_passant_square = -1; // (fen_to_position does not clear the board, "no en passant square" is -1)
    fen_to_position(board, bitboards);
    init_boards(bitboards, color[0] == 'w', castle, ep, half, full);
    game_moves_played = 0;
    game_history[game_moves_played++] = bitboards->hash;
    ply = 0;

    if (token && !strcmp(token, "moves")) {
        while ((token = strtok_r(NULL, " \t", &save))) {
            if (!play_move(token)) {
                send("info string illegal move %s", token);
                break;
            }
        }
    }
}

// what was reported last, so that the final answer can be made consistent with it
static bool info_reported;
static int info_depth, info_score;
static move_t info_first_move;
static move_t info_pv[MAX_PLY]; // the last reported pv: its second move is the move to ponder on
static int info_pv_len;
static bool info_since_ponder_hit; // an iteration line was sent after the ponderhit (see the end of go_command)

// called by the search after every completed iteration. score is from the point of view of the side to move
static void uci_iteration(int depth, int score, const move_t *pv, int pv_length) {
    double ms = search_elapsed_ms();
    long long total = search_total_nodes();
    char score_text[32];
    info_reported = true;
    info_since_ponder_hit = true;
    info_depth = depth;
    info_score = score;
    info_first_move = pv_length > 0 ? pv[0] : 0;
    info_pv_len = pv_length < MAX_PLY ? pv_length : MAX_PLY;
    for (int i = 0; i < info_pv_len; i++) {
        info_pv[i] = pv[i];
    }
    if (abs(score) >= MATE_BOUND) {
        int plies = INF - abs(score);
        int moves = (plies + 1) / 2;
        snprintf(score_text, sizeof(score_text), "mate %d", score > 0 ? moves : -moves);
    } else {
        // the engine's pawn is worth 90, the protocol wants centipawns
        snprintf(score_text, sizeof(score_text), "cp %d", score * 100 / 90);
    }
    char line[1024];
    // after a ponderhit the time starts again, so the nodes searched while pondering must not count for nps
    long long fresh = total - ponder_node_base;
    int length = snprintf(line, sizeof(line), "info depth %d score %s nodes %lld nps %lld time %d pv", depth, score_text, total,
                          ms > 0 ? (long long)(fresh * 1000.0 / ms) : 0LL, (int)ms);
    for (int i = 0; i < pv_length && length < (int)sizeof(line) - 12; i++) {
        char text[8];
        move_to_text(pv[i], text);
        length += snprintf(line + length, sizeof(line) - length, " %s", text);
    }
    send("%s", line);
}

static void load_book_if_needed(void) {
    if (!own_book || book_loaded) {
        return;
    }
    FILE *f = fopen(book_file, "rb");
    if (!f) {
        send("info string opening book %s not found, playing without it", book_file);
        own_book = false;
        return;
    }
    fclose(f);
    init_book(book_file); // (this function ends the program if the file is not readable)
    book_loaded = true;
}

// the tables are cleared (that also hands out all of their memory now, instead of during a search where the
// operating system would need part of the thinking time for it), the book is loaded
static void ensure_ready(void) {
    if (!tables_ready) {
        clear_tables();
        tables_ready = true;
    }
    load_book_if_needed();
}

static void resize_tables(int requested_mb) {
    int mb = 1;
    while (mb * 2 <= requested_mb && mb < 16384) {
        mb *= 2;
    }
    free_tables();
    init_tables(mb);
    hash_mb = mb;
    tables_ready = false;
}

static void set_option(char *arguments) {
    // setoption name <name, may contain spaces> [value <value>]
    char name[64] = {0}, value[256] = {0};
    char *value_part = strstr(arguments, " value ");
    char *name_part = strstr(arguments, "name ");
    if (!name_part) {
        return;
    }
    name_part += 5;
    size_t name_length = value_part ? (size_t)(value_part - name_part) : strlen(name_part);
    if (name_length >= sizeof(name)) name_length = sizeof(name) - 1;
    memcpy(name, name_part, name_length);
    if (value_part) {
        snprintf(value, sizeof(value), "%s", value_part + 7);
    }
    if (!strcasecmp(name, "Hash")) {
        int requested = atoi(value);
        if (requested >= 1) resize_tables(requested > 16384 ? 16384 : requested);
    } else if (!strcasecmp(name, "Clear Hash")) {
        tables_ready = false;
    } else if (!strcasecmp(name, "Move Overhead")) {
        move_overhead_ms = atoi(value) < 0 ? 0 : atoi(value);
    } else if (!strcasecmp(name, "Ponder")) {
        ponder_option = !strcasecmp(value, "true");
    } else if (!strcasecmp(name, "OwnBook")) {
        own_book = !strcasecmp(value, "true");
    } else if (!strcasecmp(name, "DebugLog")) {
        if (debug_log) {
            fclose(debug_log);
            debug_log = NULL;
        }
        if (value[0] && strcasecmp(value, "<empty>")) {
            debug_log = fopen(value, "a");
        }
    } else if (!strcasecmp(name, "BookMinGames")) {
        book_min_games = atoi(value) < 1 ? 1 : atoi(value);
    } else if (!strcasecmp(name, "BookRandom")) {
        book_random_option = !strcasecmp(value, "true");
    } else if (!strcasecmp(name, "BookFile")) {
        snprintf(book_file, sizeof(book_file), "%s", value);
        book_loaded = false;
    }
    else if (!strcasecmp(name, "Threads")) {
        int n = atoi(value);
        search_threads = n < 1 ? 1 : (n > 512 ? 512 : n);
    }
    else if (!strcasecmp(name, "DeepestThread")) {
        deepest_thread = !strcasecmp(value, "true") ? 1 : 0;
    }
#ifdef TUNE_SEARCH
    else if (tune_set_option(name, atoi(value))) {
        // a search parameter that is being tuned
    }
#endif
    // unknown options are ignored
}

struct go_parameters {
    long long wtime, btime, winc, binc, movetime, nodes;
    int movestogo, depth;
    bool infinite, perft, ponder;
};

// how much time to spend on this move. hard_ms is a limit the search never goes beyond, soft_ms is the time that is planned:
// no new iteration is started when more than half of it is gone
static void allocate_time(const struct go_parameters *go, bool white, double *hard_ms, double *soft_ms) {
    *hard_ms = 0;
    *soft_ms = 0; // 0: no limit
    if (go->movetime >= 0) {
        *hard_ms = go->movetime - move_overhead_ms;
        if (*hard_ms < 1) *hard_ms = 1;
        return;
    }
    long long remaining = white ? go->wtime : go->btime;
    long long increment = white ? go->winc : go->binc;
    if (remaining < 0) {
        return; // no clock information: infinite, depth or nodes search
    }
    double available = (double)remaining - move_overhead_ms;
    if (available < 1) available = 1;
    int moves_to_go = go->movestogo > 0 ? (go->movestogo < 40 ? go->movestogo : 40) : 30;
    double base = available / moves_to_go + increment * 0.75;
    const double soft_cap = TIME_SOFT_CAP_X100 / 100.0, hard_mult = TIME_HARD_MULT_X100 / 100.0, hard_cap = TIME_HARD_CAP_X100 / 100.0;
    *soft_ms = base < available * soft_cap ? base : available * soft_cap;
    *hard_ms = base * hard_mult < available * hard_cap ? base * hard_mult : available * hard_cap;
    if (*hard_ms < 1) *hard_ms = 1;
    if (*soft_ms > *hard_ms) *soft_ms = *hard_ms;
}

// the clock parameters of the running "go ponder" and the side to move at its root (the board itself is changed by the search,
// uci_poll must not look at it)
static struct go_parameters ponder_go;
static bool ponder_white;

// "ponderhit": the opponent played the move we were pondering on. the time that has passed since "go ponder" was the opponent's, so the
// clock of this move starts now; the search goes on without a restart (the tables and the history are warm) with the limits of a normal
// search. returns true if the search has to end because the book has a move for this position. called from uci_poll, i.e. from the search
static bool apply_ponder_hit(void) {
    ponder_hit = true;
    info_since_ponder_hit = false;
    clock_gettime(CLOCK_MONOTONIC, &wall_start);
    double hard_ms, soft_ms;
    allocate_time(&ponder_go, ponder_white, &hard_ms, &soft_ms);
    search_set_limits(soft_ms, hard_ms);
    ponder_node_base = search_total_nodes();
    return ponder_book_found;
}

// the move to ponder on after our move: the second move of the last pv, or else the table move of the position after our move.
// only a legal reply is returned
static bool find_ponder_move(move_t best, move_t *reply) {
    move_t candidate = 0;
    if (info_pv_len >= 2 && info_pv[0] == best) {
        candidate = info_pv[1];
    }
    struct undo undo;
    do_move(best, bitboards, &undo);
    if (!candidate) {
        struct hash_entry e;
        if (table_probe(trans_table, bitboards->hash, 0, &e)) {
            candidate = e.best_move;
        }
    }
    bool found = false;
    if (candidate) {
        move_t moves[MAX_NUM_MOVES];
        int count = generate_legal_moves(moves);
        for (int i = 0; i < count; i++) {
            if (m_from(moves[i]) == m_from(candidate) && m_to(moves[i]) == m_to(candidate) && m_promo(moves[i]) == m_promo(candidate)) {
                *reply = moves[i];
                found = true;
                break;
            }
        }
    }
    undo_move(best, bitboards, &undo);
    return found;
}

// waits for "stop" (an infinite search is answered only after it) or, when the search was a ponder search that ended by itself,
// also for "ponderhit"
static void wait_for_stop(bool accept_ponder_hit) {
    char line[256];
    while (!stop_received && !quit_requested) {
        if (!read_command(line, sizeof(line))) {
            quit_requested = true;
            return;
        }
        if (!strcmp(line, "stop")) return;
        if (accept_ponder_hit && !strcmp(line, "ponderhit")) {
            ponder_hit = true;
            info_since_ponder_hit = false;
            return;
        }
        if (!strcmp(line, "quit")) quit_requested = true;
        if (!strcmp(line, "isready")) send("readyok");
    }
}

static void clear_search_hooks(void) {
    use_wall_clock = false;
    stop_poll = NULL;
    iteration_hook = NULL;
    soft_time_ms = 0;
    max_nodes = 0;
}

static void go_command(char *arguments) {
    struct go_parameters go = {-1, -1, 0, 0, -1, 0, 0, 0, false, false, false};
    char *save = NULL;
    for (char *t = strtok_r(arguments, " \t", &save); t; t = strtok_r(NULL, " \t", &save)) {
        if (!strcmp(t, "infinite")) go.infinite = true;
        else if (!strcmp(t, "ponder")) go.ponder = true;
        else if (!strcmp(t, "perft")) { go.perft = true; t = strtok_r(NULL, " \t", &save); go.depth = t ? atoi(t) : 1; }
        else if (!strcmp(t, "wtime")) { t = strtok_r(NULL, " \t", &save); go.wtime = t ? atoll(t) : -1; }
        else if (!strcmp(t, "btime")) { t = strtok_r(NULL, " \t", &save); go.btime = t ? atoll(t) : -1; }
        else if (!strcmp(t, "winc")) { t = strtok_r(NULL, " \t", &save); go.winc = t ? atoll(t) : 0; }
        else if (!strcmp(t, "binc")) { t = strtok_r(NULL, " \t", &save); go.binc = t ? atoll(t) : 0; }
        else if (!strcmp(t, "movetime")) { t = strtok_r(NULL, " \t", &save); go.movetime = t ? atoll(t) : -1; }
        else if (!strcmp(t, "movestogo")) { t = strtok_r(NULL, " \t", &save); go.movestogo = t ? atoi(t) : 0; }
        else if (!strcmp(t, "depth")) { t = strtok_r(NULL, " \t", &save); go.depth = t ? atoi(t) : 0; }
        else if (!strcmp(t, "nodes")) { t = strtok_r(NULL, " \t", &save); go.nodes = t ? atoll(t) : 0; }
        // searchmoves and unknown parameters are ignored
    }
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started); // the time of the search starts when the command arrives

    ensure_ready();

    if (go.perft) {
        u64 total = perft(go.depth, bitboards, go.depth);
        send("\nNodes searched: %llu\n", (unsigned long long)total);
        return;
    }

    move_t moves[MAX_NUM_MOVES];
    int count = generate_legal_moves(moves);
    if (count == 0) {
        send("bestmove 0000");
        return;
    }

    char text[8];
    const bool ponder = go.ponder;
    if (debug_log) {
        send("info string book state: OwnBook=%d loaded=%d gameMovesPlayed=%d hash=%llu weight=%d", own_book, book_loaded,
             game_moves_played, (unsigned long long)book_hash(bitboards), book_loaded ? book_position_weight(book_hash(bitboards)) : -1);
    }
    if (own_book && book_loaded) {
        use_book = true;
        book_min_count = book_min_games;
        book_random = book_random_option;
        move_t book = get_book_move(book_hash(bitboards));
        use_book = false;
        if (book && debug_log) {
            send("info string book raw move=%u from=%d to=%d promo=%d", book, (int)m_from(book), (int)m_to(book), (int)m_promo(book));
        }
        for (int i = 0; book && i < count; i++) {
            // compare from/to/promotion only: the flag bits (castle, double push) may differ in an older book file
            if (m_from(moves[i]) == m_from(book) && m_to(moves[i]) == m_to(book) && m_promo(moves[i]) == m_promo(book)) {
                book = moves[i];
                move_to_text(book, text);
                if (ponder) {
                    // the position is the one after the move we expect: no answer now, but a ponderhit is answered with this move
                    ponder_book = book;
                    ponder_book_found = true;
                    break;
                }
                send("info string book move");
                send("bestmove %s", text);
                return;
            }
        }

        if (!ponder) {
            send("info string no book move for this position");
        }
    }

    bool white = bitboards->color == WHITE;
    double hard_ms, soft_ms;
    allocate_time(&go, white, &hard_ms, &soft_ms);
    pondering = ponder;
    ponder_hit = false;
    ponder_node_base = 0;
    if (ponder) {
        // search without time limits until the ponderhit (apply_ponder_hit) gives it the limits of a normal search
        ponder_go = go;
        ponder_white = white;
        hard_ms = 0;
        soft_ms = 0;
    } else {
        ponder_book_found = false;
    }

    reset_search();
    wall_start = started;
    use_wall_clock = true;
    stop_poll = uci_poll;
    iteration_hook = uci_iteration;
    soft_time_ms = soft_ms;
    max_time = hard_ms > 0 ? hard_ms / 1000.0 : 1e9;
    max_nodes = go.nodes;
    max_depth = go.depth > 0 && go.depth < 100 ? go.depth : 100;
    stop_received = false;

    u64 checkers;
    is_in_check(bitboards, &checkers);
    info_reported = false;
    move_t best = iter_deepening(moves, bitboards, count);
    if (ponder && ponder_hit && ponder_book_found) {
        // ponderhit and the book has a move for this position: that move, the search result is not needed
        clear_search_hooks();
        move_to_text(ponder_book, text);
        send("info string book move");
        send("bestmove %s", text);
        pondering = ponder_hit = ponder_book_found = false;
        return;
    }
    // a forced move is returned without any iteration, and a search stopped in the middle of an iteration may have
    // changed its mind after the last report: in both cases say so, so that the last PV starts with the move played
    if (!info_reported || info_first_move != best) {
        move_t one[1] = { best };
        uci_iteration(info_reported ? info_depth : 1, info_reported ? info_score : 0, one, 1);
    }

    // the totals of the whole search (the last iteration that was reported may have been stopped early)
    double used = search_elapsed_ms();
    long long total = search_total_nodes();
    send("info nodes %lld nps %lld time %d", total, used > 0 ? (long long)((total - ponder_node_base) * 1000.0 / used) : 0LL, (int)used);

    clear_search_hooks();

    // in an infinite search the protocol wants the answer only after "stop", and in a ponder search that has not got its
    // ponderhit yet as well ("ponderhit" gives the answer too)
    if ((go.infinite || (ponder && !ponder_hit)) && !stop_received && !quit_requested) {
        wait_for_stop(ponder);
    }
    if (ponder && ponder_hit && ponder_book_found) {
        move_to_text(ponder_book, text);
        send("info string book move");
        send("bestmove %s", text);
        pondering = ponder_hit = ponder_book_found = false;
        return;
    }
    // a GUI that answers a ponder search with "ponderhit" (python-chess / lichess-bot) only keeps the info lines that arrive after the
    // ponderhit. when the search ends before another iteration is finished, those would be only the totals above, and the depth, score
    // and pv of the move would be missing: send the last report again (same depth, score and pv)
    if (ponder_hit && !info_since_ponder_hit && info_reported) {
        uci_iteration(info_depth, info_score, info_pv, info_pv_len);
    }
    pondering = ponder_hit = ponder_book_found = false;
    move_to_text(best, text);
    move_t reply;
    if (ponder_option && find_ponder_move(best, &reply)) {
        char reply_text[8];
        move_to_text(reply, reply_text);
        send("bestmove %s ponder %s", text, reply_text);
    } else {
        send("bestmove %s", text);
    }
}

int uci_main(void) {
    // the output of the engine for the other modes is not wanted here: stdout goes to /dev/null, the protocol
    // messages are written to a copy of the original standard output
    fflush(stdout);
    out = fdopen(dup(STDOUT_FILENO), "w");
    if (!freopen("/dev/null", "w", stdout)) {
        return 1;
    }
    srand((unsigned)time(NULL)); // the opening book chooses randomly

    resize_tables(hash_mb);

    char line[8192];
    while (!quit_requested && read_command(line, sizeof(line))) {
        char *rest = line;
        while (*rest == ' ') rest++;
        char *space = strchr(rest, ' ');
        char command[32] = {0};
        size_t length = space ? (size_t)(space - rest) : strlen(rest);
        if (length > 31) length = 31;
        memcpy(command, rest, length);
        char *arguments = space ? space + 1 : rest + strlen(rest);

        if (!strcmp(command, "uci")) {
            send("id name %s", ENGINE_NAME);
            send("id author %s", ENGINE_AUTHOR);
            send("option name Hash type spin default %d min 1 max 16384", 128);
            send("option name Clear Hash type button");
            send("option name Threads type spin default 1 min 1 max 512");
            send("option name DeepestThread type check default true");
            send("option name Move Overhead type spin default %d min 0 max 5000", 50);
            send("option name Ponder type check default false");
            send("option name OwnBook type check default false");
            send("option name DebugLog type string default <empty>");
            send("option name BookMinGames type spin default 20 min 1 max 100000");
            send("option name BookRandom type check default false");
            send("option name BookFile type string default %s", "bookMax.dat");
#ifdef TUNE_SEARCH
            for (int i = 0; i < tune_count(); i++) {
                const struct tune_param *p = tune_at(i);
                send("option name %s type spin default %d min %d max %d", p->name, p->def, p->min, p->max);
            }
#endif
            send("uciok");
        } else if (!strcmp(command, "isready")) {
            ensure_ready();
            send("readyok");
        } else if (!strcmp(command, "setoption")) {
            set_option(arguments);
        } else if (!strcmp(command, "ucinewgame")) {
            tables_ready = false;
            ensure_ready();
            game_moves_played = 0;
            clear_history();
        } else if (!strcmp(command, "position")) {
            set_position(arguments);
        } else if (!strcmp(command, "go")) {
            go_command(arguments);
        } else if (!strcmp(command, "eval")) {
            send("info string static evaluation %d centipawns for the side to move", evaluate(bitboards) * 100 / 90);
        } else if (!strcmp(command, "quit")) {
            break;
        }
        // "stop" outside of a search and unknown commands are ignored
    }
    return 0;
}
