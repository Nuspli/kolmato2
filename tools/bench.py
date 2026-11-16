#!/usr/bin/env python3
"""usage: bench.py <depth> <bin1> [bin2 ...]   -- fixed depth search of a position set, compares builds side by side"""
import os
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
import chess, subprocess, sys, re, time
POS = [
 "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
 "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 40",
 "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 12",
 "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
 "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
 "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/2NP1N2/PPP2PPP/R1BQK2R b KQkq - 0 5",
 "r1bqkb1r/pp3ppp/2nppn2/8/3NP3/2N5/PPP2PPP/R1BQKB1R w KQkq - 0 6",
 "8/p7/8/1P6/K7/8/7k/8 w - - 0 50",
 "1K1k4/1P6/8/8/8/8/r7/2R5 w - - 0 60",
 "r2q1rk1/ppp2ppp/2n1bn2/3p4/3P4/2N1PN2/PP3PPP/R1BQ1RK1 w - - 0 9",
 "2r3k1/pp3ppp/8/3q4/3P4/2P2Q2/P4PPP/4R1K1 w - - 0 30",
 "8/8/4k3/8/2p5/2K5/8/8 w - - 0 60",
 "8/5pk1/6p1/8/8/6P1/5PK1/3R4 w - - 0 50",
 "r1b2rk1/2q1bppp/p2p1n2/np2p3/3NP3/1BN1BP2/PPP1Q1PP/R3R1K1 w - - 0 14",
 "3r2k1/pp3pp1/2p1b2p/8/2P1P3/P1N2P2/1P4PP/3R2K1 w - - 0 25",
]
def run(binp, fen, d):
    b = chess.Board(fen); f = b.fen(en_passant='fen').split()
    t = time.time()
    out = subprocess.run([binp,'-b',f[0],f[1],f[2],f[3],f[4],f[5],'-d',str(d),'-t','1000'],
                         cwd=ROOT, capture_output=True, text=True).stdout
    dt = time.time() - t
    mv = re.findall(r'final move: (\S+)', out); val = re.findall(r'>>> best move: \S+ value: (-?\d+)', out)
    n = re.findall(r'nodes searched: (\d+) \((\d+) quiet\)', out)
    nodes = (int(n[-1][0]) + int(n[-1][1])) if n else 0
    return dt, nodes, (mv[-1] if mv else '?'), (val[-1] if val else '?')
if __name__ == '__main__':
    import os; d = int(sys.argv[1]); bins = [os.path.abspath(b) for b in sys.argv[2:]]
    for fen in POS: assert chess.Board(fen).is_valid(), fen
    tot = [0.0]*len(bins); totn = [0]*len(bins); same = 0
    print("%-4s" % "pos" + "".join("| %-34s" % b.split('/')[-1] for b in bins))
    for i, fen in enumerate(POS):
        rs = [run(b, fen, d) for b in bins]
        for j, r in enumerate(rs): tot[j] += r[0]; totn[j] += r[1]
        if len(set(r[2] for r in rs)) == 1: same += 1
        print("%-4d" % i + "".join("| %6.2fs %8dk n %-6s %6s" % (r[0], r[1]//1000, r[2], r[3]) for r in rs))
    print("TOTAL" + "".join("| %6.2fs %8dk nodes" % (tot[j], totn[j]//1000) + " "*14 for j in range(len(bins))))
    print("same best move in all builds: %d/%d" % (same, len(POS)))
