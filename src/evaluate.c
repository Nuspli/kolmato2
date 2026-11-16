// the static evaluation (evaluate) and what it is made of: material, piece square tables, pawn structure, mobility, king safety,
// mop up of a lone king. a pure function of the position (the transposition table relies on that)
#include "search.h"

u64 white_squares = 0xAA55AA55AA55AA55;
u64 black_squares = 0x55AA55AA55AA55AA;

bool draw_eval_by_material(struct bitboards *BITBOARDS) {

    if (bit_count(BITBOARDS->bits[all_pieces]) == 2) {
        // only kings
        return true;

    } else if ((BITBOARDS->bits[white_pawns] | BITBOARDS->bits[black_pawns]) == 0) {
        // no pawns
        if ((BITBOARDS->bits[white_rooks] | BITBOARDS->bits[black_rooks] | BITBOARDS->bits[white_queens] | BITBOARDS->bits[black_queens]) == 0) {
            // no rooks or queens
            if ((BITBOARDS->bits[white_bishops] | BITBOARDS->bits[black_bishops]) == 0) {
                // no bishops
                if (bit_count(BITBOARDS->bits[white_knights]) < 3 && bit_count(BITBOARDS->bits[black_knights]) < 3) {
                    // less than 3 knights can not mate
                    return true;
                }

            } else if ((BITBOARDS->bits[white_knights] | BITBOARDS->bits[black_knights]) == 0) {
                // there are bishops but no knights
                // only mate would be with bishop pair vs no bishops
                if (abs(bit_count(BITBOARDS->bits[white_bishops]) - bit_count(BITBOARDS->bits[black_bishops])) < 2) {
                    return true;
                }

            } else if ((bit_count(BITBOARDS->bits[white_knights]) < 3 && BITBOARDS->bits[white_bishops] == 0) || 
                       (bit_count(BITBOARDS->bits[white_bishops]) == 1 && BITBOARDS->bits[white_knights] == 0)) {
                
                if ((bit_count(BITBOARDS->bits[black_knights]) < 3 && BITBOARDS->bits[black_bishops] == 0) || 
                    (bit_count(BITBOARDS->bits[black_bishops]) == 1 && BITBOARDS->bits[black_knights] == 0)) {
                    // both players either have not enough knights or only one bishop
                    return true;
                }
            }

        } else if ((BITBOARDS->bits[white_queens] | BITBOARDS->bits[black_queens]) == 0) {
            // there are rooks but no queens
            if (bit_count(BITBOARDS->bits[white_rooks]) == 1 && bit_count(BITBOARDS->bits[black_rooks]) == 1) {
                // both have exactly one rook
                if ((bit_count(BITBOARDS->bits[white_knights]) + bit_count(BITBOARDS->bits[white_bishops])) < 2 && 
                    (bit_count(BITBOARDS->bits[black_knights]) + bit_count(BITBOARDS->bits[black_bishops])) < 2) {
                    // but not enough minor pieces to mate
                    return true;
                }

            } else if (bit_count(BITBOARDS->bits[white_rooks]) == 1 && BITBOARDS->bits[black_rooks] == 0) {
                // white has exactly one rook   
                 if (((BITBOARDS->bits[white_knights] | BITBOARDS->bits[white_bishops]) == 0) && 
                    // but no minor pieces
                    // black has 1 or 2 minor pieces
                     (((bit_count(BITBOARDS->bits[black_knights]) + bit_count(BITBOARDS->bits[black_bishops])) == 1) ||
                      ((bit_count(BITBOARDS->bits[black_knights]) + bit_count(BITBOARDS->bits[black_bishops])) == 2))) {
                        return true;
                    }
            } else if (bit_count(BITBOARDS->bits[black_rooks]) == 1 && BITBOARDS->bits[white_rooks] == 0) {
                // black has exactly one rook
                if (((BITBOARDS->bits[black_knights] | BITBOARDS->bits[black_bishops]) == 0) &&
                    // but no minor pieces
                    // white has 1 or 2 minor pieces
                    (((bit_count(BITBOARDS->bits[white_knights]) + bit_count(BITBOARDS->bits[white_bishops])) == 1) ||
                     ((bit_count(BITBOARDS->bits[white_knights]) + bit_count(BITBOARDS->bits[white_bishops])) == 2))) {
                    return true;
                }
            }
        }
    }
    return false;
}

