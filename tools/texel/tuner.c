// Texel tuner for kolmato2.
//
// It adjusts the weights of the evaluation (material values, piece square tables and the other terms) so that the
// evaluation predicts the result of the games the positions come from as well as possible. Read README.md first.
//
// build (from the project directory, the engine must be compiled with -DTUNE so that evaluate() records its terms):
//   gcc -O2 -march=native -fopenmp -DTUNE -I. tools/texel/tuner.c $(ls *.c | grep -v '^main.c$') -o tuner -lm
// run:
//   ./tuner positions.csv --selftest          (checks that everything is consistent)
//   ./tuner positions.csv --method adam       (tunes, writes tuned_params.txt)
//
// positions.csv has one position per line:  <fen>,<result>,<game>   where result is 1.0 (white won), 0.5 (draw) or 0.0 (black won)
// and game is a number that identifies the game the position came from (the column may be missing in older files)

#include "main.h"
#include <stdint.h>
#include <math.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif

// A parameter is one integer of the engine that we want to tune (a material value, one square of a piece square table,
// the weight of one evaluation term). We keep a pointer to the engine's variable, so that the tuned values can be
// written back into the engine and the real evaluate() can be used to check the result.

#define MAX_PARAMS 1024

struct param {
    char name[48];
    int *value;        // the engine's variable
    int init;          // its value when we started
    double w;          // the current (real valued) weight that is optimized
    bool frozen;       // not changed by the optimizer
    long count;        // in how many training positions the parameter occurs (see --min-count)
    char group;        // 'm' material, 's' scalar terms, 'p' piece square tables
};

static struct param P[MAX_PARAMS];
static int NP = 0;

static int add_param(const char *name, int *value, char group, bool frozen) {
    if (NP >= MAX_PARAMS) {
        printf("too many parameters\n");
        exit(1);
    }
    snprintf(P[NP].name, sizeof(P[NP].name), "%s", name);
    P[NP].value = value;
    P[NP].init = *value;
    P[NP].w = *value;
    P[NP].frozen = frozen;
    P[NP].group = group;
    return NP++;
}

static const char *piece_names[6] = {"pawn", "knight", "bishop", "rook", "queen", "king"};

static int idx_material[5];     // parameter index of the material value of pawn .. queen
static int idx_pst;             // index of the first piece square table entry
static int pst_index(int phase, int type, int square) { return idx_pst + (phase * 6 + type) * 64 + square; }

// The terms of evaluate() that are counted (see the TRACE macros there). Each one is  sign * weight * (white - black)
// (optionally multiplied by the game phase factor that the passed pawn bonus has).
struct term {
    int feature; // F_... from eval.h
    char name[32];
    int *value;
    double sign;        // -1 for penalties (the engine subtracts them)
    int phased;         // 0: none, 1: passed pawn bonus factor (0.3 + 0.7 * endgame weight), 2: midgame terms (1 - endgame weight), 3: endgame terms (endgame weight)
    int param;          // filled in by build_params
    char cname[32];     // the name of the variable in eval.c (used for the export)
};

