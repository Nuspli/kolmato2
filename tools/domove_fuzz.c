// random playouts through do_move / undo_move: prints one digest of the complete board state after every move
// (bitboards, piece list, castling rights, en passant square, hash, position and material scores, color, static evaluation), and checks that
// undo_move restores the digest of every earlier position. run it for two builds (e.g. before and after a refactoring of
// move.c / move_generation.c) and compare the output: the two must be identical, and "undo mismatch" must not appear.
// usage: domove_fuzz [games] [seed]    built like tools/movetest.c: all sources except main.c
#include "main.h"
static unsigned long long rng_state;
static unsigned rnd(void) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17; return (unsigned)(rng_state >> 11); }
static u64 mix(u64 h, u64 v) { h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2); return h * 0xFF51AFD7ED558CCDULL; }
static u64 digest(const struct bitboards *B) {
    u64 h = 1;
    for (int i = 0; i < 15; i++) h = mix(h, B->bits[i]);
    for (int i = 0; i < 64; i++) h = mix(h, (u64)(B->piece_list[i] + 100));
    h = mix(h, B->white_castle_king_side | B->white_castle_queen_side << 1 | B->black_castle_king_side << 2 | B->black_castle_queen_side << 3);
    h = mix(h, (u64)(B->en_passant_square + 100)); h = mix(h, B->hash); h = mix(h, (u64)(B->color + 5));
    h = mix(h, (u64)(unsigned)B->white_position_opening); h = mix(h, (u64)(unsigned)B->black_position_opening);
    h = mix(h, (u64)(unsigned)B->white_position_endgame); h = mix(h, (u64)(unsigned)B->black_position_endgame);
    h = mix(h, (u64)(unsigned)B->white_material); h = mix(h, (u64)(unsigned)B->black_material);
    h = mix(h, (u64)(unsigned)evaluate((struct bitboards *)B)); // the static evaluation, too
    return h;
}
static const char *STARTS[][4] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", "w", "KQkq", "-"},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R", "w", "KQkq", "-"},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8", "w", "-", "-"},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1", "w", "kq", "-"},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R", "w", "KQ", "-"},
    {"r3k2r/8/8/8/8/8/8/R3K2R", "w", "KQkq", "-"},
    {"r3k2r/8/8/8/8/8/8/R3K2R", "b", "KQkq", "-"},
    {"4k3/PPPPPPPP/8/8/8/8/pppppppp/4K3", "w", "-", "-"},
    {"4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3", "b", "-", "-"},
    {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R", "w", "KQkq", "-"},
};
int main(int argc, char *argv[]) {
    int games = argc > 1 ? atoi(argv[1]) : 2000;
    rng_state = argc > 2 ? strtoull(argv[2], 0, 10) * 2654435761ULL + 88172645463325252ULL : 88172645463325252ULL;
    init_king_attacks(); init_knight_attacks(); init_sliding_piece_attacks(true); init_sliding_piece_attacks(false);
    init_squares_between(); init_zobrist(); init_tables(16); allocate_boards();
    int bad = 0; long plies = 0, castles = 0, eps = 0, promos = 0, caps = 0;
    for (int g = 0; g < games; g++) {
        const char **s = STARTS[g % (int)(sizeof STARTS / sizeof STARTS[0])];
        char board[128]; strcpy(board, s[0]); char castle[8]; strcpy(castle, s[2]); char ep[8]; strcpy(ep, s[3]);
        memset(bitboards, 0, sizeof(*bitboards)); bitboards->en_passant_square = -1;
        fen_to_position(board, bitboards);
        init_boards(bitboards, strcmp(s[1], "w") == 0, castle, ep, 0, 1);
        u64 digests[400]; move_t played[400]; struct undo undos[400]; int n = 0;
        digests[0] = digest(bitboards);
        printf("G%d %llx\n", g, (unsigned long long)digests[0]);
        for (; n < 150; n++) {
            move_t moves[MAX_NUM_MOVES]; u64 checkers, pinned = 0; u8 pinners[64] = {0};
            is_in_check(bitboards, &checkers);
            get_pins(bitboards, &pinned, &pinners[0]);
            u64 attacks = get_enemy_attack_mask(bitboards);
            int cnt = get_moves(bitboards, moves, checkers, pinned, &pinners[0], attacks);
            if (cnt == 0) break;
            move_t m = moves[rnd() % cnt];
            if (m_castle(m)) castles++;
            if (m_promo(m)) promos++;
            if (bitboards->piece_list[m_to(m)] != 0) caps++;
            if (m_to(m) == bitboards->en_passant_square && abs(bitboards->piece_list[m_from(m)]) == 1) eps++;
            played[n] = m;
            do_move(m, bitboards, &undos[n]);
            digests[n + 1] = digest(bitboards);
            printf("%llx\n", (unsigned long long)digests[n + 1]);
            plies++;
        }
        for (int i = n - 1; i >= 0; i--) {
            undo_move(played[i], bitboards, &undos[i]);
            if (digest(bitboards) != digests[i]) { printf("undo mismatch game %d ply %d\n", g, i); bad++; break; }
        }
    }
    printf("plies %ld castles %ld ep %ld promos %ld captures %ld undo mismatches %d\n", plies, castles, eps, promos, caps, bad);
    return bad != 0;
}
