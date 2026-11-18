// test of the staged move generation: walks the game tree of a few positions and checks at every node that
//  - tactical and quiet moves are disjoint and together exactly the legal moves (get_moves),
//  - tactical moves are exactly the legal moves that is_tactical() calls tactical,
//  - get_moves_from(square) gives exactly the legal moves of that square (for all 64 squares),
//  - random garbage moves are only accepted by get_moves_from if they are legal (what a corrupt table entry would be)
// build: see tools/README.md (links all engine files except main.c): gcc -O2 -I. tools/staged_test.c $SRC_NOMAIN -lm -pthread
#include "main.h"
#include "movepick.h"
#include <stdlib.h>

static long long nodes_checked, garbage_checked;
static unsigned long long rng = 88172645463325252ULL;
static unsigned rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng >> 11); }

static int cmp_u(const void *a, const void *b) { move_t x = *(const move_t *)a, y = *(const move_t *)b; return x < y ? -1 : x > y; }
static bool contains(const move_t *m, int n, move_t x) { for (int i = 0; i < n; i++) if (m[i] == x) return true; return false; }
#define FAIL(...) do { fprintf(stderr, "FAIL: " __VA_ARGS__); fprintf(stderr, "\n"); print_board(bitboards); exit(1); } while (0)

static void check_node(struct bitboards *BB) {
    move_t all[MAX_NUM_MOVES], tac[MAX_NUM_MOVES], qui[MAX_NUM_MOVES], one[MAX_NUM_MOVES], merged[2 * MAX_NUM_MOVES];
    u64 checkers; u64 pinned = 0; u8 pinners[64] = {0};
    is_in_check(BB, &checkers);
    get_pins(BB, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(BB);
    int na = get_moves(BB, all, checkers, pinned, pinners, attacks);
    int nt = get_tactical(BB, tac, checkers, pinned, pinners, attacks);
    int nq = get_quiets(BB, qui, checkers, pinned, pinners, attacks);
    if (nt + nq != na) FAIL("counts: tactical %d + quiet %d != all %d", nt, nq, na);
    memcpy(merged, tac, nt * sizeof(move_t)); memcpy(merged + nt, qui, nq * sizeof(move_t));
    qsort(merged, na, sizeof(move_t), cmp_u);
    move_t sorted[MAX_NUM_MOVES]; memcpy(sorted, all, na * sizeof(move_t)); qsort(sorted, na, sizeof(move_t), cmp_u);
    for (int i = 0; i < na; i++) {
        if (merged[i] != sorted[i]) FAIL("tactical + quiet differ from all at %d", i);
        if (i && sorted[i] == sorted[i - 1]) FAIL("duplicate move in get_moves");
    }
    for (int i = 0; i < nt; i++) if (!is_tactical(BB, tac[i])) FAIL("tactical list has a quiet move %d-%d", m_from(tac[i]), m_to(tac[i]));
    for (int i = 0; i < nq; i++) if (is_tactical(BB, qui[i])) FAIL("quiet list has a tactical move %d-%d", m_from(qui[i]), m_to(qui[i]));
    for (int f = 0; f < 64; f++) {
        int n1 = get_moves_from(BB, one, checkers, pinned, pinners, attacks, f), expect = 0;
        for (int i = 0; i < na; i++) if (m_from(all[i]) == f) { expect++; if (!contains(one, n1, all[i])) FAIL("from %d misses a legal move", f); }
        if (n1 != expect) FAIL("from %d gives %d moves, expected %d", f, n1, expect);
    }
    for (int k = 0; k < 40; k++) {
        move_t g = rnd() & 0x3FFFF; // any bit pattern of a move (also impossible ones)
        if (k < 20 && na > 0) { move_t base = all[rnd() % na]; g = base ^ (1u << (rnd() % 18)); } // a legal move with one bit changed
        int n1 = get_moves_from(BB, one, checkers, pinned, pinners, attacks, m_from(g));
        if (contains(one, n1, g) != contains(all, na, g)) FAIL("garbage move %x accepted/rejected wrongly", g);
        garbage_checked++;
    }
    nodes_checked++;
}

static void walk(struct bitboards *BB, int depth) {
    check_node(BB);
    if (depth == 0) return;
    move_t all[MAX_NUM_MOVES]; u64 checkers; u64 pinned = 0; u8 pinners[64] = {0};
    is_in_check(BB, &checkers); get_pins(BB, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(BB);
    int na = get_moves(BB, all, checkers, pinned, pinners, attacks);
    for (int i = 0; i < na; i++) {
        struct undo u;
        do_move(all[i], BB, &u);
        walk(BB, depth - 1);
        undo_move(all[i], BB, &u);
    }
}

int main(int argc, char *argv[]) {
    init_king_attacks(); init_knight_attacks(); init_sliding_piece_attacks(true); init_sliding_piece_attacks(false);
    init_squares_between(); init_zobrist(); init_tables(16); allocate_boards();
    const char *fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
        "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
        "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
        "8/P7/8/8/8/8/7k/K7 w - - 0 1", "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
        "8/8/8/8/k2Pp2Q/8/8/3K4 b - d3 0 1", "4k3/8/8/KPp4r/8/8/8/8 w - c6 0 1",
        "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1",
        "3k4/3pp3/8/8/8/8/3PP3/3K1R2 w - - 0 1", "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    };
    int depth = argc > 1 ? atoi(argv[1]) : 3;
    for (size_t i = 0; i < sizeof fens / sizeof fens[0]; i++) {
        char board[128], color[4], castle[8], ep[8]; int half, full;
        if (sscanf(fens[i], "%127s %3s %7s %7s %d %d", board, color, castle, ep, &half, &full) != 6) { fprintf(stderr, "bad fen %zu\n", i); return 1; }
        memset(bitboards, 0, sizeof(*bitboards));
        bitboards->en_passant_square = -1;
        fen_to_position(board, bitboards);
        init_boards(bitboards, strcmp(color, "w") == 0, castle, ep, half, full);
        walk(bitboards, depth);
    }
    printf("staged generation OK: %lld nodes checked, %lld garbage moves tested\n", nodes_checked, garbage_checked);
    return 0;
}