static struct term terms[] = {
    {F_BISHOP_BLOCKED, "bishopBlockedByPawn", &eval_bishop_blocked_by_pawn, -1, false, -1, "eval_bishop_blocked_by_pawn"},
    {F_DOUBLED, "doubledPawn", &eval_doubled_pawn, -1, false, -1, "eval_doubled_pawn"},
    {F_ISOLATED, "isolatedPawn", &eval_isolated_pawn, -1, false, -1, "eval_isolated_pawn"},
    {F_BLOCKED, "blockedPawn", &eval_blocked_pawn, -1, false, -1, "eval_blocked_pawn"},
    {F_PASSED_1, "passedPawn[rank2]", &eval_passed_pawn[1], +1, true, -1, "eval_passed_pawn"},
    {F_PASSED_2, "passedPawn[rank3]", &eval_passed_pawn[2], +1, true, -1, "eval_passed_pawn"},
    {F_PASSED_3, "passedPawn[rank4]", &eval_passed_pawn[3], +1, true, -1, "eval_passed_pawn"},
    {F_PASSED_4, "passedPawn[rank5]", &eval_passed_pawn[4], +1, true, -1, "eval_passed_pawn"},
    {F_PASSED_5, "passedPawn[rank6]", &eval_passed_pawn[5], +1, true, -1, "eval_passed_pawn"},
    {F_PASSED_6, "passedPawn[rank7]", &eval_passed_pawn[6], +1, true, -1, "eval_passed_pawn"},
    {F_ROOK_OPEN, "rookOpenFile", &eval_rook_open_file, +1, false, -1, "eval_rook_open_file"},
    {F_ROOK_SEMI, "rookSemiOpenFile", &eval_rook_semi_open_file, +1, false, -1, "eval_rook_semi_open_file"},
    {F_BISHOP_PAIR, "bishop_pair", &eval_bishop_pair, +1, false, -1, "eval_bishop_pair"},
    {F_MOBILITY_KNIGHT, "mobilityKnight", &eval_mobility[0], +1, false, -1, "eval_mobility"},
    {F_MOBILITY_BISHOP, "mobilityBishop", &eval_mobility[1], +1, false, -1, "eval_mobility"},
    {F_MOBILITY_ROOK, "mobilityRook", &eval_mobility[2], +1, false, -1, "eval_mobility"},
    {F_MOBILITY_QUEEN, "mobilityQueen", &eval_mobility[3], +1, false, -1, "eval_mobility"},
    {F_SUPPORTED, "supportedPawn", &eval_supported_pawn, +1, 0, -1, "eval_supported_pawn"},
    {F_PHALANX, "phalanxPawn", &eval_phalanx_pawn, +1, 0, -1, "eval_phalanx_pawn"},
    {F_BACKWARD, "backwardPawn", &eval_backward_pawn, -1, 0, -1, "eval_backward_pawn"},
    {F_PROTECTED_PASSER, "protected_passer", &eval_protected_passer, +1, 0, -1, "eval_protected_passer"},
    {F_PASSER_KING, "passer_king", &eval_passer_king, +1, 3, -1, "eval_passer_king"},
    {F_KING_SHIELD, "kingShield", &eval_king_shield, +1, 2, -1, "eval_king_shield"},
    {F_KING_OPEN, "kingOpenFile", &eval_king_open_file, -1, 2, -1, "eval_king_open_file"},
    {F_KING_ATT_KNIGHT, "kingAttackKnight", &eval_king_attack_knight, -1, 2, -1, "eval_king_attack_knight"},
    {F_KING_ATT_BISHOP, "kingAttackBishop", &eval_king_attack_bishop, -1, 2, -1, "eval_king_attack_bishop"},
    {F_KING_ATT_ROOK, "kingAttackRook", &eval_king_attack_rook, -1, 2, -1, "eval_king_attack_rook"},
    {F_KING_ATT_QUEEN, "kingAttackQueen", &eval_king_attack_queen, -1, 2, -1, "eval_king_attack_queen"},
    {F_PAWN_ATT_MINOR, "pawn_attacks_minor", &eval_pawn_attacks_minor, -1, 0, -1, "eval_pawn_attacks_minor"},
    {F_PAWN_ATT_MAJOR, "pawn_attacks_major", &eval_pawn_attacks_major, -1, 0, -1, "eval_pawn_attacks_major"},
    {F_OUTPOST, "knightOutpost", &eval_knight_outpost, +1, 0, -1, "eval_knight_outpost"},
    {F_ROOK_SEVENTH, "rookSeventh", &eval_rook_seventh, +1, 0, -1, "eval_rook_seventh"},
};
#define NUM_TERMS ((int)(sizeof(terms) / sizeof(terms[0])))

static void build_params(void) {
    for (int t = 0; t < 5; t++) {
        char name[48];
        snprintf(name, sizeof(name), "material[%s]", piece_names[t]);
        // the value of a pawn is kept at 90: it is the unit of the whole evaluation, the margins of the search
        // (futility, delta pruning, ...) are written in these units, and the sigmoid constant K could otherwise be absorbed
        idx_material[t] = add_param(name, &material_values[t], 'm', t == 0);
    }
    for (int i = 0; i < NUM_TERMS; i++) {
        terms[i].param = add_param(terms[i].name, terms[i].value, 's', false);
    }
    idx_pst = NP;
    for (int phase = 0; phase < 2; phase++) {
        for (int type = 0; type < 6; type++) {
            for (int sq = 0; sq < 64; sq++) {
                char name[48];
                snprintf(name, sizeof(name), "pst[%s][%s][%d]", phase == OPENING ? "opening" : "endgame", piece_names[type], sq);
                add_param(name, &white_position_tables[phase][type][sq], 'p', false);
            }
        }
    }
}

