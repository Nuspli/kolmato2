#ifndef EVAL_H
#define EVAL_H

#define OPENING 0
#define ENDGAME 1
#define MIDGAME 2

#define PAWNVALUE 90
#define KNIGHTVALUE 310
#define BISHOPVALUE 320
#define ROOKVALUE 490
#define QUEENVALUE 880

extern int material_values[6];

extern int pawn_eval_white_opening_position[64];
extern int rook_eval_white_opening_position[64];
extern int knight_eval_white_opening_position[64];
extern int bishop_eval_white_opening_position[64];
extern int queen_eval_white_opening_position[64];
extern int king_eval_white_opening_position[64];

extern int pawn_eval_white_endgame_position[64];
extern int rook_eval_white_endgame_position[64];
extern int knight_eval_white_endgame_position[64];
extern int bishop_eval_white_endgame_position[64];
extern int queen_eval_white_endgame_position[64];
extern int king_eval_white_endgame_position[64];

extern int *white_position_tables[2][6];

// weights of the evaluation terms (besides the material values and the piece square tables above).
// they are variables instead of constants so that the texel tuner (tools/texel) can change them.
// the penalties are positive numbers that are subtracted from the evaluation
extern int eval_bishop_blocked_by_pawn; // per own pawn that stands on the color of a bishop
extern int eval_doubled_pawn;
extern int eval_isolated_pawn;
extern int eval_blocked_pawn;
extern int eval_passed_pawn[8]; // bonus by rank of the passed pawn (from the point of view of its owner)
extern int eval_rook_open_file;
extern int eval_rook_semi_open_file;
extern int eval_bishop_pair;
extern int eval_mobility[4]; // per square: knight, bishop, rook, queen

// further terms (see eval_extra in search.c). all weights are in centipawns
extern int eval_supported_pawn;         // pawn defended by a pawn
extern int eval_phalanx_pawn;           // pawn with a pawn next to it on the same rank
extern int eval_backward_pawn;          // penalty: can not be supported and its stop square is attacked by an enemy pawn
extern int eval_protected_passer;       // passed pawn defended by a pawn
extern int eval_passer_king;            // endgame: per (rank of the passer * (distance of the enemy king - distance of the own king) to its stop square)
extern int eval_king_shield;            // midgame: per own pawn in the two ranks in front of the king (three files)
extern int eval_king_open_file;         // midgame penalty: per file next to the king (and its own) without an own pawn
extern int eval_king_attack_knight;     // midgame penalty: per square of the king zone that an enemy knight attacks (only if the enemy has a queen)
extern int eval_king_attack_bishop;
extern int eval_king_attack_rook;
extern int eval_king_attack_queen;
extern int eval_pawn_attacks_minor;     // penalty: own knight or bishop attacked by an enemy pawn
extern int eval_pawn_attacks_major;     // penalty: own rook or queen attacked by an enemy pawn
extern int eval_knight_outpost;         // knight on the 4th to 6th rank, defended by a pawn and not attackable by enemy pawns
extern int eval_rook_seventh;           // rook on the 7th rank with the enemy king on the 8th or enemy pawns on the 7th
extern int eval_tempo; // bonus for the side to move (not part of the tuner, it is not a white/black term)

// checkmating a lone king (not tuned by the texel tuner: such positions are too rare in game data, the values are set by hand)
extern int eval_mop_up_edge;            // per step that the lone king is pushed away from the center
extern int eval_mop_up_corner;          // bishop + knight against a lone king: per step closer to a corner of the bishop's color

// the terms of the evaluation that are counted (how often each one occurs for white and for black)
enum {
    F_BISHOP_BLOCKED, F_DOUBLED, F_ISOLATED, F_BLOCKED,
    F_PASSED_0, F_PASSED_1, F_PASSED_2, F_PASSED_3, F_PASSED_4, F_PASSED_5, F_PASSED_6, F_PASSED_7,
    F_ROOK_OPEN, F_ROOK_SEMI, F_BISHOP_PAIR,
    F_MOBILITY_KNIGHT, F_MOBILITY_BISHOP, F_MOBILITY_ROOK, F_MOBILITY_QUEEN,
    F_SUPPORTED, F_PHALANX, F_BACKWARD, F_PROTECTED_PASSER, F_PASSER_KING,
    F_KING_SHIELD, F_KING_OPEN, F_KING_ATT_KNIGHT, F_KING_ATT_BISHOP, F_KING_ATT_ROOK, F_KING_ATT_QUEEN,
    F_PAWN_ATT_MINOR, F_PAWN_ATT_MAJOR, F_OUTPOST, F_ROOK_SEVENTH,
    NUM_FEATURES
};

#ifdef TUNE
// only in the build of the texel tuner: evaluate() writes down which terms occurred (see tools/texel/README.md).
// in the normal engine this does not exist and the TRACE macros in evaluate() compile to nothing
#include <stdbool.h>
struct eval_trace {
    int white[NUM_FEATURES];
    int black[NUM_FEATURES];
    int eval_white; // the evaluation of both sides right before the final scaling
    int eval_black;
    bool valid; // false if evaluate() returned early (for example a draw by material)
};
extern struct eval_trace eval_trace;
#endif

#endif