// how far the game has progressed towards the endgame, from 0 (opening) to 1 (endgame).
// this only depends on the material on the board, so that the evaluation is a pure
// function of the position (which is required for the transposition table to be sound).
// the material is counted with the fixed standard values (PAWNVALUE, ...) and not with material_values, so that the phase
// does not change when the material values are tuned. the full starting material is 7680
static inline float game_phase(const struct bitboards *BB) {
    int total_material =
        PAWNVALUE * bit_count(BB->bits[white_pawns] | BB->bits[black_pawns]) +
        KNIGHTVALUE * bit_count(BB->bits[white_knights] | BB->bits[black_knights]) +
        BISHOPVALUE * bit_count(BB->bits[white_bishops] | BB->bits[black_bishops]) +
        ROOKVALUE * bit_count(BB->bits[white_rooks] | BB->bits[black_rooks]) +
        QUEENVALUE * bit_count(BB->bits[white_queens] | BB->bits[black_queens]);
    if (total_material <= 0) {
        return 1.0f;
    }
    float weight = ((7680 / (float)total_material) - 1.08f) * 0.5f;
    if (weight < 0.0f) weight = 0.0f;
    if (weight > 1.0f) weight = 1.0f;
    return weight;
}

// counts the squares the pieces can go to, separately for knights, bishops, rooks and queens. squares that are occupied by own
// pieces or attacked by enemy pawns are not counted
// the attack sets of the sliders are kept (att->b / r / q, n = how many) so that the king attack terms of eval_extra do not
// generate them a second time. 10 = 2 + 8 promotions, 9 queens at most, so no array can overflow
struct slider_att { u64 b[10], r[10], q[10]; int nb, nr, nq; };

static inline __attribute__((always_inline)) void mobility_counts(const struct bitboards *BB, const bool white, int counts[4], struct slider_att *att) {
    int own = white ? 0 : 6;
    u64 enemy_pawns = BB->bits[white_pawns + (white ? 6 : 0)];
    u64 enemy_pawn_attacks = white ?
        (((enemy_pawns >> 7) & not_rightmost_file_mask) | ((enemy_pawns >> 9) & not_leftmost_file_mask)) :
        (((enemy_pawns << 7) & not_leftmost_file_mask) | ((enemy_pawns << 9) & not_rightmost_file_mask));
    u64 safe = ~BB->bits[white ? white_pieces : black_pieces] & ~enemy_pawn_attacks;
    u64 occ = BB->bits[all_pieces];
    counts[0] = counts[1] = counts[2] = counts[3] = 0;
    att->nb = att->nr = att->nq = 0;

    for (u64 n = BB->bits[white_knights + own]; n; n &= n - 1) {
        counts[0] += bit_count(knight_attacks[lsb(n)] & safe);
    }
    for (u64 b = BB->bits[white_bishops + own]; b; b &= b - 1) {
        u64 a = generate_bishop_moves(lsb(b), occ);
        att->b[att->nb++] = a;
        counts[1] += bit_count(a & safe);
    }
    for (u64 r = BB->bits[white_rooks + own]; r; r &= r - 1) {
        u64 a = generate_rook_moves(lsb(r), occ);
        att->r[att->nr++] = a;
        counts[2] += bit_count(a & safe);
    }
    for (u64 q = BB->bits[white_queens + own]; q; q &= q - 1) {
        int sq = lsb(q);
        u64 a = generate_bishop_moves(sq, occ) | generate_rook_moves(sq, occ);
        att->q[att->nq++] = a;
        counts[3] += bit_count(a & safe);
    }
}

// the same as game_phase for code outside of this file (the texel tuner)
float game_phase_of(const struct bitboards *BB) {
    return game_phase(BB);
}

