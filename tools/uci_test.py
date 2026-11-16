#!/usr/bin/env python3
"""Tests kolmato2's UCI mode with python-chess as the client (an independent implementation of the protocol).
usage: uci_test.py <kolmato2-binary>"""
import sys, time, os, chess, chess.engine

binary = os.path.abspath(sys.argv[1])
failures = []

def check(name, condition, detail=""):
    print("  %-62s %s %s" % (name, "ok" if condition else "FAILED", detail))
    if not condition:
        failures.append(name)

# the engine is started without arguments and with a pipe as its input, which is how GUIs do it
engine = chess.engine.SimpleEngine.popen_uci([binary], timeout=30)

print("handshake and options")
check("engine name", engine.id.get("name") == "kolmato2", str(engine.id))
for option in ("Hash", "Clear Hash", "Threads", "Move Overhead", "OwnBook", "BookFile"):
    check("option %s is declared" % option, option in engine.options)
engine.configure({"Hash": 64, "Move Overhead": 20})

print("fixed limits")
board = chess.Board()
info = engine.analyse(board, chess.engine.Limit(depth=8))
check("depth 8 search reports depth 8", info.get("depth") == 8, "depth %s" % info.get("depth"))
check("score and pv are present", "score" in info and len(info.get("pv", [])) > 0)
b2 = board.copy()
legal_pv = True
for m in info.get("pv", []):
    if m not in b2.legal_moves:
        legal_pv = False
        break
    b2.push(m)
check("every move of the pv is legal", legal_pv)
check("nodes and time are reported", info.get("nodes", 0) > 0 and "time" in info)

for movetime in (0.2, 0.5):
    start = time.perf_counter()
    result = engine.play(board, chess.engine.Limit(time=movetime))
    elapsed = time.perf_counter() - start
    check("movetime %.1f s: legal move, time used %.3f s" % (movetime, elapsed), result.move in board.legal_moves and movetime - 0.12 < elapsed < movetime + 0.15)

info = engine.analyse(board, chess.engine.Limit(nodes=40000))
check("node limit 40000", 40000 <= info.get("nodes", 0) < 80000, "nodes %s" % info.get("nodes"))

print("clock based time management (soft and hard limits)")
for clock, inc in ((10, 0), (60, 0), (30, 1), (3, 0.1), (0.5, 0)):
    used = []
    for _ in range(2):
        start = time.perf_counter()
        result = engine.play(board, chess.engine.Limit(white_clock=clock, black_clock=clock, white_inc=inc, black_inc=inc))
        used.append(time.perf_counter() - start)
    ok = max(used) < clock * 0.6 + 0.2 and result.move in board.legal_moves
    check("clock %5.1f s + %.1f s: used %.2f s" % (clock, inc, max(used)), ok)

print("positions: mate, promotion, castling, en passant")
mate_in_2 = chess.Board("7k/8/5K2/8/8/8/8/Q7 w - - 0 1")
info = engine.analyse(mate_in_2, chess.engine.Limit(depth=8))
score = info["score"].white()
check("mate in 2 is reported as a mate score", score.is_mate() and score.mate() == 2, str(score))
result = engine.play(chess.Board("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"), chess.engine.Limit(depth=4))
check("mate in 1 is found", result.move == chess.Move.from_uci("a1a8"), str(result.move))

moves = "e2e4 d7d5 e4e5 f7f5 e5f6 e7e6 g1f3 f8d6 f1e2 g8f6 e1g1 e8g8"      # contains en passant and castling for both sides
board = chess.Board()
for m in moves.split():
    board.push_uci(m)
result = engine.play(board, chess.engine.Limit(depth=5), game=object())
check("position with en passant and castling moves is understood", result.move in board.legal_moves, str(result.move))

promo = chess.Board("8/P6k/8/8/8/8/8/K7 w - - 0 1")
result = engine.play(promo, chess.engine.Limit(depth=5))
check("promotion move has its piece letter", result.move is not None and result.move.promotion == chess.QUEEN, str(result.move))
under = chess.Board("1k6/8/8/8/8/8/p7/7K b - - 0 1")
engine.play(under, chess.engine.Limit(depth=3))
board = chess.Board("8/P6k/8/8/8/8/8/K7 w - - 0 1")
board.push_uci("a7a8n")                                        # an under-promotion that the engine has to understand
result = engine.play(board, chess.engine.Limit(depth=5))
check("under-promotion in the move list is understood", result.move in board.legal_moves, str(result.move))

print("stop during an infinite search")
board = chess.Board()
start = time.perf_counter()
with engine.analysis(board) as analysis:                       # go infinite
    time.sleep(1.0)
    first = analysis.info
    t_stop = time.perf_counter()
    analysis.stop()
    best = analysis.wait()
stopped_in = time.perf_counter() - t_stop
check("search runs until stop and answers quickly (%.3f s)" % stopped_in, stopped_in < 0.3 and best.move in board.legal_moves)
check("info lines arrived while searching", first.get("depth", 0) >= 5, "depth %s" % first.get("depth"))

print("a new game, many quick searches, the end")
for i in range(3):
    engine.play(chess.Board(), chess.engine.Limit(time=0.05), game="game%d" % i)    # a new game object sends ucinewgame
check("ucinewgame and repeated searches work", True)
engine.quit()
check("engine quits when asked", True)

print()
print("ALL PASSED" if not failures else "FAILED: " + ", ".join(failures))
sys.exit(1 if failures else 0)
