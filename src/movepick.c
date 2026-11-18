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

// ---------------------------------------------------------------------------------------------------------------------------------
// the staged move picker (see movepick.h). the scores are the same as in score_moves, so the order of the moves is the same

enum { MP_TT, MP_GEN_TACTICAL, MP_GOOD, MP_KILLER1, MP_KILLER2, MP_BAD, MP_GEN_QUIET, MP_QUIET, MP_DONE };

// is this move legal in the position? (it may come from the table, where it can belong to another position with the same hash bits, or be a
// killer move of another branch.) the moves of the piece on the from square are generated with the same code as all other moves and searched
static bool mp_is_legal(const struct move_picker *mp, move_t move) {
    move_t piece_moves[64]; // a piece has at most 27 moves
    const int n = get_moves_from(mp->BB, piece_moves, mp->checkers, mp->pinned, (u8 *)mp->pinners, mp->attacks, m_from(move));
    for (int i = 0; i < n; i++) {
        if (piece_moves[i] == move) {
            return true;
        }
    }
    return false;
}

// is the move a legal quiet move here that was not played yet as the table move? (a killer that is a capture here was already in the captures)
static inline bool mp_killer_ok(const struct move_picker *mp, move_t k) {
    return k != mp->tt && !is_tactical(mp->BB, k) && mp_is_legal(mp, k);
}

static inline int mp_victim_value(const struct bitboards *BB, move_t move, int mover_type) {
    const int victim = BB->piece_list[m_to(move)];
    if (victim) {
        return material_values[(victim < 0 ? -victim : victim) - 1];
    }
    if (mover_type == 0 && (int)m_to(move) == BB->en_passant_square) {
        return PAWNVALUE;
    }
    return 0;
}

// index of the best move in list[lo .. lo + count): highest score, equal scores in the order of generation
static inline int mp_best(const int *scores, const unsigned char *order, int lo, int count) {
    int best = lo;
    for (int j = lo + 1; j < lo + count; j++) {
        if (scores[j] > scores[best] || (scores[j] == scores[best] && order[j] < order[best])) {
            best = j;
        }
    }
    return best;
}

// generates the quiet moves (not the ones that are returned before the quiet stage) and scores them: history (for which squares are attacked
// by the enemy) plus the piece square tables
static int mp_score_quiets(const struct move_picker *mp, move_t *out_moves, int *out_scores, unsigned char *out_order) {
    const struct bitboards *BB = mp->BB;
    move_t quiets[MAX_NUM_MOVES];
    const int count = get_quiets(mp->BB, quiets, mp->checkers, mp->pinned, (u8 *)mp->pinners, mp->attacks);
    const int side = BB->color == WHITE ? 0 : 1;
    const int flip = BB->color == WHITE ? 0 : 56;
    const float w = game_phase_of(BB);
    int n = 0;
    for (int i = 0; i < count; i++) {
        const move_t move = quiets[i];
        if (move == mp->tt || move == mp->k1 || move == mp->k2) {
            continue;
        }
        const int f = m_from(move);
        const int t = m_to(move);
        const int piece = BB->piece_list[f];
        const int mover_type = (piece < 0 ? -piece : piece) - 1;
        // how much the piece square tables like the move, blended between the opening and the endgame table by the game phase
        const int delta = (int)((white_position_tables[OPENING][mover_type][t ^ flip] - white_position_tables[OPENING][mover_type][f ^ flip]) * (1 - w) +
                                (white_position_tables[ENDGAME][mover_type][t ^ flip] - white_position_tables[ENDGAME][mover_type][f ^ flip]) * w);
        out_moves[n] = move;
        out_scores[n] = history[side][f][t][HIST_IDX(mp->attacks, f, t)] + delta;
        out_order[n] = (unsigned char)i;
        n++;
    }
    return n;
}

