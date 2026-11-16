// test harness: reads FEN lines from stdin, prints the engine's legal moves (M) and quiescence captures (C)
#include "main.h"
static const char *promo_ch[8] = {"", "n", "b", "r", "q", "", "", ""};
static void print_moves(const char *tag, move_t *m, int n) {
    printf("%s", tag);
    for (int i = 0; i < n; i++) printf(" %s%s%s", notation[m_from(m[i])], notation[m_to(m[i])], promo_ch[m_promo(m[i])]);
    printf("\n");
}
int main(int argc, char *argv[]) {
    init_king_attacks(); init_knight_attacks(); init_sliding_piece_attacks(true); init_sliding_piece_attacks(false);
    init_squares_between(); init_zobrist(); init_tables(16); allocate_boards();
    char line[512];
    while (fgets(line, sizeof line, stdin)) {
        char board[128], color[4], castle[8], ep[8]; int half, full;
        if (sscanf(line, "%127s %3s %7s %7s %d %d", board, color, castle, ep, &half, &full) != 6) continue;
        memset(bitboards, 0, sizeof(*bitboards));
        bitboards->en_passant_square = -1;
        fen_to_position(board, bitboards);
        init_boards(bitboards, strcmp(color, "w") == 0, castle, ep, half, full);
        move_t moves[MAX_NUM_MOVES]; u64 checkers; u64 pinned = 0; u8 pinners[64] = {0};
        is_in_check(bitboards, &checkers);
        get_pins(bitboards, &pinned, &pinners[0]);
        u64 attacks = get_enemy_attack_mask(bitboards);
        int n = get_moves(bitboards, moves, checkers, pinned, &pinners[0], attacks);
        print_moves("M", moves, n);
        pinned = 0; memset(pinners, 0, sizeof pinners);
        is_in_check(bitboards, &checkers);
        get_pins(bitboards, &pinned, &pinners[0]);
        attacks = get_enemy_attack_mask(bitboards);
        n = get_captures(bitboards, moves, checkers, pinned, &pinners[0], attacks);
        print_moves("C", moves, n);
        printf("E %d\n", evaluate(bitboards));
        printf("A");
        for (int sq = 0; sq < 64; sq++) printf(" %llx", (unsigned long long)attackers_to(bitboards, sq, bitboards->bits[all_pieces]));
        printf("\n");
        { // SEE of every legal capture that is neither en passant nor a promotion
            move_t all[MAX_NUM_MOVES]; u64 ch; u64 pn = 0; u8 pr[64] = {0};
            is_in_check(bitboards, &ch); get_pins(bitboards, &pn, &pr[0]);
            u64 at = get_enemy_attack_mask(bitboards);
            int na = get_moves(bitboards, all, ch, pn, &pr[0], at);
            printf("S");
            for (int i = 0; i < na; i++) {
                if (bitboards->piece_list[m_to(all[i])] != 0 && m_promo(all[i]) == 0)
                    printf(" %s%s=%d", notation[m_from(all[i])], notation[m_to(all[i])], see(bitboards, all[i]));
            }
            printf("\n");
        }
        fflush(stdout);
    }
    return 0;
}
