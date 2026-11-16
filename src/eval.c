#include "eval.h"

// each piece is worth a different amount of material depending on the square it is on and the phase of the game
// blacks pieces are negative as it is the minimizing player

int material_values[6] = {90, 351, 370, 548, 1117, 0};

// weights of the other evaluation terms, see eval.h
int eval_bishop_blocked_by_pawn = 5;
int eval_doubled_pawn = 3;
int eval_isolated_pawn = 10;
int eval_blocked_pawn = 8;
int eval_passed_pawn[8] = {0, 14, 17, 48, 90, 148, 164, 0};
int eval_rook_open_file = 19;
int eval_rook_semi_open_file = 14;
int eval_bishop_pair = 40;
int eval_mobility[4] = {7, 6, 4, 3};
int eval_supported_pawn = 16;
int eval_phalanx_pawn = 6;
int eval_backward_pawn = 9;
int eval_protected_passer = 4;
int eval_passer_king = 5;
int eval_king_shield = 6;
int eval_king_open_file = 21;
int eval_king_attack_knight = 14;
int eval_king_attack_bishop = 10;
int eval_king_attack_rook = 15;
int eval_king_attack_queen = 9;
int eval_pawn_attacks_minor = 49;
int eval_pawn_attacks_major = 37;
int eval_knight_outpost = 22;
int eval_rook_seventh = 19;
int eval_tempo = 10;
int eval_mop_up_edge = 15;
int eval_mop_up_corner = 25;

#ifdef TUNE
struct eval_trace eval_trace;
#endif

int pawn_eval_white_opening_position[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
     -14,   18,   15,  -12,  -18,  -10,  -11,  -22,
     -11,    8,  -11,   -3,  -11,   -8,  -14,  -16,
     -11,   -4,    9,   18,   15,    7,  -10,  -13,
      -5,   12,   26,   25,   22,    8,   10,    2,
      37,   41,   54,   56,   49,   31,   28,   18,
      64,    1,   74,  102,  135,   96,  136,  137,
       0,    0,    0,    0,    0,    0,    0,    0
};

int pawn_eval_white_endgame_position[64] = {
       0,    0,    0,    0,    0,    0,    0,    0,
       3,    1,    9,   18,    9,   10,    8,   18,
       0,   -7,    3,    4,    4,   -3,    6,   12,
       6,    6,    0,   -4,   -7,    0,   15,   20,
      21,   15,    3,    1,    1,    8,   20,   35,
      36,   27,   15,   17,   10,   23,   35,   47,
      79,   90,   76,   79,   80,   83,   96,   89,
       0,    0,    0,    0,    0,    0,    0,    0
};

int knight_eval_white_opening_position[64] = {
     -11,  -11,   -6,   -1,   -6,  -19,  -21,  -40,
       9,   20,   15,   17,   12,    4,   -8,  -24,
      -4,   31,   27,   29,   22,    9,    6,  -21,
      23,   30,   30,   32,   28,   23,   12,    7,
      43,   28,   61,   20,   41,   32,   17,   26,
      24,   42,   76,   75,   59,   39,   39,   11,
      12,   16,   85,   45,   72,   46,    0,    0,
     -54,  -68, -179,   14,   39,  -16,  -75, -182
};

int knight_eval_white_endgame_position[64] = {
     -53,  -36,  -14,  -26,  -20,  -18,  -54,  -46,
     -25,  -20,  -25,  -10,  -11,  -20,  -19,  -43,
     -27,  -13,  -15,   -2,    2,  -10,  -17,  -36,
     -30,   -5,    0,    7,    5,   -3,   -8,  -27,
     -22,    0,   -5,   11,    9,   -3,  -11,  -16,
     -24,  -16,  -11,   -5,   -2,   -1,  -15,  -18,
     -54,  -11,  -24,  -21,   -7,  -13,  -16,  -38,
    -116,  -64,  -33,  -14,  -13,  -21,  -32,  -68
};

int bishop_eval_white_opening_position[64] = {
       5,  -25,    1,   22,   15,   12,   12,   17,
       9,   44,   40,   33,   23,   35,   32,   28,
      17,   33,   44,   27,   32,   34,   38,   19,
      26,   29,   21,   48,   52,   32,   32,   21,
      22,   35,   37,   45,   61,   40,   40,   25,
      35,   58,   76,   61,   52,   31,   40,   28,
      -7,   -1,   18,   11,   38,   29,   15,   -7,
      -8,  -46, -151,    8,  -18,    9,   21,   25
};

