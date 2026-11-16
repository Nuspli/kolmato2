#include "print.h"

void print_logo() {                                                                          
    puts(
        "                                                                                       ,,,,\n"
        " ,dPYb,                  ,dPYb,                                  I8                 ad88PP88b,\n"
        " IP'`Yb                  IP'`Yb                                  I8                d8\"     \"88\n"
        " I8  8I                  I8  8I                               88888888                     d8P\n"
        " I8  8bgg,               I8  8'                                  I8                       a8P \n"
        " I8 dP\" \"8    ,ggggg,    I8 dP   ,ggg,,ggg,,ggg,     ,gggg,gg    I8      ,ggggg,        ,d8P  \n"
        " I8d8bggP\"   dP\"  \"Y8ggggI8dP   ,8\" \"8P\" \"8P\" \"8,   dP\"  \"Y8I    I8     dP\"  \"Y8,     ,d8P'   \n"
        " I8P' \"Yb,  i8'    ,8I   I8P    I8   8I   8I   8I  i8'    ,8I   ,I8,   i8'    ,8I   ,d8P'     \n"
        ",d8    `Yb,,d8,   ,d8'  ,d8b,_ ,dP   8I   8I   Yb,,d8,   ,d8b, ,d88b, ,d8,   ,d8'  a88\"       \n"
        "88P      Y8P\"Y8888P\"    8P'\"Y888P'   8I   8I   `Y8P\"Y8888P\"`Y888P\"\"Y88P\"Y8888P\"    88888888888\n"
        "\n"
    );
}

void print_help() {
    puts(
        "About:\n"
        "  kolmato2 is the successor of kolmato, a chess engine that started in early 2023 as part of my first web dev project.\n"
        "  You can find the old JS version at https://nuspli.github.io/chess/chess.html\n"
        "  The source code of kolmato2 is at https://github.com/nuspli/kolmato2, the original kolmato (no longer maintained) at https://github.com/nuspli/kolmato.\n"
        "  If you are looking for more sources to develop your own chess engine, I recommend the following:\n"
        "    https://www.chessprogramming.org\n"
        "    https://www.cs.cmu.edu/afs/cs/academic/class/15418-s12/www/competition/www.contrib.andrew.cmu.edu/~jvirdo/rasmussen-2004.pdf\n"
        "\n"
        "Options:\n"
        "  uci                                       - Start in uci mode (also the default when the input is not a terminal).\n"
        "\n"
        "  -h, --help                                - Print this help message.\n"
        "  -e, --evaluate [FEN_STRING | startpos]    - Get a quick evaluation of a given position.\n"
        "  -b, --best     [FEN_STRING | startpos]    - Calculate the best move for a given position\n"
        "                 -t, --time NUMBER          - Set the time limit in seconds for the engine to think. Accepts decimals.\n"
        "                 -d, --depth NUMBER         - Set the depth limit for the engine to think.\n"
        "                 -b, --book FILE_NAME       - Use the opening book. Must be in the correct format.\n"
        "  -g, --game     [FEN_STRING | startpos]    - play against the engine interactively\n"
        "                 -t, --time NUMBER          - Set the time limit in seconds for the engine to think. Accepts decimals.\n"
        "                 -d, --depth NUMBER         - Set the depth limit for the engine to think.\n"
        "                 -b, --book FILE_NAME       - Use the opening book. Must be in the correct format.\n"
        "  -p, --perft    [FEN_STRING | startpos]    - Bulk count of leaf nodes for a given position\n\n"
        "FEN_STRING                                    enter in this format (the fields are separate arguments, the last two are optional):\n"
        "                                              rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\n"
        "startpos                                      the starting position of chess, instead of a FEN_STRING\n"
        "DEFAULTS                                      max time: 10 seconds\n"
        "                                              max depth: 100 ply\n"
    );
}

const char *promo_suffix[8] = {"", "n", "b", "r", "q", "", "", ""};
char notation[64][3] = {
    // to translate a move from number to notation
    "h1", "g1", "f1", "e1", "d1", "c1", "b1", "a1",
    "h2", "g2", "f2", "e2", "d2", "c2", "b2", "a2",
    "h3", "g3", "f3", "e3", "d3", "c3", "b3", "a3",
    "h4", "g4", "f4", "e4", "d4", "c4", "b4", "a4",
    "h5", "g5", "f5", "e5", "d5", "c5", "b5", "a5",
    "h6", "g6", "f6", "e6", "d6", "c6", "b6", "a6",
    "h7", "g7", "f7", "e7", "d7", "c7", "b7", "a7",
    "h8", "g8", "f8", "e8", "d8", "c8", "b8", "a8"
};

void print_binary(u64 x) {
    // print the binary representation of a 64 bit integer
    for (int i = 63; i >= 0; i--) {
        u64 mask = 1ULL << i; // Create a mask with the i-th bit set to 1
        int bit = (x & mask) ? 1 : 0; // Extract the i-th bit from n

        printf("%d", bit);
    }
    printf("\n");
}

