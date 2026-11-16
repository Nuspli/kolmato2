#include "check.h"

// the enemy pieces that attack the king of the color `white` (a compile time constant: the functions are always_inline and called with
// literals, so there is one source for both colors and no runtime color test). pretend the king was every other piece:
// if that piece which is on the kings position can capture a piece of its kind from there, that enemy piece can also capture the king
static inline __attribute__((always_inline)) u64 attackers_of_king(const bool white, const struct bitboards *BB) {
    const int own = white ? 0 : 6;
    const int enemy = white ? 6 : 0;
    const u64 king = BB->bits[white_king + own];
    const int king_index = lsb(king);
    // the squares the enemy pawns attack the king from: white pawns capture upwards, so they stand below the king and black pawns above it
    const u64 pawn_squares = white ? ((king << 7) & not_leftmost_file_mask) | ((king << 9) & not_rightmost_file_mask)
                                  : ((king >> 7) & not_rightmost_file_mask) | ((king >> 9) & not_leftmost_file_mask);

    return (king_attacks[king_index] & BB->bits[white_king + enemy]) |
           (knight_attacks[king_index] & BB->bits[white_knights + enemy]) |
           (generate_bishop_moves(king_index, BB->bits[all_pieces]) & (BB->bits[white_bishops + enemy] | BB->bits[white_queens + enemy])) |
           (generate_rook_moves(king_index, BB->bits[all_pieces]) & (BB->bits[white_rooks + enemy] | BB->bits[white_queens + enemy])) |
           (pawn_squares & BB->bits[white_pawns + enemy]);
}

// as this is only used after a move was played to check its validity, if white is to move
// white is the opponent and this checks if white can capture the black king
bool can_capture_opponents_king(struct bitboards *BITBOARDS) {
    return (BITBOARDS->color == WHITE ? attackers_of_king(false, BITBOARDS) : attackers_of_king(true, BITBOARDS)) != 0;
}

// is the side to move in check, used on the board before making a move (mainly to see if a player can castle or not)
bool is_in_check(struct bitboards *BITBOARDS, u64 *checkers) {
    *checkers = BITBOARDS->color == WHITE ? attackers_of_king(true, BITBOARDS) : attackers_of_king(false, BITBOARDS);
    return *checkers != 0;
}

bool is_in_check_light(struct bitboards *BITBOARDS) {
    // in case we dont want to pass a checkers pointer
    return (BITBOARDS->color == WHITE ? attackers_of_king(true, BITBOARDS) : attackers_of_king(false, BITBOARDS)) != 0;
}
