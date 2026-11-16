#!/usr/bin/env python3
"""Compare the engine's legal moves / quiescence captures with python-chess on random-playout positions.
usage: movegen_diff.py <movetest-binary> [num_games] [seed]"""
import chess, random, subprocess, sys
import os; exe = os.path.abspath(sys.argv[1]); games = int(sys.argv[2]) if len(sys.argv) > 2 else 300; random.seed(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
STARTS = [chess.STARTING_FEN,
 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
 "5k2/8/6K1/7p/7P/1P6/P4PP1/8 w - - 0 51",
 "8/8/8/8/8/3k4/7q/K7 b - - 0 1", "4k3/8/8/8/8/8/p7/4K3 b - - 0 1", "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"]
fens = []
for g in range(games):
    b = chess.Board(random.choice(STARTS))
    for _ in range(random.randint(10, 160)):
        if b.is_game_over(): break
        fens.append(b.fen(en_passant='fen'))
        b.push(random.choice(list(b.legal_moves)))
inp = "\n".join(fens) + "\n"
out = subprocess.run([exe], input=inp, capture_output=True, text=True, cwd='/tmp').stdout.splitlines()
rows = [l for l in out if l[:1] in "MC"]
assert len(rows) == 2 * len(fens), (len(rows), len(fens))
bad = 0; capbad = 0
for i, fen in enumerate(fens):
    b = chess.Board(fen)
    legal = {m.uci() for m in b.legal_moves}
    eng = set(rows[2*i].split()[1:]); cap = set(rows[2*i+1].split()[1:])
    expect_caps = {m.uci() for m in b.legal_moves if b.is_capture(m)}
    if eng != legal:
        bad += 1
        if bad <= 8: print("MOVES DIFFER:", fen, "\n   extra in engine:", sorted(eng - legal), " missing:", sorted(legal - eng))
    if not cap <= legal or not expect_caps <= cap:
        capbad += 1
        if capbad <= 8: print("CAPTURES DIFFER:", fen, "\n   illegal in list:", sorted(cap - legal), " missing captures:", sorted(expect_caps - cap))
print("positions: %d   move-list mismatches: %d   capture-list mismatches: %d" % (len(fens), bad, capbad))