int bishop_eval_white_endgame_position[64] = {
      -5,    1,    5,   -1,    1,   -8,    4,  -26,
      -9,   -5,    1,    0,    9,    0,   -4,    4,
       7,    8,    4,   18,   15,   10,    9,    0,
      -1,    7,   14,    7,   15,   19,   11,    5,
      -2,   12,   14,   14,   12,   12,   12,    1,
      -9,   20,   19,   15,   10,   17,   15,   -2,
      -7,   14,   20,   14,   12,   12,   12,    5,
       1,   12,    9,   19,    8,   10,   13,  -16
};

int rook_eval_white_opening_position[64] = {
     -46,  -44,    4,    5,    1,  -12,  -20,  -28,
     -93,  -19,  -17,  -19,  -24,  -29,  -39,  -50,
     -44,  -24,  -21,  -22,  -31,  -36,  -38,  -45,
     -44,  -21,  -20,  -18,  -14,  -20,  -30,  -36,
     -23,  -34,    7,    8,    0,   -4,  -17,  -25,
     -15,    3,   53,   33,   30,   16,   14,  -16,
       6,  -26,   41,   34,   31,   14,   -3,   -6,
      65,   45,  -11,   33,   34,   47,   43,   37
};

int rook_eval_white_endgame_position[64] = {
      44,   57,   37,   32,   38,   48,   49,   54,
      46,   46,   41,   44,   47,   54,   48,   51,
      51,   59,   57,   54,   60,   63,   61,   58,
      61,   59,   67,   63,   65,   68,   62,   67,
      66,   75,   68,   62,   70,   73,   73,   75,
      71,   74,   68,   66,   67,   69,   68,   74,
      61,   67,   61,   62,   58,   59,   59,   59,
      72,   82,   81,   69,   69,   70,   71,   70
};

int queen_eval_white_opening_position[64] = {
      27,  -30,   -6,   28,   45,   43,   18,   31,
      -2,   55,   52,   49,   46,   44,   36,   21,
      28,   41,   41,   29,   32,   35,   33,   12,
      28,   37,   32,   33,   29,   34,   19,   24,
      42,   31,   30,   33,   22,   27,   17,   11,
      38,   51,   75,   43,   41,   12,   22,   17,
      75,   36,   45,   39,   45,   34,   -8,   14,
      35,   33,   44,   57,   48,   39,   43,  -26
};

int queen_eval_white_endgame_position[64] = {
      35,   39,   33,   46,   -1,   13,   49,   45,
      83,  -16,   24,   42,   47,   44,   48,   84,
      97,  117,  105,  110,   83,   88,   69,   98,
     126,  139,  126,  134,  135,  125,  111,   94,
     126,  152,  139,  153,  152,  121,  138,  115,
     135,  156,  147,  150,  136,  148,  121,   98,
     113,  144,  156,  138,  145,  130,  143,  115,
     145,  135,  132,  133,  129,  123,  130,  152
};

int king_eval_white_opening_position[64] = {
      14,   29,  -26,   12,  -37,   32,   34,   -5,
      14,   22,    7,  -20,  -13,   14,   20,   35,
     -41,    9,  -23,  -29,  -20,  -10,   12,  -15,
     -81,  -14,  -52,  -24,  -55,  -42,    1,  -92,
     -77,   88,    1,   37,   -5,    5,  -35,  -80,
      38,   85,  -10,   66,   83,   18,   17,   65,
     -39,  221,  100,  127,  136,  107,  -70,  144,
     109,  -36,  -18,  -81,  -36,   55,   66,  -27
};

int king_eval_white_endgame_position[64] = {
     -32,  -15,  -13,  -23,  -14,  -15,  -23,  -24,
      -3,    3,    2,    1,    1,   -1,   -1,  -12,
       4,    2,    4,    5,    2,    0,   -7,   -8,
      -9,   -4,    0,    0,   -1,   -6,   -8,  -15,
      -3,    2,   -9,   -3,   -5,  -12,   -5,  -24,
       4,   24,   15,   -1,    0,    3,    9,  -19,
     -16,   21,    7,    2,    6,    6,    3,  -35,
    -138,  -29,   -9,    4,  -17,    6,  -54, -124
};

int *white_position_tables[2][6] = {
    {pawn_eval_white_opening_position, knight_eval_white_opening_position, bishop_eval_white_opening_position, rook_eval_white_opening_position, queen_eval_white_opening_position, king_eval_white_opening_position},
    {pawn_eval_white_endgame_position, knight_eval_white_endgame_position, bishop_eval_white_endgame_position, rook_eval_white_endgame_position, queen_eval_white_endgame_position, king_eval_white_endgame_position}
};
