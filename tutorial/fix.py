#!/usr/bin/env python3
r"""
Rewrite \ct{...} to \ct|...| wherever the argument was written with LaTeX
escapes or wrapped across a line.

\ct is \lstinline and therefore verbatim, so `\#foo' prints the backslash and
a newline inside the argument is a hard error.  Writing the escapes is the
habit of anyone who has written LaTeX before, so this fixes them rather than
asking the author not to have the habit.  check.py is the gate; this is the
repair.
"""
import re, glob

def unescape(a):
    return re.sub(r'\\([\\&$_#%^{}])', r'\1', a).replace('\\ldots', '...').replace('{}', '')

def pick_delim(a):
    for d in '|!+~/':
        if d not in a:
            return d
    raise SystemExit('no delimiter available for: ' + a)

for f in sorted(glob.glob('chapters/*.tex')):
    text = open(f).read()
    out, i, changed = [], 0, 0
    while True:
        m = re.compile(r'\\ct\{').search(text, i)
        if not m:
            out.append(text[i:]); break
        j, depth, nl = m.end(), 1, False
        while j < len(text) and depth:
            c = text[j]
            if c == '{': depth += 1
            elif c == '}': depth -= 1
            elif c == '\n': nl = True
            j += 1
        arg = text[m.end():j-1]
        if nl:
            arg = ' '.join(arg.split())
        if re.search(r'\\[\\&$_#%^{}]', arg) or '\\ldots' in arg or nl:
            new = unescape(arg)
            d = pick_delim(new)
            out.append(text[i:m.start()]); out.append('\\ct' + d + new + d)
            changed += 1
        else:
            out.append(text[i:j])
        i = j
    if changed:
        open(f, 'w').write(''.join(out))
        print(f'{f}: fixed {changed}')
