#!/bin/bash
# SPRT test: NEW engine binary vs BASE engine binary with fastchess (https://github.com/Disservin/fastchess).
# usage: tools/sprt.sh NEW BASE [tc=10+0.1] [elo0=0] [elo1=5] [concurrency=cores-1]
# H1 = NEW is at least elo1 stronger than BASE. Stops by itself when the test is decided (alpha = beta = 0.05).
# fastchess is taken from $FASTCHESS or the PATH. the balanced openings (openings.epd) are made on the first run with tools/gen_openings.py
# (random 8 ply lines that the engine rates as about equal); the games go to games.pgn. both files are in the current directory.
set -e
NEW="$1"; BASE="$2"; TC="${3:-10+0.1}"; ELO0="${4:-0}"; ELO1="${5:-5}"
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -n "$NEW" ] && [ -n "$BASE" ] || { sed -n 2,4p "$0"; exit 1; }
FC="${FASTCHESS:-$(command -v fastchess || true)}"
[ -x "$FC" ] || { echo "fastchess not found (put it in the PATH or set FASTCHESS=...)"; exit 1; }
for b in "$NEW" "$BASE"; do
  [ -x "$b" ] || { echo "not an executable: $b"; exit 1; }
done
[ -f openings.epd ] || python3 "$HERE/gen_openings.py" "$BASE" openings.epd
# which files are really tested (a wrong directory is easy to miss): full path and build time of both
echo "NEW : $(readlink -f "$NEW")  $(stat -c '%y' "$NEW" | cut -d. -f1)"
echo "BASE: $(readlink -f "$BASE")  $(stat -c '%y' "$BASE" | cut -d. -f1)"
# one game at a time per core, the engine is single threaded by default. one core stays free for the OS and fastchess
CONC="${6:-$(( $(nproc) > 1 ? $(nproc) - 1 : 1 ))}"
"$FC" \
  -engine cmd="$NEW"  name=new  option.Hash=16 option.OwnBook=false \
  -engine cmd="$BASE" name=base option.Hash=16 option.OwnBook=false \
  -each proto=uci tc="$TC" \
  -openings file=openings.epd format=epd order=random \
  -games 2 -repeat -rounds 100000 \
  -concurrency "$CONC" \
  -sprt elo0="$ELO0" elo1="$ELO1" alpha=0.05 beta=0.05 \
  -ratinginterval 20 -pgnout file=games.pgn