// For every position we write the evaluation (white's point of view, in centipawns) as
//
//      eval = scale * ( constant + sum_j coefficient_j * weight_j )
//
// where j runs over the few parameters that occur in the position. The coefficients are the "features": for a
// material value it is (white count - black count), for a piece square table entry it is +1 for a white piece on that
// square and -1 for a black piece (split between the opening and the endgame table by the game phase), and so on.
// Everything the model does not know about (the king terms of the endgame, rounding to integers, ...) ends up in
// "constant", which we compute once with the real engine: constant = real evaluation - model evaluation.
// "scale" is the factor 17680 / (white + |black| + 10000) at the end of evaluate(); we keep it fixed per position.
//
// Because of this form the error and its gradient can be computed from these few numbers without any chess logic,
// which makes every optimization step very fast. The real evaluate() is only used to build the features and, at the
// end, to check that the tuned weights really work in the engine.

struct position {
    float result; // 1, 0.5 or 0 from white's point of view
    float scale;
    float constant;
    float phase;         // game phase: 0 opening ... 1 endgame (only used by the self test)
    uint32_t start;      // the entries of this position are e_idx[start .. start + count)
    uint16_t count;
};

static struct position *pos = NULL;
static long num_pos = 0, n_train = 0, n_val = 0; // after loading: pos[0..n_train) is the training set, then the validation set
static uint16_t *e_idx = NULL;
static float *e_coef = NULL;
static size_t num_entries = 0, cap_entries = 0;

static char **val_fen = NULL; // the validation positions as text, to check the tuned weights with the real engine
static float *val_result = NULL;

static void push_entry(int param, double coefficient) {
    if (num_entries == cap_entries) {
        cap_entries = cap_entries ? cap_entries * 2 : (1u << 22);
        e_idx = realloc(e_idx, cap_entries * sizeof(uint16_t));
        e_coef = realloc(e_coef, cap_entries * sizeof(float));
        if (!e_idx || !e_coef) {
            printf("out of memory\n");
            exit(1);
        }
    }
    e_idx[num_entries] = (uint16_t)param;
    e_coef[num_entries] = (float)coefficient;
    num_entries++;
}

// sets up the engine's board for a position given as text and returns false if it can not be used
static bool setup_board(const char *fen) {
    char board[128], color[4], castle[8], ep[8];
    int half = 0, full = 1;
    if (sscanf(fen, "%127s %3s %7s %7s %d %d", board, color, castle, ep, &half, &full) < 4) {
        return false;
    }
    // fen_to_position does not clear squares of a previous position, and "no en passant square" is -1
    memset(bitboards, 0, sizeof(*bitboards));
    bitboards->en_passant_square = -1;
    fen_to_position(board, bitboards);
    init_boards(bitboards, strcmp(color, "w") == 0, castle, ep, half, full);
    return true;
}

// computes the features of the position that is currently set up. returns false for positions the evaluation does not
// handle with its normal terms (draws by material)
static bool extract_features(float result, struct position *out) {
    evaluate(bitboards); // fills eval_trace (the engine was compiled with -DTUNE)
    if (!eval_trace.valid) {
        return false;
    }
    float phase = game_phase_of(bitboards);
    size_t start = num_entries;

    // material: (white count - black count) of every piece type
    for (int t = 0; t < 5; t++) {
        int diff = bit_count(bitboards->bits[white_pawns + t]) - bit_count(bitboards->bits[black_pawns + t]);
        if (diff) {
            push_entry(idx_material[t], diff);
        }
    }

    // piece square tables. black uses the white tables mirrored (square ^ 56) and counts negative
    for (int sq = 0; sq < 64; sq++) {
        int piece = bitboards->piece_list[sq];
        if (piece > 0) {
            push_entry(pst_index(OPENING, piece - 1, sq), 1 - phase);
            push_entry(pst_index(ENDGAME, piece - 1, sq), phase);
        } else if (piece < 0) {
            push_entry(pst_index(OPENING, -piece - 1, sq ^ 56), -(1 - phase));
            push_entry(pst_index(ENDGAME, -piece - 1, sq ^ 56), -phase);
        }
    }

    // the counted terms of evaluate()
    for (int i = 0; i < NUM_TERMS; i++) {
        int diff = eval_trace.white[terms[i].feature] - eval_trace.black[terms[i].feature];
        if (diff) {
            double coefficient = terms[i].sign * diff * (terms[i].phased == 1 ? 0.3 + 0.7 * phase : terms[i].phased == 2 ? 1.0 - phase : terms[i].phased == 3 ? phase : 1.0);
            push_entry(terms[i].param, coefficient);
        }
    }

    // what the model predicts with the starting weights, and what is left over
    double model = 0;
    for (size_t e = start; e < num_entries; e++) {
        model += (double)e_coef[e] * P[e_idx[e]].init;
    }
    out->result = result;
    out->start = (uint32_t)start;
    out->count = (uint16_t)(num_entries - start);
    out->constant = (float)((eval_trace.eval_white + eval_trace.eval_black) - model);
    out->scale = (float)(17680.0 / (eval_trace.eval_white - eval_trace.eval_black + 10000));
    out->phase = phase;
    return true;
}

