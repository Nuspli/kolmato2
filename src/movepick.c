#include "movepick.h"

// ply_index < 0 means there are no quiet moves to order (quiescence search)
void score_moves(const move_t *moves, int *scores, int num_moves, const struct bitboards *BB, move_t table_move, int ply_index, u64 attacks) {

    const int side = BB->color == WHITE ? 0 : 1;
    const int flip = BB->color == WHITE ? 0 : 56;
    // the quiet moves are ordered with the piece square tables, weighted with the game phase. the phase is only worked out when the first quiet move
    // is scored: the quiescence search (ply_index < 0) and nodes where only the table move, captures and killers are scored never need it
    float w = -1.0f;
    const bool use_killers = ply_index >= 0 && ply_index < MAX_KILLER_PLY;
    const move_t killer1 = use_killers ? killers[ply_index][0] : 0;
    const move_t killer2 = use_killers ? killers[ply_index][1] : 0;

    for (int i = 0; i < num_moves; i++) {

        move_t move = moves[i];

        if (move == table_move) {
            scores[i] = SCORE_TABLE_MOVE;
            continue;
        }

        int f = m_from(move);
        int t = m_to(move);
        int piece = BB->piece_list[f];
        int mover_type = (piece < 0 ? -piece : piece) - 1;
        int victim = BB->piece_list[t];
        int victim_value = 0;
        bool capture = false;

        if (victim) {
            victim_value = material_values[(victim < 0 ? -victim : victim) - 1];
            capture = true;
        } else if (mover_type == 0 && t == BB->en_passant_square) {
            victim_value = PAWNVALUE;
            capture = true;
        }

        int promo = m_promo(move);

        if (capture || promo) {
            int attacker_value = material_values[mover_type];
            // most valuable victim first, least valuable attacker first (the victim counts 16 times as much)
            int score = 16 * victim_value - attacker_value;
            if (promo) {
                score += 8 * material_values[promo];
            }
            // capturing something at least as valuable as the capturing piece can not lose material, otherwise the
            // static exchange evaluation tells if the capture wins or loses material
            bool good = promo || victim_value >= attacker_value || see(BB, move) >= 0;
            scores[i] = (good ? SCORE_GOOD_CAPTURE : SCORE_BAD_CAPTURE) + score;
            continue;
        }

        if (use_killers) {
            if (move == killer1) {
                scores[i] = SCORE_KILLER_1;
                continue;
            }
            if (move == killer2) {
                scores[i] = SCORE_KILLER_2;
                continue;
            }
        }

        if (w < 0.0f) {
            w = game_phase_of(BB);
        }
        // how much the piece square tables like the move: the gain of the piece going from f to t (^ flip mirrors the
        // table for black), blended between the opening and the endgame table by the game phase
        int delta = (int)((white_position_tables[OPENING][mover_type][t ^ flip] - white_position_tables[OPENING][mover_type][f ^ flip]) * (1 - w) +
                          (white_position_tables[ENDGAME][mover_type][t ^ flip] - white_position_tables[ENDGAME][mover_type][f ^ flip]) * w);

        // history is the main part: how often this move caused a cutoff before (and for which squares were attacked)
        int quiet_score = history[side][f][t][HIST_IDX(attacks, f, t)] + delta;
        scores[i] = quiet_score;
    }
}

void merge_sort_array_dec(move_t moves[], int values[], int left, int right) {
    // merge sort: sorts the moves decreasingly by value. it is stable, moves with the same value keep their order
    if (left < right) {
        int middle = left + (right - left) / 2;

        merge_sort_array_dec(moves, values, left, middle);
        merge_sort_array_dec(moves, values, middle + 1, right);

        int left_size = middle - left + 1;
        int right_size = right - middle;

        int left_values[left_size], right_values[right_size];
        move_t left_moves[left_size], right_moves[right_size];

        for (int i = 0; i < left_size; i++) {
            left_values[i] = values[left + i];
            left_moves[i] = moves[left + i];
        }
        for (int i = 0; i < right_size; i++) {
            right_values[i] = values[middle + 1 + i];
            right_moves[i] = moves[middle + 1 + i];
        }

        int i = 0, j = 0, k = left;
        while (i < left_size && j < right_size) {
            if (left_values[i] >= right_values[j]) {
                values[k] = left_values[i];
                moves[k] = left_moves[i];
                i++;
            } else {
                values[k] = right_values[j];
                moves[k] = right_moves[j];
                j++;
            }
            k++;
        }

        while (i < left_size) {
            values[k] = left_values[i];
            moves[k] = left_moves[i];
            i++;
            k++;
        }

        while (j < right_size) {
            values[k] = right_values[j];
            moves[k] = right_moves[j];
            j++;
            k++;
        }
    }
}
