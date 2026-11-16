#include "bitboards.h"

// global bitboards
struct bitboards *bitboards;

// tables for hash updating
u64 ZOBRIST_TABLE[64][13]; 
u64 white_to_move;
u64 castling_rights[4];

void allocate_boards() {
    // allocate memory for the bitboards
    bitboards = malloc(sizeof(struct bitboards));
    reset_boards(bitboards);
}

void free_boards() {
    // free the memory allocated for the bitboards
    free(bitboards);
}

#if GOOD_ZOBRIST
// the old keys, for the opening book only
static u64 book_zobrist[64][13];
static u64 book_white_to_move;
static u64 book_castling[4];

static u64 splitmix64(u64 *state) {
    u64 z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
#endif

void init_zobrist() {
    // initialize the zobrist table with random 64 bit numbers
    // for each square and type of piece
    // an en passant square is also treated as a piece type
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 13; j++) {
            ZOBRIST_TABLE[i][j] = rand64();
        }
    }
    white_to_move = rand64();
    castling_rights[0] = rand64();
    castling_rights[1] = rand64();
    castling_rights[2] = rand64();
    castling_rights[3] = rand64();
#if GOOD_ZOBRIST
    // keep the keys above (the book was made with them) and use new ones for everything else
    memcpy(book_zobrist, ZOBRIST_TABLE, sizeof(book_zobrist));
    book_white_to_move = white_to_move;
    memcpy(book_castling, castling_rights, sizeof(book_castling));
    u64 state = 0x6B6F6C6D61746F31ULL;
    for (int i = 0; i < 64; i++) {
        for (int j = 0; j < 13; j++) {
            ZOBRIST_TABLE[i][j] = splitmix64(&state);
        }
    }
    white_to_move = splitmix64(&state);
    for (int i = 0; i < 4; i++) {
        castling_rights[i] = splitmix64(&state);
    }
#endif
}

u64 book_hash(const struct bitboards *BITBOARDS) {
#if GOOD_ZOBRIST
    // the same as init_board_hash() with the old keys
    u64 hash = 0;
    if (BITBOARDS->color == WHITE) {hash ^= book_white_to_move;}
    if (BITBOARDS->black_castle_king_side) {hash ^= book_castling[0];}
    if (BITBOARDS->black_castle_queen_side) {hash ^= book_castling[1];}
    if (BITBOARDS->white_castle_king_side) {hash ^= book_castling[2];}
    if (BITBOARDS->white_castle_queen_side) {hash ^= book_castling[3];}
    for (int i = 0; i < 64; i++) {
        if (BITBOARDS->en_passant_square == i) {
            hash ^= book_zobrist[i][12];
        }
        if (BITBOARDS->piece_list[i] != 0) {
            hash ^= book_zobrist[i][BITBOARDS->piece_list[i] > 0 ? BITBOARDS->piece_list[i] - 1 : -(BITBOARDS->piece_list[i]) + 5];
        }
    }
    return hash;
#else
    return BITBOARDS->hash;
#endif
}

u64 init_board_hash(struct bitboards *BITBOARDS, bool is_white_to_move) {
    // initialize the board hash for the starting position
    // using xor makes it easy to update the hash when making a move and
    // if a move was to be undone, the hash would return back to the original
    // collisions can happen, but they are very rare
    u64 hash = 0;
    if (is_white_to_move) {hash ^= white_to_move;}
    if (BITBOARDS->black_castle_king_side) {hash ^= castling_rights[0];}
    if (BITBOARDS->black_castle_queen_side) {hash ^= castling_rights[1];}
    if (BITBOARDS->white_castle_king_side) {hash ^= castling_rights[2];}
    if (BITBOARDS->white_castle_queen_side) {hash ^= castling_rights[3];}
    for (int i = 0; i < 64; i++) {
        if (BITBOARDS->en_passant_square == i) {
            hash ^= ZOBRIST_TABLE[i][12];
        }
        if (BITBOARDS->piece_list[i] != 0) {
            hash ^= ZOBRIST_TABLE[i][BITBOARDS->piece_list[i] > 0 ? BITBOARDS->piece_list[i] - 1 : -(BITBOARDS->piece_list[i]) + 5];
        }
    }
    return hash;
}

bool string_contains_char(char* string, char c) {
    return strchr(string, c) != NULL;
}