// Checkmating a lone king. Without help the search only finds the mate once it is within its horizon, and the king
// endgame table even rewards the lone king for standing in the center. This returns a bonus (>= 0) for the side that
// has the material, which grows as the lone king is driven towards the edge. With exactly a bishop and a knight the mate
// is only possible in a corner of the color of the bishop, so the king is driven there.
// it is used when the other side has nothing but a king and the pieces are enough to checkmate
static int mop_up_bonus(const struct bitboards *BB, bool white_has_material) {
    int own = white_has_material ? 0 : 6;
    int enemy = white_has_material ? 6 : 0;
    u64 queens = BB->bits[white_queens + own], rooks = BB->bits[white_rooks + own];
    u64 bishops = BB->bits[white_bishops + own], knights = BB->bits[white_knights + own];
    u64 pawns = BB->bits[white_pawns + own];

    // a lone minor piece (or two knights, which the draw detection handles) can not checkmate, and with pawns on the
    // board the game is about promoting them, not about the corner
    if (!queens && !rooks && bit_count(bishops | knights) < 2) {
        return 0;
    }

    int king = lsb(BB->bits[white_king + enemy]);
    int file = king % 8, rank = king / 8;

    if (!queens && !rooks && !pawns && bit_count(bishops) == 1 && bit_count(knights) == 1) {
        u64 bishop_color = (bishops & white_squares) ? white_squares : black_squares;
        int nearest = 14;
        static const int corners[4] = {0, 7, 56, 63};
        for (int i = 0; i < 4; i++) {
            if (bishop_color & (1ULL << corners[i])) {
                int distance = abs(corners[i] % 8 - file) + abs(corners[i] / 8 - rank);
                if (distance < nearest) {
                    nearest = distance;
                }
            }
        }
        return eval_mop_up_corner * (7 - nearest);
    }

    int file_from_edge = file < 7 - file ? file : 7 - file; // 0 on the edge, 3 in the center
    int rank_from_edge = rank < 7 - rank ? rank : 7 - rank;
    return eval_mop_up_edge * ((3 - file_from_edge) + (3 - rank_from_edge));
}

// in the build of the texel tuner (-DTUNE) this records how often a term occurs for white and for black, otherwise it does nothing
#ifdef TUNE
#define TRACE(feature, white_count, black_count) do { eval_trace.white[feature] = (white_count); eval_trace.black[feature] = (black_count); } while (0)
#else
#define TRACE(feature, white_count, black_count) do { } while (0)
#endif

// switches for the groups of additional evaluation terms (each one can be tested by itself with -DEV_xxx=0).
// several groups (passer2, king2, mob2, bpair2, blockfix) were rejected by SPRT and are removed

static inline int chebyshev(int a, int b) {
    int df = abs(a % 8 - b % 8), dr = abs(a / 8 - b / 8);
    return df > dr ? df : dr;
}

#define FORCE_INLINE static inline __attribute__((always_inline))

// `white` is a compile time constant in all the per side functions below (always_inline, called with the literals true / false),
// so each color gets its own code without a runtime test and the source exists once. what differs between the colors:
// d = +1 / -1 is the direction the pawns move in (white pawns go to higher squares), the pieces of a color are bits[offset + type]
// (offset 0 for white, 6 for black), and "rel" ranks count from the side's own back rank (7 - rank for black)

// the squares on the ranks in front of rank r (as seen from the color that moves in direction d)
FORCE_INLINE u64 ranks_ahead(bool white, int r) {
    return white ? (r < 7 ? ~0ULL << (8 * (r + 1)) : 0) : (1ULL << (8 * r)) - 1;
}

FORCE_INLINE u64 pawn_attacks_of(bool white, u64 pawns) {
    return white ? ((pawns << 7) & not_leftmost_file_mask) | ((pawns << 9) & not_rightmost_file_mask)
                 : ((pawns >> 7) & not_rightmost_file_mask) | ((pawns >> 9) & not_leftmost_file_mask);
}

