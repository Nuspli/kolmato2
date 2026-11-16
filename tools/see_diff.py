#!/usr/bin/env python3
"""Verify attackers_to() and see() against independent python-chess implementations.
usage: see_diff.py <movetest-binary> [games] [seed]"""
import chess, random, subprocess, sys, os
exe = os.path.abspath(sys.argv[1]); games = int(sys.argv[2]) if len(sys.argv) > 2 else 300; random.seed(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
VAL = {chess.PAWN: 90, chess.KNIGHT: 310, chess.BISHOP: 320, chess.ROOK: 490, chess.QUEEN: 880, chess.KING: 20000}
def eng_sq(s): return 7 + 8 * chess.square_rank(s) - chess.square_file(s)       # python-chess square -> engine square
def ref_see(board, move):
    b = board.copy(stack=False); to = move.to_square
    gains = [VAL[b.piece_type_at(to)]]
    mover = b.piece_at(move.from_square); b.remove_piece_at(move.from_square); b.set_piece_at(to, mover)
    side = not mover.color; cur = VAL[mover.piece_type]
    while True:
        att = b.attackers(side, to)
        if not att: break
        sq = min(att, key=lambda s: VAL[b.piece_type_at(s)])
        gains.append(cur - gains[-1]); cur = VAL[b.piece_type_at(sq)]
        p = b.piece_at(sq); b.remove_piece_at(sq); b.set_piece_at(to, p); side = not side
    while len(gains) > 1:
        g = gains.pop(); gains[-1] = -max(-gains[-1], g)
    return gains[0]
fens = []
for _ in range(games):
    b = chess.Board()
    for _ in range(random.randint(8, 120)):
        if b.is_game_over(): break
        ms = list(b.legal_moves); caps = [m for m in ms if b.is_capture(m)]
        fens.append(b.fen(en_passant='fen'))
        b.push(random.choice(caps) if caps and random.random() < .45 else random.choice(ms))
out = subprocess.run([exe], input="\n".join(fens) + "\n", capture_output=True, text=True, cwd='/tmp').stdout.splitlines()
rows = [l for l in out if l[:1] in "MCAS"]; assert len(rows) == 4 * len(fens), (len(rows), len(fens))
att_bad = see_bad = see_n = 0
for i, fen in enumerate(fens):
    b = chess.Board(fen)
    a_row = rows[4*i+2].split()[1:]; s_row = rows[4*i+3].split()[1:]
    for sq in range(64):
        want = 0
        for s in (b.attackers(chess.WHITE, sq) | b.attackers(chess.BLACK, sq)): want |= 1 << eng_sq(s)
        if int(a_row[eng_sq(sq)], 16) != want:
            att_bad += 1
            if att_bad <= 3: print("ATTACKERS DIFFER", fen, chess.square_name(sq))
    eng = dict(x.split('=') for x in s_row)
    for m in b.legal_moves:
        if b.is_capture(m) and not b.is_en_passant(m) and m.promotion is None:
            see_n += 1
            got = int(eng[m.uci()]); want = ref_see(b, m)
            if got != want:
                see_bad += 1
                if see_bad <= 6: print("SEE DIFFER", fen, m.uci(), "engine", got, "reference", want)
print("positions: %d  attacker-set mismatches: %d   captures checked: %d  SEE mismatches: %d" % (len(fens), att_bad, see_n, see_bad))