void init_boards(struct bitboards *BITBOARDS, bool is_white_to_move, char* castle, char* en_pas, int fifty_move, int move_num) {
    // set up the internal board and game counters
    // the BITBOARDS are used for a very fast move generation
    // the piece list is useful for determening which piece is on a given square since that
    // would require to check that bit in every bitboard resulting in long if chains or a switch
    // the list is mainly to speed up the move ordering
    // the evaluations are also kept within the board to avoid having to call an extra function to evaluate a position
    // they are updated when making a move
    half_move_count = fifty_move;
    full_move_count = is_white_to_move ? move_num * 2 - 1 : move_num * 2;

    int piece;

    for (int i = 0; i < 64; i++) {
        piece = BITBOARDS->piece_list[i];
        if (piece != 0) {
            int index = piece > 0 ? piece - 1 : -(piece) + 5;
            set_bit(BITBOARDS->bits[all_pieces], i);
            if (piece < 0) {
                set_bit(BITBOARDS->bits[black_pieces], i);
                BITBOARDS->black_position_opening -= white_position_tables[OPENING][index - 6][i ^ 56];
                BITBOARDS->black_position_endgame -= white_position_tables[ENDGAME][index - 6][i ^ 56];
                BITBOARDS->black_material -= material_values[index - 6];

            } else {
                set_bit(BITBOARDS->bits[white_pieces], i);
                BITBOARDS->white_position_opening += white_position_tables[OPENING][index][i];
                BITBOARDS->white_position_endgame += white_position_tables[ENDGAME][index][i];
                BITBOARDS->white_material += material_values[index];
            }
            set_bit(BITBOARDS->bits[index], i);
        }
    }

    if (string_contains_char(castle, 'K')) {
        BITBOARDS->white_castle_king_side = true;
    }
    if (string_contains_char(castle, 'Q')) {
        BITBOARDS->white_castle_queen_side = true;
    }
    if (string_contains_char(castle, 'k')) {
        BITBOARDS->black_castle_king_side = true;
    }
    if (string_contains_char(castle, 'q')) {
        BITBOARDS->black_castle_queen_side = true;
    }

    if (en_pas[0] != '-') {
        BITBOARDS->en_passant_square = 7 - (en_pas[0] - 'a') + (en_pas[1] - '1') * 8;
    }
    
    BITBOARDS->hash = init_board_hash(BITBOARDS, is_white_to_move);
    BITBOARDS->color = is_white_to_move ? WHITE : BLACK;
}

void init_boards_light(struct bitboards *BITBOARDS, bool is_white_to_move, char* castle, char* en_pas) {

    int piece;

    for (int i = 0; i < 64; i++) {
        piece = BITBOARDS->piece_list[i];
        
        if (piece != 0) {
                set_bit(BITBOARDS->bits[all_pieces], i);
            if (piece < 0) {
                set_bit(BITBOARDS->bits[black_pieces], i);
            } else {
                set_bit(BITBOARDS->bits[white_pieces], i);
            }
            set_bit(BITBOARDS->bits[piece > 0 ? piece - 1 : -(piece) + 5], i);
        }
    }

    if (string_contains_char(castle, 'K')) {
        BITBOARDS->white_castle_king_side = true;
    }
    if (string_contains_char(castle, 'Q')) {
        BITBOARDS->white_castle_queen_side = true;
    }
    if (string_contains_char(castle, 'k')) {
        BITBOARDS->black_castle_king_side = true;
    }
    if (string_contains_char(castle, 'q')) {
        BITBOARDS->black_castle_queen_side = true;
    }

    if (en_pas[0] != '-') {
        BITBOARDS->en_passant_square |= (1ULL << (7 - (en_pas[0] - 'a') + (en_pas[1] - '1') * 8));
    } else {
        BITBOARDS->en_passant_square = -1;
    }
    
    BITBOARDS->hash = init_board_hash(BITBOARDS, is_white_to_move);
    BITBOARDS->color = is_white_to_move ? WHITE : BLACK;
}

void reset_boards(struct bitboards *BITBOARDS) {
    // used for parsing the opening book
    memset(BITBOARDS->bits, 0, sizeof(BITBOARDS->bits));
    BITBOARDS->white_castle_queen_side = false;
    BITBOARDS->white_castle_king_side = false;
    BITBOARDS->black_castle_queen_side = false;
    BITBOARDS->black_castle_king_side = false;
    BITBOARDS->en_passant_square = -1;
    BITBOARDS->hash = 0;
    BITBOARDS->color = 0;
    memset(BITBOARDS->piece_list, 0, sizeof(BITBOARDS->piece_list));
    BITBOARDS->white_position_opening = 0;
    BITBOARDS->black_position_opening = 0;
    BITBOARDS->white_position_endgame = 0;
    BITBOARDS->black_position_endgame = 0;
    BITBOARDS->white_material = 0;
    BITBOARDS->black_material = 0;
    memset(BITBOARDS->pinner_for_square, 0, sizeof(BITBOARDS->pinner_for_square));
}

void reset_boards_light(struct bitboards *BITBOARDS) {
    memset(BITBOARDS->bits, 0, sizeof(BITBOARDS->bits));
    BITBOARDS->white_castle_queen_side = false;
    BITBOARDS->white_castle_king_side = false;
    BITBOARDS->black_castle_queen_side = false;
    BITBOARDS->black_castle_king_side = false;
    BITBOARDS->en_passant_square = -1;
    BITBOARDS->hash = 0;
    BITBOARDS->color = 0;
    memset(BITBOARDS->piece_list, 0, sizeof(BITBOARDS->piece_list));
    memset(BITBOARDS->pinner_for_square, 0, sizeof(BITBOARDS->pinner_for_square));
}
