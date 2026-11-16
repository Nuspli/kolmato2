#!/usr/bin/env python3
"""Tests of UCI pondering (go ponder / ponderhit / stop / isready while pondering).
usage: python3 tools/ponder_test.py ./kolmato2 [-v]
Needs python-chess only for the legality check of the ponder move."""
import subprocess, sys, time, threading, queue
import chess

class Engine:
    def __init__(self, path, verbose=False):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, bufsize=1)
        self.q = queue.Queue(); self.verbose = verbose
        threading.Thread(target=self._read, daemon=True).start()
    def _read(self):
        for line in self.p.stdout:
            self.q.put((time.time(), line.strip()))
    def send(self, s):
        if self.verbose: print(">>", s)
        self.p.stdin.write(s + "\n"); self.p.stdin.flush()
    def wait(self, prefix, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            try: t, line = self.q.get(timeout=max(0.01, end - time.time()))
            except queue.Empty: break
            if self.verbose and not line.startswith("info depth"): print("<<", line)
            if line.startswith(prefix): return t, line
        return None, None
    def none_within(self, prefix, secs):
        t, line = self.wait(prefix, secs); return line is None
    def close(self):
        try: self.send("quit"); self.p.wait(3)
        except Exception: self.p.kill()

fails = 0
def check(name, ok, detail=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (("  (" + detail + ")") if detail else ""))
    if not ok: fails += 1

def new(path, verbose, ponder=True):
    e = Engine(path, verbose)
    e.send("uci"); e.wait("uciok")
    e.send("setoption name Hash value 16")
    if ponder: e.send("setoption name Ponder value true")
    e.send("isready"); e.wait("readyok")
    return e

START = "position startpos moves e2e4 e7e5 g1f3"      # black to move; the pondered reply would be b8c6 / g8f6
def main():
    path = sys.argv[1]; v = "-v" in sys.argv

    # 1. Ponder off: no ponder move; on: a legal one
    e = new(path, v, ponder=False)
    e.send("position startpos"); e.send("go movetime 300")
    t, line = e.wait("bestmove", 5)
    check("Ponder off: bestmove without ponder move", line and "ponder" not in line, line or "")
    e.close()
    e = new(path, v)
    b = chess.Board(); ok = True; detail = ""
    for fen_moves in ["", "e2e4 e7e5 g1f3 b8c6"]:
        e.send("position startpos" + (" moves " + fen_moves if fen_moves else "")); e.send("go movetime 300")
        t, line = e.wait("bestmove", 5); w = line.split()
        bb = chess.Board()
        for m in fen_moves.split(): bb.push_uci(m)
        if len(w) != 4 or w[2] != "ponder": ok = False; detail = line; break
        mv = chess.Move.from_uci(w[1])
        if mv not in bb.legal_moves: ok = False; detail = line; break
        bb.push(mv)
        if chess.Move.from_uci(w[3]) not in bb.legal_moves: ok = False; detail = line; break
    check("Ponder on: bestmove carries a legal ponder move", ok, detail)
    # promotion / forced move still gives a legal answer
    e.send("position fen 7k/8/8/8/8/8/5q2/K7 w - - 0 1"); e.send("go depth 6")
    t, line = e.wait("bestmove", 5); check("forced move answers", line is not None, line or "")

    # 2. go ponder: no bestmove while pondering, isready answered, stop answered
    e.send(START); e.send("go ponder wtime 5000 btime 5000 winc 100 binc 100")
    check("go ponder: no bestmove by itself", e.none_within("bestmove", 1.0))
    e.send("isready"); t, line = e.wait("readyok", 2); check("isready while pondering is answered", line == "readyok")
    t0 = time.time(); e.send("stop"); t, line = e.wait("bestmove", 2)
    check("stop ends the ponder search with a bestmove", line is not None and t - t0 < 0.5, "%.3f s" % (t - t0 if t else -1))

    # 3. ponderhit: the search goes on and the clock starts with the ponderhit
    e.send(START); e.send("go ponder wtime 4000 btime 4000 winc 0 binc 0")
    time.sleep(2.0)                                   # the opponent thinks for 2 s: must not be charged
    t0 = time.time(); e.send("ponderhit")
    t, line = e.wait("bestmove", 6); dt = t - t0 if t else 99
    # budget: soft limit about 4000/30 = 133 ms (+ scaling, up to 2.3x), hard limit 3.87 x
    check("ponderhit: answer comes within the normal time of the move (not 2 s late)", line is not None and dt < 1.5, "%.3f s after ponderhit" % dt)
    check("ponderhit: bestmove carries a ponder move", line is not None and "ponder" in line, line or "")
    # info after the ponderhit: time restarts
    # 4. ponderhit with a very long ponder phase must not play instantly nor forfeit: depth keeps growing
    e.send(START); e.send("go ponder wtime 60000 btime 60000 winc 1000 binc 1000")
    time.sleep(1.5); t0 = time.time(); e.send("ponderhit")
    t, line = e.wait("bestmove", 20); dt = t - t0 if t else 99
    check("ponderhit with 60 s clock: answers within the hard limit (3.87 x base, <= 58 %)", line is not None and dt < 12, "%.3f s" % dt)

    # 5. a ponder search that ends by itself (mate in 1) waits for ponderhit / stop
    e.send("position fen 6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1"); e.send("go ponder wtime 5000 btime 5000")
    check("ended ponder search (mate found) waits", e.none_within("bestmove", 1.5))
    t0 = time.time(); e.send("ponderhit"); t, line = e.wait("bestmove", 2)
    check("... and answers at ponderhit", line is not None and t - t0 < 0.3 and line.startswith("bestmove a1a8"), line or "")
    e.send("go ponder wtime 5000 btime 5000")
    check("ended ponder search waits (again)", e.none_within("bestmove", 1.0))
    e.send("stop"); t, line = e.wait("bestmove", 2); check("... and answers at stop", line is not None, line or "")
    # forced move while pondering
    e.send("position fen 7k/8/8/8/8/8/5q2/K7 w - - 0 1"); e.send("go ponder wtime 5000 btime 5000")
    check("forced move while pondering waits", e.none_within("bestmove", 1.0))
    e.send("ponderhit"); t, line = e.wait("bestmove", 2); check("... answers at ponderhit", line is not None, line or "")

    # 6. next move after a miss: stop, new position, normal go
    e.send(START); e.send("go ponder wtime 5000 btime 5000"); time.sleep(0.5); e.send("stop"); e.wait("bestmove", 2)
    e.send("position startpos moves e2e4 e7e5 g1f3 g8f6"); e.send("go wtime 5000 btime 5000")
    t, line = e.wait("bestmove", 5); check("normal search after a ponder miss", line is not None, line or "")

    # 7. nps after a ponderhit counts only the new time (info line of the final report)
    e.send(START); e.send("go ponder wtime 3000 btime 3000"); time.sleep(1.5); e.send("ponderhit")
    last = None; end = time.time() + 5
    while time.time() < end:
        try: t, l = e.q.get(timeout=0.2)
        except queue.Empty: continue
        if l.startswith("info nodes"): last = l
        if l.startswith("bestmove"): break
    ok = False
    if last:
        f = last.split(); nodes = int(f[2]); nps = int(f[4]); ms = int(f[6])
        ok = nps < 5_000_000   # sanity only (ms counts from the ponderhit)
        check("final info line after ponderhit is sane", ok, last)
    else:
        check("final info line after ponderhit is sane", False, "no line")
    e.close()

    # 8. threads: ponder with 4 threads, ponderhit and stop
    e = new(path, v); e.send("setoption name Threads value 4")
    e.send(START); e.send("go ponder wtime 3000 btime 3000"); time.sleep(1.0); e.send("ponderhit")
    t, line = e.wait("bestmove", 6); check("Threads 4: ponderhit answers", line is not None, line or "")
    e.send(START); e.send("go ponder wtime 3000 btime 3000"); time.sleep(0.5); e.send("stop")
    t, line = e.wait("bestmove", 3); check("Threads 4: stop answers", line is not None, line or "")
    e.close()
    print("\nFAILED: %d" % fails if fails else "\nall ponder tests passed")
    sys.exit(1 if fails else 0)
main()