// the terms counted for one side; eval_extra reports them to the texel tuner (TRACE) and scales / adds them
struct extra_terms {
    int supported, phalanx, backward, protected_passer, passer_king; // pawns
    int shield, open_files, attackers[4]; // king safety (attackers: knight, bishop, rook, queen)
    int pawn_attacks_minor, pawn_attacks_major, outposts, seventh;
    int score; // the sum of all of them, positive = good for the side
};

FORCE_INLINE void extra_terms_for(const bool white, const struct bitboards *BB, float endgame_weight, const struct slider_att *enemy_sliders, struct extra_terms *x) {
    const int own = white ? 0 : 6, enemy = white ? 6 : 0;
    const int d = white ? 1 : -1;
    const u64 own_pawns = BB->bits[white_pawns + own], enemy_pawns = BB->bits[white_pawns + enemy];
    const u64 own_att = pawn_attacks_of(white, own_pawns), enemy_att = pawn_attacks_of(!white, enemy_pawns);
    const int own_king = lsb(BB->bits[white_king + own]), enemy_king = lsb(BB->bits[white_king + enemy]);
    int sc = 0;

    memset(x, 0, sizeof(*x));

    // pawn structure
    for (u64 p = own_pawns; p; p &= p - 1) {
        int b = lsb(p), f = b % 8, r = b / 8;
        u64 me = bit(b);
        const bool not_last_rank = white ? r < 7 : r > 0;
        x->supported += (me & own_att) != 0;
        x->phalanx += (adj_files[f] & ((u64)0xFF << (8 * r)) & own_pawns) != 0;
        // backward: neighbors on the files next to it only behind it, and the square in front of it is attacked by an enemy pawn
        if ((adj_files[f] & own_pawns) != 0 && (adj_files[f] & own_pawns & ~ranks_ahead(white, r)) == 0 && not_last_rank && (bit(b + 8 * d) & enemy_att)) {
            x->backward++;
        }
        // passed pawn: no enemy pawn in front of it on its file or the files next to it
        if (((file[f] | adj_files[f]) & ranks_ahead(white, r) & enemy_pawns) == 0 && not_last_rank) {
            x->protected_passer += (me & own_att) != 0;
            x->passer_king += (white ? r : 7 - r) * (chebyshev(enemy_king, b + 8 * d) - chebyshev(own_king, b + 8 * d));
        }
    }
    sc += eval_supported_pawn * x->supported + eval_phalanx_pawn * x->phalanx - eval_backward_pawn * x->backward + eval_protected_passer * x->protected_passer;
    sc += (int)(eval_passer_king * x->passer_king * endgame_weight);

    // king safety (middlegame): the pawn shield, open files next to the king, and the enemy pieces that attack the zone around it
    {
        const float mg = 1.0f - endgame_weight;
        const int kf = own_king % 8, kr = own_king / 8;
        u64 ranks = 0;
        for (int i = 1; i <= 2; i++) {
            int r = kr + d * i;
            if (r >= 0 && r < 8) ranks |= (u64)0xFF << (8 * r);
        }
        x->shield = bit_count((file[kf] | adj_files[kf]) & ranks & own_pawns);
        for (int f = kf - 1; f <= kf + 1; f++) {
            if (f >= 0 && f < 8 && (file[f] & own_pawns) == 0) x->open_files++;
        }
        // the enemy attacks only count if the enemy has a queen (without one an attack rarely succeeds)
        if (BB->bits[white_queens + enemy] != 0) {
            u64 zone = king_attacks[own_king] | bit(own_king);
            zone |= white ? (zone << 8) : (zone >> 8);
            for (u64 n = BB->bits[white_knights + enemy]; n; n &= n - 1) {
                x->attackers[0] += bit_count(knight_attacks[lsb(n)] & zone);
            }
            // the attacks of the enemy sliders were generated by mobility_counts
            for (int i = 0; i < enemy_sliders->nb; i++) x->attackers[1] += bit_count(enemy_sliders->b[i] & zone);
            for (int i = 0; i < enemy_sliders->nr; i++) x->attackers[2] += bit_count(enemy_sliders->r[i] & zone);
            for (int i = 0; i < enemy_sliders->nq; i++) x->attackers[3] += bit_count(enemy_sliders->q[i] & zone);
        }
        int safety = eval_king_shield * x->shield - eval_king_open_file * x->open_files - eval_king_attack_knight * x->attackers[0] -
                     eval_king_attack_bishop * x->attackers[1] - eval_king_attack_rook * x->attackers[2] - eval_king_attack_queen * x->attackers[3];
        sc += (int)(safety * mg);
    }

    // own knights and bishops attacked by enemy pawns, rooks and queens attacked by enemy pawns
    x->pawn_attacks_minor = bit_count(enemy_att & (BB->bits[white_knights + own] | BB->bits[white_bishops + own]));
    x->pawn_attacks_major = bit_count(enemy_att & (BB->bits[white_rooks + own] | BB->bits[white_queens + own]));
    sc -= eval_pawn_attacks_minor * x->pawn_attacks_minor + eval_pawn_attacks_major * x->pawn_attacks_major;

    // knight outposts (4th to 6th rank counted from the own side, defended by a pawn, no enemy pawn can attack it) and rooks on the 7th rank
    for (u64 n = BB->bits[white_knights + own]; n; n &= n - 1) {
        int b = lsb(n), f = b % 8, r = b / 8;
        int rel = white ? r : 7 - r;
        if (rel >= 3 && rel <= 5 && (bit(b) & own_att) && (adj_files[f] & enemy_pawns & ranks_ahead(white, r)) == 0) x->outposts++;
    }
    for (u64 r = BB->bits[white_rooks + own]; r; r &= r - 1) {
        if (lsb(r) / 8 == (white ? 6 : 1) && ((enemy_king / 8 == (white ? 7 : 0)) || (enemy_pawns & ((u64)0xFF << (white ? 48 : 8))))) x->seventh++;
    }
    sc += eval_knight_outpost * x->outposts + eval_rook_seventh * x->seventh;

    x->score = sc;
}

