# kolmato2 build
#
#   make                  normal build -> ./kolmato2 (-Ofast, cpu flags of this machine)
#   make pgo              profile guided build (compiles, trains on a few searches, compiles again), about 7-10 % faster
#   make tune             PGO build with the search parameters as UCI options (-DTUNE_SEARCH) -> ./kolmato2_tune, for SPSA
#   make test             the regression suite (tools/regress.sh, needs python-chess: pip install chess)
#   make bench            fixed depth search of 16 positions (tools/bench.py, prints the node signature)
#   make book             unpack the opening book (book/18moves.7z -> bookMax.dat, needs 7z / 7zz / 7za)
#   make clean
#
# variables (make VAR=value):
#   EXTRA="-DRFP_MARGIN=100 ..."   more compiler flags, e.g. a parameter of params.def or -DUSE_HUGEPAGES
#   OUT=name                       name of the binary (default kolmato2)
#   PORTABLE=1                     only -mpopcnt, no BMI: a binary that runs on any x86-64 cpu with popcnt (Haswell / Zen and newer have BMI)
#   EVAL_C=file.c                  build with this file instead of eval.c (A/B test of a texel retune: make pgo OUT=kolmato2_base EVAL_C=eval_before_texel.c)
#   CC=clang                       another compiler (gcc is what the engine is developed and tested with)
#
# the engine uses -mpopcnt (about 18 % faster) and, when the machine that builds it has them, -mbmi -mbmi2 (blsr / tzcnt in the bit loops,
# 2-9 % faster). a binary built with BMI checks at startup that its cpu has it and says so instead of crashing. x86-64 only.
# every build compiles all files in one command (about 3 s), so a changed EXTRA is always picked up.

CC = gcc
OUT = kolmato2
EXTRA =

# the sources are in src/ in the released layout, next to this file in the development tree
SRCDIR := $(if $(wildcard src/main.c),src,.)
SRC_NOMAIN_NAMES = bitboards.c book.c check.c counters.c eval.c evaluate.c fen.c history.c magic.c move.c move_generation.c movepick.c \
                   params.c parser.c perft.c print.c random.c search.c see.c tables.c threads.c timeman.c uci.c
EVAL_C = $(SRCDIR)/eval.c
SRC_NOMAIN := $(subst $(SRCDIR)/eval.c,$(EVAL_C),$(addprefix $(SRCDIR)/,$(SRC_NOMAIN_NAMES)))
SRC := $(SRC_NOMAIN) $(SRCDIR)/main.c

ifeq ($(PORTABLE),1)
CPUFLAGS = -mpopcnt
else
# BMI1 and BMI2 are looked up in /proc/cpuinfo (or asked from the compiler where there is none), and the compiler has to accept the flags
HAVE_BMI := $(shell { if [ -r /proc/cpuinfo ]; then grep -qw bmi1 /proc/cpuinfo && grep -qw bmi2 /proc/cpuinfo; \
                      else echo | $(CC) -march=native -dM -E - 2>/dev/null | grep -q __BMI2__; fi; } && \
                    echo 'int main(void){return 0;}' | $(CC) -mbmi -mbmi2 -x c - -o /dev/null 2>/dev/null && echo 1)
CPUFLAGS = -mpopcnt $(if $(HAVE_BMI),-mbmi -mbmi2)
endif

CFLAGS_ALL = -pthread -Ofast -I$(SRCDIR) $(CPUFLAGS) $(EXTRA)
BIN = $(abspath $(OUT))
PGO_DIR = pgo_data

.PHONY: all pgo tune test bench book clean print-env

all:
	$(CC) $(CFLAGS_ALL) $(SRC) -o $(OUT) -lm

# 1. an instrumented engine searches six positions to learn which code paths are hot, 2. the final engine is built with that profile.
# PGO changes the speed only, never the search results (same node counts as `make`)
pgo:
	rm -rf $(PGO_DIR) && mkdir $(PGO_DIR)
	$(CC) $(CFLAGS_ALL) -flto -fprofile-generate -fprofile-dir=$(PGO_DIR) -fprofile-update=single $(SRC) -o $(OUT) -lm
	@printf '%s\n' \
	    'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1' \
	    'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10' \
	    'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10' \
	    'r1b2rk1/2q1bppp/p2p1n2/np2p3/3NP3/1BN1BP2/PPP1Q1PP/R3R1K1 w - - 0 14' \
	    '2r3k1/pp3ppp/8/3q4/3P4/2P2Q2/P4PPP/4R1K1 w - - 0 30' \
	    '8/5pk1/6p1/8/8/6P1/5PK1/3R4 w - - 0 50' | \
	    while read -r p c k e h f; do $(BIN) -b $$p $$c $$k $$e $$h $$f -d 12 -t 1000 > /dev/null 2>&1 || true; done
	$(CC) $(CFLAGS_ALL) -flto -fprofile-use -fprofile-dir=$(PGO_DIR) -fprofile-correction -Wno-missing-profile $(SRC) -o $(OUT) -lm
	rm -rf $(PGO_DIR)
	@echo "built $(BIN) with PGO"

tune:
	$(MAKE) pgo OUT=kolmato2_tune EXTRA="-DTUNE_SEARCH $(EXTRA)"

test:
	bash tools/regress.sh

bench: all
	python3 tools/bench.py 13 ./$(OUT)

book:
	@for z in 7z 7zz 7za; do if command -v $$z >/dev/null 2>&1; then $$z x -y book/18moves.7z && exit 0; fi; done; \
	echo "no 7z found: install p7zip-full (Debian/Ubuntu: sudo apt install 7zip or p7zip-full) or 7-Zip, then extract book/18moves.7z here"; exit 1

clean:
	rm -rf $(PGO_DIR) kolmato2 kolmato2_tune

# the build variables for the shell scripts of tools/:  eval "$(make -s print-env)"
print-env:
	@echo 'SRCDIR="$(SRCDIR)"'
	@echo 'SRC_NOMAIN="$(SRC_NOMAIN)"'
	@echo 'SRC="$(SRC)"'
	@echo 'CPUFLAGS="$(CPUFLAGS)"'
