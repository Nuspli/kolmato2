#!/usr/bin/env python3
"""Generate training data for the texel tuner by letting kolmato2 play against itself.

usage: datagen.py --kolmato2 ../kolmato2 --games 2000 --workers 6 --depth 7 --out positions.csv

Every game starts with a few random moves (so that the games differ) and is only used if the engine thinks the
resulting position is roughly balanced. The engine then plays both sides at a fixed search depth (fast and
reproducible, the time per move does not matter). From each game some "quiet" positions are written to the output
file together with the result of the game:     <fen>,<result>,<game>     (result: 1.0 white won, 0.5 draw, 0.0 black won)

Why quiet positions only? The tuner evaluates a position with the static evaluation. If a capture is pending, the static
evaluation is meaningless (it is the search's job to resolve the exchange), so such positions would only add noise. We
skip positions where the side to move is in check or where the move the engine actually played is a capture,
a promotion or gives check.

The result of a game is decided by the rules of chess (checkmate, stalemate, repetition, fifty moves, insufficient
material) or by adjudication when the engine's own score says the game is decided for a long time (the same way
the match scripts do it). Adjudication makes the games much shorter and costs a little accuracy in the labels.
"""
import argparse, os, random, re, subprocess, sys, time
from multiprocessing import Pool

import chess

ARGS = None


def engine(board, depth):
    """Ask kolmato2 for its move and its score (from white's point of view). A new process per move: no hash table or
    history is kept, which is fine for generating data."""
    f = board.fen(en_passant='fen').split()
    cmd = [ARGS.kolmato2, '-b', f[0], f[1], f[2], f[3], f[4], f[5], '-d', str(depth), '-t', '10', '-hash', '16']
    out = subprocess.run(cmd, cwd='/tmp', capture_output=True, text=True, timeout=120).stdout
    move = re.findall(r'(?:final|forced) move: (\S+)', out)
    score = re.findall(r'>>> best move: \S+ value: (-?\d+)', out)
    return (move[-1] if move else None), (int(score[-1]) if score else 0)


def play_game(rng):
    """Returns (list of (fen, ply) candidates, result) or None if the game was not usable."""
    board = chess.Board()
    # a few random moves to get a different game every time, then check that the position is balanced
    for _ in range(rng.choice([6, 8, 8, 10])):
        board.push(rng.choice(list(board.legal_moves)))
        if board.is_game_over():
            return None
    _, score = engine(board, 5)
    if abs(score) > 150:
        return None

    candidates, history = [], []
    result = None
    while result is None:
        if board.is_game_over(claim_draw=True):
            outcome = board.result(claim_draw=True)
            result = {'1-0': 1.0, '0-1': 0.0}.get(outcome, 0.5)
            break
        if board.ply() >= 240:
            result = 0.5
            break
        move, score = engine(board, ARGS.depth)
        if move is None:
            return None
        move = chess.Move.from_uci(move)
        if move not in board.legal_moves:
            return None
        history.append(score)
        quiet = (not board.is_check() and not board.is_capture(move) and move.promotion is None
                 and not board.gives_check(move) and abs(score) <= 2000 and board.ply() >= 12)
        if quiet and rng.random() < ARGS.sample_rate:
            candidates.append(board.fen())
        board.push(move)
        # adjudication
        if len(history) >= 6 and all(s >= 600 for s in history[-6:]):
            result = 1.0
        elif len(history) >= 6 and all(s <= -600 for s in history[-6:]):
            result = 0.0
        elif board.ply() >= 80 and len(history) >= 20 and all(abs(s) <= 20 for s in history[-20:]):
            result = 0.5
    return candidates, result


def worker(seed_and_games):
    seed, games = seed_and_games
    rng = random.Random(seed)
    lines, used = [], 0
    while used < games:
        game = play_game(rng)
        if game is None:
            continue
        candidates, result = game
        # the third column identifies the game, the tuner keeps all positions of a game together (training or validation)
        lines.extend("%s,%.1f,%d" % (fen, result, seed * 1000 + used) for fen in candidates)
        used += 1
    return lines


def main():
    global ARGS
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--kolmato2', required=True, help='path of the engine binary')
    ap.add_argument('--games', type=int, default=1000, help='number of games to play')
    ap.add_argument('--workers', type=int, default=max(1, (os.cpu_count() or 2) - 2), help='games played in parallel')
    ap.add_argument('--depth', type=int, default=7, help='search depth of the engine in the games')
    ap.add_argument('--sample-rate', type=float, default=0.4, help='fraction of the quiet positions that are written')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--out', default='positions.csv')
    ARGS = ap.parse_args()
    ARGS.kolmato2 = os.path.abspath(ARGS.kolmato2)

    per_chunk = 5       # games per task, results are written after every chunk so that nothing is lost if the run is stopped
    chunks = [(ARGS.seed * 1000003 + i, per_chunk) for i in range((ARGS.games + per_chunk - 1) // per_chunk)]
    start, total_games, total_positions = time.time(), 0, 0
    with Pool(ARGS.workers) as pool, open(ARGS.out, 'a') as out:
        for lines in pool.imap_unordered(worker, chunks):
            out.write("\n".join(lines) + "\n")
            out.flush()
            total_games += per_chunk
            total_positions += len(lines)
            rate = total_games / (time.time() - start) * 60
            print("%d games, %d positions (%.0f games/minute)" % (total_games, total_positions, rate), flush=True)


if __name__ == '__main__':
    main()