// the additional evaluation terms. returns how much better white and black are (both positive = good for that side).
// the terms that only matter in the middlegame are scaled with (1 - endgame_weight), the king distance to passed pawns
// with endgame_weight. the counts are recorded for the texel tuner (TRACE) like in evaluate()
static void eval_extra(const struct bitboards *BB, float endgame_weight, int *white_score, int *black_score, const struct slider_att sliders[2]) {
    struct extra_terms w, b;
    extra_terms_for(true, BB, endgame_weight, &sliders[1], &w); // the attackers of the white king are the black sliders
    extra_terms_for(false, BB, endgame_weight, &sliders[0], &b);
    TRACE(F_SUPPORTED, w.supported, b.supported);
    TRACE(F_PHALANX, w.phalanx, b.phalanx);
    TRACE(F_BACKWARD, w.backward, b.backward);
    TRACE(F_PROTECTED_PASSER, w.protected_passer, b.protected_passer);
    TRACE(F_PASSER_KING, w.passer_king, b.passer_king);
    TRACE(F_KING_SHIELD, w.shield, b.shield);
    TRACE(F_KING_OPEN, w.open_files, b.open_files);
    TRACE(F_KING_ATT_KNIGHT, w.attackers[0], b.attackers[0]);
    TRACE(F_KING_ATT_BISHOP, w.attackers[1], b.attackers[1]);
    TRACE(F_KING_ATT_ROOK, w.attackers[2], b.attackers[2]);
    TRACE(F_KING_ATT_QUEEN, w.attackers[3], b.attackers[3]);
    TRACE(F_PAWN_ATT_MINOR, w.pawn_attacks_minor, b.pawn_attacks_minor);
    TRACE(F_PAWN_ATT_MAJOR, w.pawn_attacks_major, b.pawn_attacks_major);
    TRACE(F_OUTPOST, w.outposts, b.outposts);
    TRACE(F_ROOK_SEVENTH, w.seventh, b.seventh);
    *white_score = w.score;
    *black_score = b.score;
}

// the pawn structure, bishop and rook terms of one side
struct pawn_terms {
    int doubled, isolated, blocked, passed_bonus, passed_count[8], bishop_blocked, rooks_open, rooks_semi, bishop_pair;
};

