#!/usr/bin/env python3
"""Does a build with -DNAME=value flags behave exactly like the tuning build with the same values set as UCI options?

A -D flag is silently ignored when a parameter macro is defined without #ifndef (this once happened with RFP_MARGIN / DELTA_MARGIN and
cost a full SPSA evaluation). params.def makes that impossible; the check is still the proof that the tune build and the flags build
agree. Run this before an SPRT of SPSA results:

    make tune                                        # ./kolmato2_tune  (-DTUNE_SEARCH)
    make pgo EXTRA="<flags>"                         # ./kolmato2 = the flags build
    python3 tools/check_flags.py ./kolmato2_tune ./kolmato2 "<flags>"
(PGO builds change timing only, never node counts, so they can be compared with a non PGO tune build.)
Expected: identical node counts on 16/16 positions."""
import re, subprocess, sys
sys.path.insert(0, __file__.rsplit('/', 1)[0] if '/' in __file__ else '.')
from bench import POS

def nodes(binary, options, fen, depth):
    cmds = "uci\n" + "".join("setoption name %s value %s\n" % kv for kv in options.items())
    cmds += "isready\nposition fen %s\ngo depth %d\n" % (fen, depth)
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    p.stdin.write(cmds); p.stdin.flush()
    last = None
    for line in p.stdout:
        m = re.match(r"info depth %d .*nodes (\d+)" % depth, line)
        if m:
            last = int(m.group(1))
        if line.startswith("bestmove"):
            break
    p.stdin.write("quit\n"); p.stdin.flush(); p.wait()
    return last

if len(sys.argv) < 4:
    sys.exit(__doc__)
tune, flagsbin, flags = sys.argv[1:4]
depth = int(sys.argv[4]) if len(sys.argv) > 4 else 10
opts = dict(re.findall(r"-D(\w+)=(-?\d+)", flags))
same = 0
for fen in POS:
    a, b = nodes(tune, opts, fen, depth), nodes(flagsbin, {}, fen, depth)
    same += (a == b)
    if a != b:
        print("differs: %s  tune %s  flags %s" % (fen, a, b))
print("%d/%d positions with identical node counts (%d options set: %s)" % (same, len(POS), len(opts), ' '.join(sorted(opts))))
sys.exit(0 if same == len(POS) else 1)
