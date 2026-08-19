#!/usr/bin/env python3
r"""
Lint the tutorial's LaTeX for the two mistakes this book keeps making.

\ct is \lstinline, so its argument is VERBATIM: a backslash escape inside it
prints the backslash, and a newline inside it is a hard error.  Both are easy
to write by habit and neither is obvious in the output, so they are checked
here rather than discovered in a PDF.
"""
import re, sys, glob

bad = []
for f in sorted(glob.glob('chapters/*.tex')):
    text = open(f).read()
    for m in re.finditer(r'\\ctp?\{', text):
        i, depth, broke = m.end(), 1, False
        while i < len(text) and depth:
            ch = text[i]
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
            elif ch == '\n':
                broke = True
                break
            i += 1
        line = text[:i].count('\n') + 1
        verbatim = not text[m.start():m.end()].startswith('\\ctp')
        if broke:
            bad.append((f, line, 'spans a line', ''))
            continue
        arg = text[m.end():i - 1]
        if verbatim and re.search(r'\\[\\&$_#%^{}]', arg):
            bad.append((f, line, 'escaped char inside verbatim \\ct', arg))
        if '\\ct' in arg:
            bad.append((f, line, 'nested \\ct', arg))

# \ctp takes a braced argument, not lstinline delimiters.
for f in sorted(glob.glob('chapters/*.tex')):
    for n, line in enumerate(open(f), 1):
        if '\\ctp|' in line:
            bad.append((f, n, '\\ctp with | delimiters (it takes braces)', line.strip()))

for f in sorted(glob.glob('chapters/*.tex')):
    for n, line in enumerate(open(f), 1):
        if re.search(r'\\item\[\\ct[|{]', line):
            bad.append((f, n, '\\ct inside an \\item label (use \\ctp)', line.strip()[:60]))

for f, line, why, arg in bad:
    print(f'{f}:{line}: {why}  {arg}')
sys.exit(1 if bad else 0)