FORCE_INLINE void pawn_terms_for(const bool white, const struct bitboards *BB, struct pawn_terms *t) {
    const int own = white ? 0 : 6, enemy = white ? 6 : 0;
    const u64 own_pawns = BB->bits[white_pawns + own], enemy_pawns = BB->bits[white_pawns + enemy];
    const u64 enemy_pieces = BB->bits[white ? black_pieces : white_pieces];

    memset(t, 0, sizeof(*t));

    for (u64 p = own_pawns; p; p &= p - 1) {
        u8 b = lsb(p);
        const int f = b % 8, r = b / 8;
        const u64 ahead = ranks_ahead(white, r);
        t->doubled += bit_count((bit(b) ^ file[f]) & own_pawns);
        t->isolated += (adj_files[f] & own_pawns) == 0;
        t->blocked += (file[f] & ahead & enemy_pieces) != 0;
        if (((file[f] | adj_files[f]) & ahead & enemy_pawns) == 0) {
            const int rel = white ? r : 7 - r;
            t->passed_bonus += eval_passed_pawn[rel];
            t->passed_count[rel]++;
        }
    }

    // a bishop that is blocked by own pawns on its color is bad
    for (u64 bs = BB->bits[white_bishops + own]; bs; bs &= bs - 1) {
        u8 b = lsb(bs);
        t->bishop_blocked += ((bit(b) & white_squares) > 0) * bit_count(white_squares & own_pawns);
        t->bishop_blocked += ((bit(b) & black_squares) > 0) * bit_count(black_squares & own_pawns);
    }

    // rook on an open file (no pawns) or semi open file (only enemy pawns)
    for (u64 rs = BB->bits[white_rooks + own]; rs; rs &= rs - 1) {
        u8 b = lsb(rs);
        t->rooks_open += (file[b % 8] & (own_pawns | enemy_pawns)) == 0;
        t->rooks_semi += ((file[b % 8] & own_pawns) == 0) && ((file[b % 8] & enemy_pawns) != 0);
    }

    t->bishop_pair = bit_count(BB->bits[white_bishops + own]) >= 2;
}

// everything but material, piece square scores, extra terms and the endgame / mop up special cases: positive = good for the side
FORCE_INLINE int pawn_terms_score(const struct pawn_terms *t, const int mobility[4], float endgame_weight) {
    int sc = 0;
    for (int piece = 0; piece < 4; piece++) sc += eval_mobility[piece] * mobility[piece];
    sc += t->bishop_pair * eval_bishop_pair;
    sc += t->rooks_open * eval_rook_open_file;
    sc += t->rooks_semi * eval_rook_semi_open_file;
    sc -= eval_bishop_blocked_by_pawn * t->bishop_blocked;
    sc -= eval_doubled_pawn * t->doubled + eval_isolated_pawn * t->isolated + eval_blocked_pawn * t->blocked;
    sc += (int)(t->passed_bonus * (0.3f + 0.7f * endgame_weight));
    return sc;
}

// the endgame king term of the side that is ahead (positive = good for it): a penalty for the distance between the kings and the endgame king table
static inline __attribute__((always_inline)) int endgame_king_term(const bool white, const struct bitboards *BB, int dist_between_kings) {
    const int own_king = lsb(BB->bits[white_king + (white ? 0 : 6)]);
    int term = 0;
    term -= (dist_between_kings < 4) * 5 * dist_between_kings;
    term -= (dist_between_kings >= 4) * 10 * dist_between_kings;
    term += king_eval_white_endgame_position[white ? own_king : own_king ^ 56] / 2;
    return term;
}

