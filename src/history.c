#include "history.h"

_Thread_local move_t killers[MAX_KILLER_PLY][2];
_Thread_local int history[2][64][64][HIST_N];
_Thread_local int16_t corr_pawn[2][CH_SIZE];

// forget everything that was learned about move quality (new game)
void clear_history(void) {
    memset(history, 0, sizeof(history));
    memset(corr_pawn, 0, sizeof(corr_pawn));
}

void quiet_cutoff(int side, int depth, move_t move, const move_t *tried, int num_tried, u64 attacks) {
    if (ply < MAX_KILLER_PLY && killers[ply][0] != move) {
        killers[ply][1] = killers[ply][0];
        killers[ply][0] = move;
    }
    int bonus = depth * depth * HIST_BONUS_MUL / 16;
    if (bonus > HIST_BONUS_CAP) bonus = HIST_BONUS_CAP;
    update_history(side, move, bonus, attacks);
    for (int i = 0; i < num_tried; i++) {
        update_history(side, tried[i], -bonus, attacks);
    }
}

// start of a search: killers are forgotten, the history keeps what was learned in the earlier moves of this game but lets it fade
// (it is cleared by ucinewgame)
void history_new_search(void) {
    memset(killers, 0, sizeof(killers));
    int *hist = &history[0][0][0][0];
    for (int k = 0; k < 2 * 64 * 64 * HIST_N; k++)
        hist[k] /= 2;
}