static void load_positions(const char *path, long max_positions, double val_fraction) {
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("can not open %s\n", path);
        exit(1);
    }
    long capacity = 1 << 20;
    pos = malloc(capacity * sizeof(struct position));
    char line[512];
    long lines = 0, skipped = 0;
    char **fens = NULL; // temporary: remember the text of every position, the validation ones are kept
    long fen_cap = 0;
    float *results = NULL;
    bool *is_val = NULL;
    long val_every = val_fraction > 0 ? (long)(1.0 / val_fraction + 0.5) : 0;

    while (fgets(line, sizeof(line), f) && (max_positions <= 0 || num_pos < max_positions)) {
        lines++;
        char *comma = strchr(line, ','); // a fen contains no comma
        if (!comma) {
            skipped++;
            continue;
        }
        *comma = 0;
        float result = (float)atof(comma + 1);
        char *second = strchr(comma + 1, ',');
        long game = second ? atol(second + 1) : -1;
        if (!setup_board(line)) {
            skipped++;
            continue;
        }
        if (num_pos == capacity) {
            capacity *= 2;
            pos = realloc(pos, capacity * sizeof(struct position));
        }
        if (!extract_features(result, &pos[num_pos])) {
            skipped++;
            continue;
        }
        if (num_pos == fen_cap) {
            fen_cap = fen_cap ? fen_cap * 2 : 1 << 20;
            fens = realloc(fens, fen_cap * sizeof(char *));
            results = realloc(results, fen_cap * sizeof(float));
            is_val = realloc(is_val, fen_cap * sizeof(bool));
        }
        // validation positions are chosen by game, not one by one: positions of the same game are nearly the same position
        // with the same result, and if some of them were used for training the validation error would be far too optimistic.
        // (without a game column the file is cut into blocks of 100 positions instead, which is only an approximation)
        long unit = game >= 0 ? game : num_pos / 100;
        // the id is mixed first (a multiplicative hash), so that the choice does not depend on how the ids happen to be numbered
        uint64_t mixed = ((uint64_t)unit * 0x9E3779B97F4A7C15ull) >> 33;
        bool val = val_every && (mixed % val_every == 0);
        is_val[num_pos] = val;
        fens[num_pos] = val ? strdup(line) : NULL;
        results[num_pos] = result;
        num_pos++;
    }
    fclose(f);

    // reorder: training positions first, then validation positions (position is small, the entries stay where they are)
    struct position *sorted = malloc(num_pos * sizeof(struct position));
    long t = 0, v = 0;
    for (long i = 0; i < num_pos; i++) if (!is_val[i]) n_train++; else n_val++;
    val_fen = malloc((n_val + 1) * sizeof(char *));
    val_result = malloc((n_val + 1) * sizeof(float));
    for (long i = 0; i < num_pos; i++) {
        if (is_val[i]) {
            sorted[n_train + v] = pos[i];
            val_fen[v] = fens[i];
            val_result[v] = results[i];
            v++;
        } else {
            sorted[t++] = pos[i];
        }
    }
    free(pos);
    pos = sorted;
    free(fens);
    free(results);
    free(is_val);
    if (val_every && n_val == 0) {
        printf("warning: no validation positions were selected (too few games?), the validation error will not be available\n");
    }
    printf("read %ld lines: %ld positions used (%ld training, %ld validation), %ld skipped (no features: draw by material or bad line)\n",
           lines, num_pos, n_train, n_val, skipped);
    printf("memory for the features: %.0f MB\n", (num_entries * (sizeof(uint16_t) + sizeof(float)) + num_pos * sizeof(struct position)) / 1e6);
}

// The evaluation is turned into a probability that white wins (or rather the expected score of white):
//
//      p = sigmoid(k * eval) = 1 / (1 + exp(-k * eval))
//
// and the error is the mean squared difference to the real result r (1, 0.5 or 0):   E = mean (r - p)^2
// k only decides how "sharp" the sigmoid is. It is fitted once (fit_k) and then kept constant. In the usual notation
// p = 1 / (1 + 10^(-K * eval / 400)) the two constants are related by k = K * ln(10) / 400.

static double sigmoid(double x) { return 1.0 / (1.0 + exp(-x)); }

