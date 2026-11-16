import sys, chess, chess.engine
path=sys.argv[1]; n=int(sys.argv[2])
me=chess.engine.SimpleEngine.popen_uci(path); me.configure({"Hash":16})
op=chess.engine.SimpleEngine.popen_uci(sys.argv[3] if len(sys.argv)>3 else path); op.configure({"Hash":16})
b=chess.Board(); clock=8.0; missing=0; total=0; plies=0
while not b.is_game_over() and plies<n*2:
    if b.turn==chess.WHITE:
        lim=chess.engine.Limit(white_clock=clock,black_clock=clock,white_inc=0.1,black_inc=0.1)
        r=me.play(b,lim,ponder=True,game="g",info=chess.engine.INFO_ALL)
        total+=1
        if not all(k in r.info for k in("score","depth","pv")): missing+=1; print("MISSING at ply",plies,sorted(r.info))
        b.push(r.move)
    else:
        r=op.play(b,chess.engine.Limit(time=0.02)); b.push(r.move)
    plies+=1
me.quit(); op.quit(); print(path,"moves",total,"missing info",missing)
