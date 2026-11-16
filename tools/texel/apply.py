#!/usr/bin/env python3
"""Copies the tuned values from the tuner's output file into eval.c.
usage: apply.py eval.c tuned_params.txt       (eval.c is changed in place, the old version is kept as eval.c.orig)

Both files contain C definitions such as   int eval_doubled_pawn = 10;   or   int eval_mobility[4] = {4, 4, 2, 1};
For every definition in the tuned file the definition with the same name in eval.c is replaced."""
import re, shutil, sys
src, params = sys.argv[1], sys.argv[2]
text = open(src, newline='').read()
crlf = '\r\n' in text
text = text.replace('\r\n', '\n')
tuned = open(params).read()
DEF = re.compile(r'int (\w+)(\[\d+\])?\s*=\s*(\{[^}]*\}|-?\d+);')
count = 0
for m in DEF.finditer(tuned):
    name, new = m.group(1), m.group(0)
    pat = re.compile(r'int %s(\[\d+\])?\s*=\s*(\{[^}]*\}|-?\d+);' % re.escape(name))
    if len(pat.findall(text)) != 1:
        print("WARNING: %s found %d times in %s, not changed" % (name, len(pat.findall(text)), src)); continue
    text = pat.sub(lambda _: re.sub(r'\s+//.*', '', new), text, count=1)
    count += 1
shutil.copyfile(src, src + '.orig')
open(src, 'w', newline='').write(text.replace('\n', '\r\n') if crlf else text)
print("replaced %d definitions in %s (the old file is %s.orig)" % (count, src, src))
