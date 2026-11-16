#!/usr/bin/env python3
"""Make a balanced random opening book (EPD) for SPRT testing.
Random legal lines of N plies, kept only if kolmato2 at a fixed shallow depth says |cp| <= LIMIT.
usage: gen_openings.py ./kolmato2 openings.epd [count=2000] [plies=8] [limit=80]"""
import sys, random, chess, chess.engine
exe, out = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 2000
plies = int(sys.argv[4]) if len(sys.argv) > 4 else 8
limit = int(sys.argv[5]) if len(sys.argv) > 5 else 80
eng = chess.engine.SimpleEngine.popen_uci(exe)
eng.configure({"OwnBook": False})
seen, kept = set(), []
random.seed(12345)
while len(kept) < count:
    b = chess.Board()
    for _ in range(plies):
        if b.is_game_over(): break
        b.push(random.choice(list(b.legal_moves)))
    if b.is_game_over(): continue
    key = b.epd()
    if key in seen: continue
    seen.add(key)
    info = eng.analyse(b, chess.engine.Limit(depth=6))
    s = info["score"].white()
    if s.is_mate() or abs(s.score()) > limit: continue
    kept.append(key)
    if len(kept) % 200 == 0: print(len(kept), "openings", file=sys.stderr)
eng.quit()
with open(out, "w") as f:
    f.write("\n".join(kept) + "\n")
