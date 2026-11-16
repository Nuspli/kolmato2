#!/usr/bin/env python3
"""En passant stress test: random positions in which an en passant capture is available (kings and rooks / queens often on the rank of the two
pawns, bishops on the diagonals through the captured pawn, checks by the pawn, knights and sliders), compared with python-chess:
the engine's legal move list must be the same, the quiescence captures must contain all legal captures and nothing illegal.
usage: ep_diff.py <movetest-binary> [num_positions] [seed]      (build the binary with -DVERIFY_EP=1 to also compare ep_capture_is_legal with the complete test)"""
import chess, random, subprocess, sys, os
exe = os.path.abspath(sys.argv[1]); want = int(sys.argv[2]) if len(sys.argv) > 2 else 50000; random.seed(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
S = chess.square
def make():
    b = chess.Board(None)
    f = random.randrange(8); g = random.choice([x for x in (f - 1, f + 1) if 0 <= x < 8])
    cap, frm, ep = S(f, 4), S(g, 4), S(f, 5)
    b.set_piece_at(cap, chess.Piece(chess.PAWN, chess.BLACK)); b.set_piece_at(frm, chess.Piece(chess.PAWN, chess.WHITE))
    busy = {cap, frm, ep, S(f, 6)}
    def inb(x, y): return 0 <= x < 8 and 0 <= y < 8
    def put(x, y, piece):
        if inb(x, y) and S(x, y) not in busy and not (piece.piece_type == chess.PAWN and y in (0, 7)):
            busy.add(S(x, y)); b.set_piece_at(S(x, y), piece); return True
        return False
    def free(rank_bias):
        for _ in range(50):
            sq = S(random.randrange(8), 4) if random.random() < rank_bias else random.randrange(64)
            if sq not in busy: return sq
    W, B = chess.WHITE, chess.BLACK
    DIAG = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
    r = random.random(); wk = None; pending = None
    # white (side to move) king: gadgets that create the interesting situations, otherwise random / on the rank of the pawns
    if r < 0.12:      # checked by the pawn that is captured
        x = random.choice([f - 1, f + 1]); wk = (x, 3)
    elif r < 0.30:    # on a diagonal through the captured pawn / through the landing square, with a black bishop or queen on the far side
        base = random.choice([(f, 4), (f, 5)]); dx, dy = random.choice(DIAG); a = random.randint(1, 4); c = random.randint(1, 4)
        wk = (base[0] + dx * a, base[1] + dy * a)
        piece = chess.Piece(random.choice([chess.BISHOP, chess.QUEEN]), B)
        if inb(*wk) and (wk[0], wk[1]) != (f, 6): pending = (base[0] - dx * c, base[1] - dy * c, piece)
        else: wk = None
    elif r < 0.55:    # on the rank of the pawns
        wk = (random.randrange(8), 4)
    if wk is not None and not inb(*wk): wk = None
    if wk is None or S(*wk) in busy:
        sq = free(0.3); wk = (chess.square_file(sq), chess.square_rank(sq))
    busy.add(S(*wk)); b.set_piece_at(S(*wk), chess.Piece(chess.KING, W))
    if pending is not None: put(*pending)
    sq = free(0.4); busy.add(sq); b.set_piece_at(sq, chess.Piece(chess.KING, B))
    # checks of other kinds
    if random.random() < 0.25:      # knight check
        dx, dy = random.choice([(1, 2), (2, 1), (-1, 2), (-2, 1), (1, -2), (2, -1), (-1, -2), (-2, -1)]); put(wk[0] + dx, wk[1] + dy, chess.Piece(chess.KNIGHT, B))
    if random.random() < 0.15:      # check by another black pawn
        put(wk[0] + random.choice([-1, 1]), wk[1] + 1, chess.Piece(chess.PAWN, B))
    if random.random() < 0.25:      # slider check along any line
        dx, dy = random.choice(DIAG + [(1, 0), (-1, 0), (0, 1), (0, -1)]); k = random.randint(1, 6)
        pt = random.choice([chess.QUEEN, chess.ROOK] if dx == 0 or dy == 0 else [chess.QUEEN, chess.BISHOP]); put(wk[0] + dx * k, wk[1] + dy * k, chess.Piece(pt, B))
    if random.random() < 0.15:      # a rook or queen on the rank of the pawns
        put(random.randrange(8), 4, chess.Piece(random.choice([chess.ROOK, chess.QUEEN]), B))
    for _ in range(random.randint(0, 5)):
        pt = random.choice([chess.ROOK, chess.ROOK, chess.QUEEN, chess.BISHOP, chess.BISHOP, chess.KNIGHT, chess.PAWN, chess.PAWN])
        sq = free(0.4)
        put(chess.square_file(sq), chess.square_rank(sq), chess.Piece(pt, random.choice([W, B])))
    b.turn = chess.WHITE; b.ep_square = ep
    if random.random() < 0.5: b = b.mirror()
    # python-chess calls a check that can not have come from the last (double step) move impossible; the engine must still get such a position (a fen) right
    return b if b.status() & ~chess.STATUS_IMPOSSIBLE_CHECK == 0 else None
boards = []
while len(boards) < want:
    b = make()
    if b is not None: boards.append(b)
fens = [b.fen(en_passant='fen') for b in boards]
out = subprocess.run([exe], input="\n".join(fens) + "\n", capture_output=True, text=True, cwd='/tmp')
if out.returncode != 0 or "EP MISMATCH" in out.stderr: print("engine failed:", out.returncode, out.stderr[:500]); sys.exit(1)
rows = [l for l in out.stdout.splitlines() if l[:1] in "MC"]
assert len(rows) == 2 * len(fens), (len(rows), len(fens))
bad = capbad = legalEp = illegalEp = in_check = 0
for i, b in enumerate(boards):
    legal = {m.uci() for m in b.legal_moves}
    eng = set(rows[2 * i].split()[1:]); cap = set(rows[2 * i + 1].split()[1:])
    expect_caps = {m.uci() for m in b.legal_moves if b.is_capture(m)}
    legalEp += any(b.is_en_passant(m) for m in b.legal_moves)
    illegalEp += any(b.is_en_passant(m) and not b.is_legal(m) for m in b.pseudo_legal_moves)
    in_check += b.is_check()
    if eng != legal:
        bad += 1
        if bad <= 8: print("MOVES DIFFER:", fens[i], "\n   extra in engine:", sorted(eng - legal), " missing:", sorted(legal - eng))
    if not cap <= legal or not expect_caps <= cap:
        capbad += 1
        if capbad <= 8: print("CAPTURES DIFFER:", fens[i], "\n   illegal in list:", sorted(cap - legal), " missing captures:", sorted(expect_caps - cap))
print("positions: %d (side to move in check: %d, legal en passant available: %d, pseudo-legal en passant that is illegal: %d)" % (len(boards), in_check, legalEp, illegalEp))
print("move-list mismatches: %d   capture-list mismatches: %d" % (bad, capbad))
sys.exit(1 if bad or capbad else 0)