static inline double eval_of(const struct position *p, const double *w) {
    double sum = p->constant;
    const uint16_t *idx = e_idx + p->start;
    const float *coef = e_coef + p->start;
    for (int e = 0; e < p->count; e++) {
        sum += (double)coef[e] * w[idx[e]];
    }
    return sum * p->scale;
}

// the error on positions [from, to), with the weights w and the sigmoid constant k
static double error_on(const double *w, double k, long from, long to) {
    double sum = 0;
#ifdef _OPENMP
#pragma omp parallel for reduction(+ : sum) schedule(static)
#endif
    for (long i = from; i < to; i++) {
        double d = pos[i].result - sigmoid(k * eval_of(&pos[i], w));
        sum += d * d;
    }
    return sum / (to - from);
}

static double *current_weights(void) {
    double *w = malloc(NP * sizeof(double));
    for (int j = 0; j < NP; j++) w[j] = P[j].w;
    return w;
}

// golden section search for the k that gives the smallest error with the starting weights
static double fit_k(void) {
    double *w = current_weights();
    double lo = 1e-4, hi = 5e-2, g = 0.6180339887;
    double a = hi - g * (hi - lo), b = lo + g * (hi - lo);
    double ea = error_on(w, a, 0, n_train), eb = error_on(w, b, 0, n_train);
    for (int it = 0; it < 40; it++) {
        if (ea < eb) { hi = b; b = a; eb = ea; a = hi - g * (hi - lo); ea = error_on(w, a, 0, n_train); }
        else         { lo = a; a = b; ea = eb; b = lo + g * (hi - lo); eb = error_on(w, b, 0, n_train); }
    }
    free(w);
    return (lo + hi) / 2;
}

// For one position, with  e = eval,  p = sigmoid(k e),  d = p - r :
//      d(error) / d(e) = 2 d * p (1 - p) * k            (chain rule: the square, then the sigmoid)
//      d(e) / d(weight_j) = scale * coefficient_j        (the evaluation is linear in the weights)
// so the gradient of the mean error with respect to weight j is the average over all positions of
//      2 d p (1 - p) k * scale * coefficient_j

static void gradient(const double *w, double k, double *grad) {
    int threads = 1;
#ifdef _OPENMP
    threads = omp_get_max_threads();
#endif
    double *local = calloc((size_t)threads * NP, sizeof(double));
#ifdef _OPENMP
#pragma omp parallel
#endif
    {
        int tid = 0;
#ifdef _OPENMP
        tid = omp_get_thread_num();
#endif
        double *g = local + (size_t)tid * NP;
#ifdef _OPENMP
#pragma omp for schedule(static)
#endif
        for (long i = 0; i < n_train; i++) {
            const struct position *p = &pos[i];
            double prob = sigmoid(k * eval_of(p, w));
            double common = 2 * (prob - p->result) * prob * (1 - prob) * k * p->scale / n_train;
            const uint16_t *idx = e_idx + p->start;
            const float *coef = e_coef + p->start;
            for (int e = 0; e < p->count; e++) {
                g[idx[e]] += common * coef[e];
            }
        }
    }
    for (int j = 0; j < NP; j++) {
        double sum = 0;
        for (int t = 0; t < threads; t++) sum += local[(size_t)t * NP + j];
        grad[j] = sum;
    }
    free(local);
}

static double regularization = 0; // strength of the pull towards the starting weights (units: pawns), see --reg

static double reg_term(const double *w) {
    double sum = 0;
    for (int j = 0; j < NP; j++) {
        double d = (w[j] - P[j].init) / 100.0;
        sum += d * d;
    }
    return regularization * sum;
}