move_t mp_next(struct move_picker *mp, int *bad) {
    *bad = 0;
    struct bitboards *BB = mp->BB;

    switch (mp->stage) {
    case MP_TT:
#if MP_SNAPSHOT
        mp->snap_n = mp_score_quiets(mp, mp->snap_moves, mp->snap_scores, mp->snap_order);
#endif
        mp->stage = MP_GEN_TACTICAL;
        if (mp->tt != 0 && mp_is_legal(mp, mp->tt)) {
            return mp->tt;
        }
        /* fall through */

    case MP_GEN_TACTICAL:
        mp->n = get_tactical(BB, mp->moves, mp->checkers, mp->pinned, (u8 *)mp->pinners, mp->attacks);
        mp->nbad = 0;
        for (int i = 0; i < mp->n; i++) {
            const move_t move = mp->moves[i];
            const int piece = BB->piece_list[m_from(move)];
            const int mover_type = (piece < 0 ? -piece : piece) - 1;
            const int promo = m_promo(move);
            // most valuable victim first, least valuable attacker first (the victim counts 16 times as much)
            int score = 16 * mp_victim_value(BB, move, mover_type) - material_values[mover_type];
            if (promo) {
                score += 8 * material_values[promo];
            }
            mp->scores[i] = score;
            mp->order[i] = (unsigned char)i;
        }
        mp->stage = MP_GOOD;
        /* fall through */

    case MP_GOOD:
        while (mp->n > 0) {
            const int b = mp_best(mp->scores, mp->order, 0, mp->n);
            const move_t move = mp->moves[b];
            const int score = mp->scores[b];
            const unsigned char ord = mp->order[b];
            mp->n--;
            mp->moves[b] = mp->moves[mp->n];
            mp->scores[b] = mp->scores[mp->n];
            mp->order[b] = mp->order[mp->n];
            if (move == mp->tt) {
                continue; // already returned (it is only in the list if it was legal)
            }
            // a capture of something at least as valuable as the capturing piece can not lose material, otherwise the static exchange
            // evaluation tells (promotions are always good)
            if (!m_promo(move)) {
                const int piece = BB->piece_list[m_from(move)];
                const int mover_type = (piece < 0 ? -piece : piece) - 1;
                if (mp_victim_value(BB, move, mover_type) < material_values[mover_type] && see(BB, move) < 0) {
                    mp->nbad++;
                    mp->moves[MAX_NUM_MOVES - mp->nbad] = move;
                    mp->scores[MAX_NUM_MOVES - mp->nbad] = score;
                    mp->order[MAX_NUM_MOVES - mp->nbad] = ord;
                    continue;
                }
            }
            return move;
        }
        mp->stage = MP_KILLER1;
        /* fall through */

    case MP_KILLER1:
        mp->stage = MP_KILLER2;
        if (mp->k1 != 0 && mp_killer_ok(mp, mp->k1)) {
            return mp->k1;
        }
        /* fall through */

    case MP_KILLER2:
        mp->stage = MP_BAD;
        if (mp->k2 != 0 && mp->k2 != mp->k1 && mp_killer_ok(mp, mp->k2)) {
            return mp->k2;
        }
        /* fall through */

    case MP_BAD:
        if (mp->nbad > 0) {
            const int lo = MAX_NUM_MOVES - mp->nbad;
            const int b = mp_best(mp->scores, mp->order, lo, mp->nbad);
            const move_t move = mp->moves[b];
            mp->moves[b] = mp->moves[lo];
            mp->scores[b] = mp->scores[lo];
            mp->order[b] = mp->order[lo];
            mp->nbad--;
            *bad = 1;
            return move;
        }
        mp->stage = MP_GEN_QUIET;
        /* fall through */

    case MP_GEN_QUIET:
#if MP_SNAPSHOT
        mp->n = mp->snap_n;
        memcpy(mp->moves, mp->snap_moves, mp->snap_n * sizeof(move_t));
        memcpy(mp->scores, mp->snap_scores, mp->snap_n * sizeof(int));
        memcpy(mp->order, mp->snap_order, mp->snap_n);
#else
        // the list is free again (the bad captures are all returned)
        mp->n = mp_score_quiets(mp, mp->moves, mp->scores, mp->order);
#endif
        mp->stage = MP_QUIET;

        /* fall through */

    case MP_QUIET:
        if (mp->n > 0) {
            const int b = mp_best(mp->scores, mp->order, 0, mp->n);
            const move_t move = mp->moves[b];
            mp->n--;
            mp->moves[b] = mp->moves[mp->n];
            mp->scores[b] = mp->scores[mp->n];
            mp->order[b] = mp->order[mp->n];
            return move;
        }
        mp->stage = MP_DONE;
        /* fall through */

    default:
        return 0;
    }
}
