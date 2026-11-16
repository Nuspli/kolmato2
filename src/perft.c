#include "search.h"
#include "movepick.h"

// perft for testing the move generator (console option and uci "go perft")
u64 perft(int depth, struct bitboards *BITBOARDS, int original_depth) {
    // perft function for debugging

    if (depth == 0) {
        return 1;
    }

    move_t possible[MAX_NUM_MOVES];
    int num_moves = 0;

    u64 checkers;
    is_in_check(BITBOARDS, &checkers);
    u64 pinned = 0;
    u8 pinners[64] = {0};
    get_pins(BITBOARDS, &pinned, &pinners[0]);
    u64 attacks = get_enemy_attack_mask(BITBOARDS);
    num_moves = get_moves(BITBOARDS, &possible[0], checkers, pinned, &pinners[0], attacks);
    
    // bulk counting: at the last ply the number of legal moves is the number of leaf nodes, so there is no need to make
    // and unmake all of them. (not at the root, so that depth 1 still prints the moves)
#if !VERIFY_GIVESCHECK
    if (depth == 1 && original_depth != 1) {
        return num_moves;
    }
#endif

    u64 nodes = 0;

    for (int i = 0; i < num_moves; i++) {

        struct undo undo;
#if VERIFY_GIVESCHECK
        const bool verify_gives_check = gives_check(BITBOARDS, possible[i]);
#endif
        do_move(possible[i], BITBOARDS, &undo);
#if VERIFY_GIVESCHECK
        if (verify_gives_check != is_in_check_light(BITBOARDS)) {
            fprintf(stderr, "GIVESCHECK MISMATCH (perft): move %d->%d promo %d castle %d predicted %d\n", m_from(possible[i]), m_to(possible[i]), (int)m_promo(possible[i]), (int)m_castle(possible[i]), (int)verify_gives_check);
            abort();
        }
#endif

        u64 nodesbefore = nodes;
        nodes += perft(depth-1, BITBOARDS, original_depth);
        undo_move(possible[i], BITBOARDS, &undo);


        if (depth == original_depth && nodes != nodesbefore) {
            printf("%s%s:", notation[m_from(possible[i])], notation[m_to(possible[i])]);
            printf(" %llu\n", (unsigned long long)(nodes-nodesbefore));
        }
        
    }

    return nodes;
}