void print_board(struct bitboards *BITBOARDS) {
    // print the current board state in a readable format
    char board[8][8] = {0};
    
    for (int i = 63; i >= 0; i--) {
        int row = i / 8;
        int col = i % 8;
        
        if (check_bit(BITBOARDS->bits[white_pawns], i)) {
            board[7 - row][7 - col] = 'P';
        } else if (check_bit(BITBOARDS->bits[black_pawns], i)) {
            board[7 - row][7 - col] = 'p';
        } else if (check_bit(BITBOARDS->bits[white_knights], i)) {
            board[7 - row][7 - col] = 'N';
        } else if (check_bit(BITBOARDS->bits[black_knights], i)) {
            board[7 - row][7 - col] = 'n';
        } else if (check_bit(BITBOARDS->bits[white_bishops], i)) {
            board[7 - row][7 - col] = 'B';
        } else if (check_bit(BITBOARDS->bits[black_bishops], i)) {
            board[7 - row][7 - col] = 'b';
        } else if (check_bit(BITBOARDS->bits[white_rooks], i)) {
            board[7 - row][7 - col] = 'R';
        } else if (check_bit(BITBOARDS->bits[black_rooks], i)) {
            board[7 - row][7 - col] = 'r';
        } else if (check_bit(BITBOARDS->bits[white_queens], i)) {
            board[7 - row][7 - col] = 'Q';
        } else if (check_bit(BITBOARDS->bits[black_queens], i)) {
            board[7 - row][7 - col] = 'q';
        } else if (check_bit(BITBOARDS->bits[white_king], i)) {
            board[7 - row][7 - col] = 'K';
        } else if (check_bit(BITBOARDS->bits[black_king], i)) {
            board[7 - row][7 - col] = 'k';
        } else {
            board[7 - row][7 - col] = ' ';
        }
    }
    
    // print the board
    printf("\n");
    for (int i = 0; i < 8; i++) {
        printf("    +---+---+---+---+---+---+---+---+\n");
        printf("  %d ", 8 - i);
        for (int j = 0; j < 8; j++) {
            printf("| %c ", board[i][j]);
        }
        printf("|\n");
    }
    printf("    +---+---+---+---+---+---+---+---+\n");
    printf("      a   b   c   d   e   f   g   h  \n");
    printf("\n");

    /*
    
    +---+---+---+---+---+---+---+---+
  8 | r |   |   |   | k |   |   | r |
    +---+---+---+---+---+---+---+---+
  7 | p |   | p | p | q | p | b |   |
    +---+---+---+---+---+---+---+---+
  6 | B | n |   |   | p | n | p |   |
    +---+---+---+---+---+---+---+---+
  5 |   |   |   | P | N |   |   |   |
    +---+---+---+---+---+---+---+---+
  4 |   | p |   |   | P |   |   |   |
    +---+---+---+---+---+---+---+---+
  3 |   |   | N |   |   | Q |   | p |
    +---+---+---+---+---+---+---+---+
  2 | P | P | P | B |   | P | P | P |
    +---+---+---+---+---+---+---+---+
  1 | R |   |   |   | K |   |   | R |
    +---+---+---+---+---+---+---+---+
      a   b   c   d   e   f   g   h

    */

    // print the FEN notation

    for (int i = 63; i >= 0; i--) {
        if (check_bit(BITBOARDS->bits[white_pawns], i)) {
            printf("P");
        } else if (check_bit(BITBOARDS->bits[black_pawns], i)) {
            printf("p");
        } else if (check_bit(BITBOARDS->bits[white_knights], i)) {
            printf("N");
        } else if (check_bit(BITBOARDS->bits[black_knights], i)) {
            printf("n");
        } else if (check_bit(BITBOARDS->bits[white_bishops], i)) {
            printf("B");
        } else if (check_bit(BITBOARDS->bits[black_bishops], i)) {
            printf("b");
        } else if (check_bit(BITBOARDS->bits[white_rooks], i)) {
            printf("R");
        } else if (check_bit(BITBOARDS->bits[black_rooks], i)) {
            printf("r");
        } else if (check_bit(BITBOARDS->bits[white_queens], i)) {
            printf("Q");
        } else if (check_bit(BITBOARDS->bits[black_queens], i)) {
            printf("q");
        } else if (check_bit(BITBOARDS->bits[white_king], i)) {
            printf("K");
        } else if (check_bit(BITBOARDS->bits[black_king], i)) {
            printf("k");
        } else {
            int count = 0;
            while (!(check_bit(BITBOARDS->bits[all_pieces], i))) {
                count++;
                i--;
                if ((i + 1) % 8 == 0) {
                    break;
                }
            }
            printf("%d", count);
            i++;
        }
        if ((i % 8) == 0 && i != 0) {
            printf("/");
        }
    }

    printf(" %c ", BITBOARDS->color == WHITE ? 'w' : 'b');

    if (BITBOARDS->white_castle_king_side) {
        printf("K");
    }
    if (BITBOARDS->white_castle_queen_side) {
        printf("Q");
    }
    if (BITBOARDS->black_castle_king_side) {
        printf("k");
    }
    if (BITBOARDS->black_castle_queen_side) {
        printf("q");
    }
    if (!BITBOARDS->white_castle_king_side && !BITBOARDS->white_castle_queen_side && !BITBOARDS->black_castle_king_side && !BITBOARDS->black_castle_queen_side) {
        printf("-");
    }

    if (BITBOARDS->en_passant_square >= 0) {
        printf(" %s", notation[BITBOARDS->en_passant_square]);
    } else {
        printf(" -");
    }

    printf(" %d %d\nhash: ", half_move_count, full_move_count % 2 == 0 ? (full_move_count / 2) + 1 : (full_move_count + 1) / 2);
    print_binary(BITBOARDS->hash);

    printf("\n\n");
}