// Adam: gradient descent where every weight gets its own step size. m is a running average of the gradient, v of its
// square. A weight moves by about lr in the direction of the gradient, no matter how large the gradient is, which is
// important here: a pawn value and a rarely used piece square table entry have gradients that differ by orders of magnitude.
static void optimize_adam(double k, int iterations, double lr, double decay, int patience) {
    double *w = current_weights(), *grad = calloc(NP, sizeof(double)), *m = calloc(NP, sizeof(double)), *v = calloc(NP, sizeof(double));
    double *best = current_weights();
    double best_val = error_on(w, k, n_train, num_pos);
    int since_best = 0;
    printf("\n iteration   train error   validation error   learning rate\n");
    printf(" %9d   %.8f   %.8f\n", 0, error_on(w, k, 0, n_train), best_val);
    const double beta1 = 0.9, beta2 = 0.999, eps = 1e-9;
    clock_t start = clock();
    for (int t = 1; t <= iterations; t++) {
        gradient(w, k, grad);
        double rate = lr * pow(decay, t);
        for (int j = 0; j < NP; j++) {
            if (P[j].frozen) continue;
            double g = grad[j] + 2 * regularization * (w[j] - P[j].init) / 1e4;
            m[j] = beta1 * m[j] + (1 - beta1) * g;
            v[j] = beta2 * v[j] + (1 - beta2) * g * g;
            double m_hat = m[j] / (1 - pow(beta1, t)), v_hat = v[j] / (1 - pow(beta2, t));
            w[j] -= rate * m_hat / (sqrt(v_hat) + eps);
        }
        if (t % 20 == 0 || t == iterations) {
            double tr = error_on(w, k, 0, n_train), va = error_on(w, k, n_train, num_pos);
            printf(" %9d   %.8f   %.8f   %.4f   (%.0fs)\n", t, tr, va, rate, (double)(clock() - start) / CLOCKS_PER_SEC);
            fflush(stdout);
            if (va < best_val - 1e-9) {
                best_val = va;
                memcpy(best, w, NP * sizeof(double));
                since_best = 0;
            } else if (++since_best >= patience) {
                printf(" the validation error has not improved for %d checks: stopping (early stopping, avoids overfitting)\n", patience);
                break;
            }
        }
    }
    for (int j = 0; j < NP; j++) P[j].w = best[j];
    free(w); free(grad); free(m); free(v); free(best);
}

// The original Texel method: change one weight by one unit, keep the change if the error gets smaller, repeat.
// It needs the error of the whole training set for every single trial, so it is much slower, but it is simple, and
// it keeps all weights integers. Included for comparison.
static void optimize_local(double k, int sweeps) {
    double *w = current_weights();
    double best_err = error_on(w, k, 0, n_train) + reg_term(w);
    for (int sweep = 1; sweep <= sweeps; sweep++) {
        int changed = 0;
        for (int j = 0; j < NP; j++) {
            if (P[j].frozen) continue;
            for (int dir = 1; dir >= -1; dir -= 2) {
                w[j] += dir;
                double err = error_on(w, k, 0, n_train) + reg_term(w);
                if (err < best_err - 1e-12) {
                    best_err = err;
                    changed++;
                    break; // keep it (and do not try the other direction)
                }
                w[j] -= dir;
            }
        }
        double va = error_on(w, k, n_train, num_pos);
        printf(" sweep %2d: %d weights changed, training error %.8f, validation error %.8f\n", sweep, changed, best_err - reg_term(w), va);
        fflush(stdout);
        if (!changed) break;
    }
    for (int j = 0; j < NP; j++) P[j].w = w[j];
    free(w);
}

// the error of the real engine (evaluate() with the weights that are currently written into the engine's variables)
static double engine_error(double k, long val_limit) {
    double sum = 0;
    long n = 0;
    for (long i = 0; i < n_val && (val_limit <= 0 || i < val_limit); i++) {
        if (!setup_board(val_fen[i])) continue;
        double e = (double)evaluate(bitboards) * bitboards->color; // evaluate() is from the side to move's point of view
        double d = val_result[i] - sigmoid(k * e);
        sum += d * d;
        n++;
    }
    return sum / n;
}

static void write_weights_to_engine(const double *w) {
    for (int j = 0; j < NP; j++) *P[j].value = (int)lround(w[j]);
}

static void print_report(FILE *out) {
    fprintf(out, "// ---- tuned values. apply them with:  python3 tools/texel/apply.py eval.c %s ----\n\n", "<this file>");
    fprintf(out, "int material_values[6] = {%d, %d, %d, %d, %d, 0};\n\n", *P[idx_material[0]].value, *P[idx_material[1]].value,
            *P[idx_material[2]].value, *P[idx_material[3]].value, *P[idx_material[4]].value);
    for (int i = 0; i < NUM_TERMS; i++) {
        if (!strcmp(terms[i].cname, "eval_passed_pawn") || !strcmp(terms[i].cname, "eval_mobility")) continue;
        fprintf(out, "int %s = %d;   // was %d\n", terms[i].cname, *terms[i].value, P[terms[i].param].init);
    }
    fprintf(out, "\nint eval_passed_pawn[8] = {0, %d, %d, %d, %d, %d, %d, 0};\n", eval_passed_pawn[1], eval_passed_pawn[2], eval_passed_pawn[3],
            eval_passed_pawn[4], eval_passed_pawn[5], eval_passed_pawn[6]);
    fprintf(out, "int eval_mobility[4] = {%d, %d, %d, %d};\n", eval_mobility[0], eval_mobility[1], eval_mobility[2], eval_mobility[3]);
    fprintf(out, "\n// piece square tables (index 0 is h1, the first row of numbers is the first rank, as in eval.c)\n");
    for (int phase = 0; phase < 2; phase++) {
        for (int type = 0; type < 6; type++) {
            fprintf(out, "\nint %s_eval_white_%s_position[64] = {\n", piece_names[type], phase == OPENING ? "opening" : "endgame");
            for (int row = 0; row < 8; row++) {
                fprintf(out, "   ");
                for (int col = 0; col < 8; col++) {
                    fprintf(out, " %4d%s", white_position_tables[phase][type][row * 8 + col], row * 8 + col < 63 ? "," : "");
                }
                fprintf(out, "\n");
            }
            fprintf(out, "};\n");
        }
    }
}