int evaluate(struct bitboards *BITBOARDS) {

    eval_calls++;

#ifdef TUNE
    memset(&eval_trace, 0, sizeof(eval_trace));
#endif

    if (draw_eval_by_material(BITBOARDS)) {
        return 0;
    }

    int eval_white = 0;
    int eval_black = 0;

    // material (black's score is kept negated)
    eval_white += BITBOARDS->white_material;
    eval_black += BITBOARDS->black_material;

    // pawn structure, bishops and rooks
    struct pawn_terms pawn_w, pawn_b;
    pawn_terms_for(true, BITBOARDS, &pawn_w);
    pawn_terms_for(false, BITBOARDS, &pawn_b);

    // mobility: the number of squares the pieces can go to, not counting squares that are occupied by own pieces
    // or attacked by enemy pawns
    int mobility_white[4];
    int mobility_black[4];
    struct slider_att slider_att[2];
    mobility_counts(BITBOARDS, true, mobility_white, &slider_att[0]);
    mobility_counts(BITBOARDS, false, mobility_black, &slider_att[1]);

    // game phase dependent evaluation
    float endgame_weight = game_phase(BITBOARDS);

    // the terms of each side are summed as "good for the side" and added to white / subtracted from black (whose score is negated)
    eval_white += pawn_terms_score(&pawn_w, mobility_white, endgame_weight);
    eval_black -= pawn_terms_score(&pawn_b, mobility_black, endgame_weight);

    for (int piece = 0; piece < 4; piece++) {
        TRACE(F_MOBILITY_KNIGHT + piece, mobility_white[piece], mobility_black[piece]);
    }
    TRACE(F_BISHOP_PAIR, pawn_w.bishop_pair, pawn_b.bishop_pair);
    TRACE(F_ROOK_OPEN, pawn_w.rooks_open, pawn_b.rooks_open);
    TRACE(F_ROOK_SEMI, pawn_w.rooks_semi, pawn_b.rooks_semi);
    TRACE(F_BISHOP_BLOCKED, pawn_w.bishop_blocked, pawn_b.bishop_blocked);
    TRACE(F_DOUBLED, pawn_w.doubled, pawn_b.doubled);
    TRACE(F_ISOLATED, pawn_w.isolated, pawn_b.isolated);
    TRACE(F_BLOCKED, pawn_w.blocked, pawn_b.blocked);
    for (int rank = 0; rank < 8; rank++) {
        TRACE(F_PASSED_0 + rank, pawn_w.passed_count[rank], pawn_b.passed_count[rank]);
    }

    int extra_white, extra_black;
    eval_extra(BITBOARDS, endgame_weight, &extra_white, &extra_black, slider_att);
    eval_white += extra_white;
    eval_black -= extra_black;

    // activity

    eval_white += (int)(BITBOARDS->white_position_opening * (1 - endgame_weight) + BITBOARDS->white_position_endgame * endgame_weight);
    eval_black += (int)(BITBOARDS->black_position_opening * (1 - endgame_weight) + BITBOARDS->black_position_endgame * endgame_weight);

    if (endgame_weight == 1) {
        int dist_between_kings = abs(lsb(BITBOARDS->bits[black_king]) / 8 - lsb(BITBOARDS->bits[white_king]) / 8) + abs(lsb(BITBOARDS->bits[black_king]) % 8 - lsb(BITBOARDS->bits[white_king]) % 8);
        // important for endgames: the side that is ahead brings its king closer to the enemy king and towards the center
        if (eval_white > -eval_black) {
            eval_white += endgame_king_term(true, BITBOARDS, dist_between_kings);
        } else if (-eval_black > eval_white) {
            eval_black -= endgame_king_term(false, BITBOARDS, dist_between_kings);
        }
    }

    // checkmating a lone king
    if (BITBOARDS->bits[black_pieces] == BITBOARDS->bits[black_king]) {
        eval_white += mop_up_bonus(BITBOARDS, true);
    } else if (BITBOARDS->bits[white_pieces] == BITBOARDS->bits[white_king]) {
        eval_black -= mop_up_bonus(BITBOARDS, false);
    }

#ifdef TUNE
    eval_trace.eval_white = eval_white;
    eval_trace.eval_black = eval_black;
    eval_trace.valid = true;
#endif

    return ((int)(((eval_white + eval_black) * 17680) / (eval_white - eval_black + 10000))) * (BITBOARDS->color) + eval_tempo;
}
