#!/usr/bin/env python3
"""A position and its color-flipped mirror must get the same static evaluation (from the side to move's view).
usage: eval_symmetry.py <movetest-binary> [games] [seed]"""
import chess, random, subprocess, sys, os
exe = os.path.abspath(sys.argv[1]); games = int(sys.argv[2]) if len(sys.argv) > 2 else 300; random.seed(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
fens = []
for _ in range(games):
    b = chess.Board()
    for _ in range(random.randint(6, 140)):
        if b.is_game_over(): break
        ms = list(b.legal_moves); caps = [m for m in ms if b.is_capture(m)]
        if not b.is_check(): fens.append((b.fen(en_passant='fen'), b.mirror().fen(en_passant='fen')))
        b.push(random.choice(caps) if caps and random.random() < .4 else random.choice(ms))
flat = [x for pair in fens for x in pair]
out = subprocess.run([exe], input="\n".join(flat) + "\n", capture_output=True, text=True, cwd='/tmp').stdout.splitlines()
ev = [int(l.split()[1]) for l in out if l.startswith('E ')]
assert len(ev) == len(flat), (len(ev), len(flat))
bad = 0
for i, (f, m) in enumerate(fens):
    a, c = ev[2*i], ev[2*i+1]
    if a != c:
        bad += 1
        if bad <= 6: print("ASYMMETRIC: %d vs %d  %s" % (a, c, f))
print("position pairs: %d   asymmetric: %d" % (len(fens), bad))