static void self_test(double k) {
    printf("\n== self test ==\n");

    // 1. does the model reproduce the engine? constant = real evaluation - model, it should be tiny for most positions.
    //    exceptions: the king terms in endgames (only used when the game phase is exactly 1) and rounding
    long count[2] = {0, 0}, big[2] = {0, 0};
    double sum_abs[2] = {0, 0};
    for (long i = 0; i < num_pos; i++) {
        int pure = pos[i].phase >= 0.999f; // the engine's king terms are only used in pure endgames
        double c = fabs(pos[i].constant);
        count[pure]++;
        sum_abs[pure] += c;
        if (c > 3) big[pure]++;
    }
    printf("1. model versus real evaluation (constant = real evaluation - model):\n");
    printf("   positions that are not pure endgames: %ld, mean |constant| %.2f cp, %.2f%% above 3 cp\n", count[0], count[0] ? sum_abs[0] / count[0] : 0, count[0] ? 100.0 * big[0] / count[0] : 0);
    printf("   pure endgames (game phase 1):         %ld, mean |constant| %.2f cp, %.2f%% above 3 cp\n", count[1], count[1] ? sum_abs[1] / count[1] : 0, count[1] ? 100.0 * big[1] / count[1] : 0);
    printf("   (the first line must be tiny: only rounding. the second line is large because the engine's king terms for\n");
    printf("    pure endgames are not part of the linear model, they simply stay in the constant)\n");

    // 2. is the analytic gradient right? compare with the numerical derivative (error(w + h) - error(w - h)) / 2h
    double *w = current_weights(), *grad = calloc(NP, sizeof(double));
    gradient(w, k, grad);
    double worst = 0;
    int checked = 0;
    for (int j = 0; j < NP && checked < 12; j++) {
        if (fabs(grad[j]) < 1e-9) continue;
        double h = 0.05;
        w[j] += h;  double up = error_on(w, k, 0, n_train);
        w[j] -= 2 * h; double down = error_on(w, k, 0, n_train);
        w[j] += h;
        double numeric = (up - down) / (2 * h);
        double rel = fabs(numeric - grad[j]) / (fabs(grad[j]) + 1e-12);
        if (rel > worst) worst = rel;
        if (checked < 5) printf("   %-28s analytic %+.4e   numeric %+.4e\n", P[j].name, grad[j], numeric);
        checked++;
    }
    printf("2. gradient check on %d parameters: largest relative difference %.2e %s\n", checked, worst, worst < 1e-3 ? "(ok)" : "(PROBLEM)");
    free(w); free(grad);

    // 3. the real engine's error with the starting weights should be almost the same as the model's
    double model = error_on(current_weights(), k, n_train, num_pos);
    double real = engine_error(k, 0);
    printf("3. validation error, model %.8f versus real engine %.8f\n", model, real);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: tuner positions.csv [--selftest] [--method adam|local] [--iters N] [--lr X] [--decay X] [--patience N]\n"
               "             [--val-frac F] [--k K] [--min-count N] [--reg L] [--max-positions N] [--groups m,s,p] [--out file]\n");
        return 1;
    }
    const char *method = "adam", *out_path = "tuned_params.txt", *groups = "m,s,p";
    int iters = 600, patience = 10, min_count = 50, sweeps = 20;
    double lr = 1.0, decay = 0.995, val_fraction = 0.1, k_fixed = 0;
    long max_positions = 0;
    bool test = false;
    for (int a = 2; a < argc; a++) {
        if (!strcmp(argv[a], "--selftest")) test = true;
        else if (!strcmp(argv[a], "--method") && a + 1 < argc) method = argv[++a];
        else if (!strcmp(argv[a], "--iters") && a + 1 < argc) iters = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--lr") && a + 1 < argc) lr = atof(argv[++a]);
        else if (!strcmp(argv[a], "--decay") && a + 1 < argc) decay = atof(argv[++a]);
        else if (!strcmp(argv[a], "--patience") && a + 1 < argc) patience = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--val-frac") && a + 1 < argc) val_fraction = atof(argv[++a]);
        else if (!strcmp(argv[a], "--k") && a + 1 < argc) k_fixed = atof(argv[++a]);
        else if (!strcmp(argv[a], "--min-count") && a + 1 < argc) min_count = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--reg") && a + 1 < argc) regularization = atof(argv[++a]);
        else if (!strcmp(argv[a], "--max-positions") && a + 1 < argc) max_positions = atol(argv[++a]);
        else if (!strcmp(argv[a], "--groups") && a + 1 < argc) groups = argv[++a];
        else if (!strcmp(argv[a], "--sweeps") && a + 1 < argc) sweeps = atoi(argv[++a]);
        else if (!strcmp(argv[a], "--out") && a + 1 < argc) out_path = argv[++a];
        else { printf("unknown option %s\n", argv[a]); return 1; }
    }

    // initialization of the engine, as in main.c
    init_king_attacks(); init_knight_attacks(); init_sliding_piece_attacks(true); init_sliding_piece_attacks(false);
    init_squares_between(); init_zobrist(); init_tables(1); allocate_boards();

    build_params();
    printf("%d parameters\n", NP);
    load_positions(argv[1], max_positions, val_fraction);
    if (n_train == 0) { printf("no training positions\n"); return 1; }

    // parameters that are not in a selected group, or that occur in too few positions, are not changed
    for (long i = 0; i < n_train; i++) {
        for (uint32_t e = pos[i].start; e < pos[i].start + pos[i].count; e++) P[e_idx[e]].count++;
    }
    int tuned = 0, rare = 0;
    for (int j = 0; j < NP; j++) {
        if (!strchr(groups, P[j].group)) P[j].frozen = true;
        if (!P[j].frozen && P[j].count < min_count) { P[j].frozen = true; rare++; }
        if (!P[j].frozen) tuned++;
    }
    printf("%d parameters are tuned (%d were left alone because they occur in fewer than %d positions)\n", tuned, rare, min_count);

    double k = k_fixed > 0 ? k_fixed : fit_k();
    printf("sigmoid constant k = %.9f (in the usual notation K = %.3f)\n", k, k * 400 / log(10));

    double *w0 = current_weights();
    double train_before = error_on(w0, k, 0, n_train), val_before = error_on(w0, k, n_train, num_pos);
    printf("error with the current weights: training %.8f, validation %.8f\n", train_before, val_before);

    if (test) {
        self_test(k);
        return 0;
    }

    double engine_before = engine_error(k, 0); // the real engine, starting weights
    if (!strcmp(method, "local")) optimize_local(k, sweeps);
    else optimize_adam(k, iters, lr, decay, patience);

    double *w = current_weights();
    printf("\n== result ==\n");
    printf("model error:   training %.8f -> %.8f,   validation %.8f -> %.8f\n", train_before, error_on(w, k, 0, n_train), val_before, error_on(w, k, n_train, num_pos));
    write_weights_to_engine(w); // rounds the weights to integers and puts them into the engine
    double wr[MAX_PARAMS];
    for (int j = 0; j < NP; j++) wr[j] = *P[j].value;
    printf("rounded to integers: validation %.8f (model)\n", error_on(wr, k, n_train, num_pos));
    double engine_after = engine_error(k, 0);
    printf("REAL ENGINE:   validation %.8f -> %.8f   (this is the number that matters: the engine's own evaluate() on positions the optimizer never saw)\n", engine_before, engine_after);

    printf("\nchanges (name: old -> new):\n");
    for (int j = 0; j < NP; j++) {
        if (P[j].group == 'p') continue;
        if (*P[j].value != P[j].init) printf("  %-28s %5d -> %5d\n", P[j].name, P[j].init, *P[j].value);
    }
    long pst_changed = 0;
    for (int j = 0; j < NP; j++) if (P[j].group == 'p' && *P[j].value != P[j].init) pst_changed++;
    printf("  %ld piece square table entries changed\n", pst_changed);

    FILE *out = fopen(out_path, "w");
    if (out) {
        print_report(out);
        fclose(out);
        printf("\nwrote %s\n", out_path);
    }
    return 0;
}
