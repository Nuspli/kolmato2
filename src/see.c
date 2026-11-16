#include "search.h"

// all pieces (of both colors) that attack a square, given an occupancy (which allows sliding attackers
// that were hiding behind another piece to be found after that piece was removed)
u64 attackers_to(const struct bitboards *BB, int sq, u64 occ) {
    u64 sq_bit = 1ULL << sq;
    u64 white_pawn_attackers = (((sq_bit & not_leftmost_file_mask) >> 7) | ((sq_bit & not_rightmost_file_mask) >> 9)) & BB->bits[white_pawns];
    u64 black_pawn_attackers = (((sq_bit & not_rightmost_file_mask) << 7) | ((sq_bit & not_leftmost_file_mask) << 9)) & BB->bits[black_pawns];
    return white_pawn_attackers | black_pawn_attackers |
        (knight_attacks[sq] & (BB->bits[white_knights] | BB->bits[black_knights])) |
        (king_attacks[sq] & (BB->bits[white_king] | BB->bits[black_king])) |
        (generate_bishop_moves(sq, occ) & (BB->bits[white_bishops] | BB->bits[black_bishops] | BB->bits[white_queens] | BB->bits[black_queens])) |
        (generate_rook_moves(sq, occ) & (BB->bits[white_rooks] | BB->bits[black_rooks] | BB->bits[white_queens] | BB->bits[black_queens]));
}

/*
static exchange evaluation (SEE): the material that the side making a capture wins or loses if both sides keep
capturing on that square. a capture that takes a defended pawn with a queen looks fine at first, but the pawn recaptures
and the queen is lost. the exchange is played out on the one square without searching: both sides always capture with
their least valuable piece, and each side may stop whenever continuing would lose material. gain[d] is the material balance
after the d-th capture assuming the capturing side stops; at the end the values are folded back so that each side picks
the better of "stop" and "continue". a negative result means the capture loses material.
pins are ignored (a pinned piece is counted as able to capture). not meant for en passant and promotions
*/
int see(const struct bitboards *BB, move_t move) {

    // the same piece values as the evaluation (so that "winning a knight for a bishop" means the same everywhere),
    // and a huge value for the king so that it is never "captured" in the exchange
    int see_values[6] = {material_values[0], material_values[1], material_values[2], material_values[3], material_values[4], 20000};

    int from = m_from(move);
    int to = m_to(move);
    int mover = BB->piece_list[from];
    int victim = BB->piece_list[to];

    int gain[40];
    int d = 0;

    gain[0] = victim ? see_values[(victim < 0 ? -victim : victim) - 1] : 0;

    u64 occ = BB->bits[all_pieces];
    u64 attackers = attackers_to(BB, to, occ);
    u64 bishops_queens = BB->bits[white_bishops] | BB->bits[black_bishops] | BB->bits[white_queens] | BB->bits[black_queens];
    u64 rooks_queens = BB->bits[white_rooks] | BB->bits[black_rooks] | BB->bits[white_queens] | BB->bits[black_queens];

    int piece_on_square = (mover < 0 ? -mover : mover) - 1; // the piece that is currently standing on the target square
    u64 from_bit = 1ULL << from;
    int side = mover > 0 ? 0 : 6; // offset of the side that made the last capture (0 white, 6 black)

    while (true) {
        d++;
        gain[d] = see_values[piece_on_square] - gain[d - 1];

        // the piece that captured is now on the target square, so it is no longer an attacker that can be used
        attackers &= ~from_bit;
        occ &= ~from_bit;
        // pieces behind it may now be able to see the square
        attackers |= (generate_bishop_moves(to, occ) & bishops_queens) | (generate_rook_moves(to, occ) & rooks_queens);
        attackers &= occ;

        // the other side recaptures with its least valuable attacker
        side = 6 - side;
        u64 mine = attackers;
        u64 found = 0;
        int found_type = 0;
        for (int type = 0; type < 6; type++) {
            found = mine & BB->bits[white_pawns + side + type];
            if (found) {
                found_type = type;
                break;
            }
        }
        if (!found) {
            break;
        }

        piece_on_square = found_type;
        from_bit = found & -found;
    }

    // the sides can stop capturing whenever it is no longer profitable
    while (--d) {
        gain[d - 1] = -((-gain[d - 1] > gain[d]) ? -gain[d - 1] : gain[d]);
    }

    return gain[0];
}
