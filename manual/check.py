#!/usr/bin/env python3
r"""
Lint the manual's LaTeX for the mistakes this book keeps making.

\ct is \lstinline, so its argument is VERBATIM: a backslash escape inside it
prints the backslash, and a newline inside it is a hard error.  Both are easy
to write by habit and neither is obvious in the output, so they are checked
here rather than discovered in a PDF.

\ctp is the opposite problem.  It is ordinary text in a typewriter font, for
the places \lstinline cannot go, and TeX therefore reads its argument: a bare
~ is a non-breaking space and the tilde DISAPPEARS, \~{} and \^{} are accent
commands and set as raised accents rather than the ASCII characters, and ' is
a curly closing quote and not the quote a Smalltalk string literal is written
with.  Every one of those silently prints a character the reader cannot type
and the compiler will not accept, so they are checked too.  (>> and -- would
ligature into a guillemet and an en dash; \DisableLigatures in preamble.tex
handles those, which is why they are not listed here.)
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

#  Characters \ctp cannot set as themselves.  The replacement is \ct, which
#  is verbatim, unless the site is a moving argument -- a section title or an
#  \item label -- and then the character has to be spelled with the T1 symbol
#  command: \textasciitilde, \textasciicircum, \textquotesingle.
CTP_TRAPS = (
    (r'(?<!\\)~',      'bare ~ in \\ctp (sets as a non-breaking space; the tilde vanishes)'),
    (r'\\~',           r'\~ in \ctp (an accent, not an ASCII tilde) -- use \ct or \textasciitilde'),
    (r'\\\^',          r'\^ in \ctp (an accent, not a caret) -- use \ct or \textasciicircum'),
    (r"'",             "' in \\ctp (sets as a curly quote) -- use \\ct or \\textquotesingle"),
)
for f in sorted(glob.glob('chapters/*.tex')):
    text = open(f).read()
    for m in re.finditer(r'\\ctp\{', text):
        i, depth = m.end(), 1
        while i < len(text) and depth:
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
            i += 1
        arg = text[m.end():i - 1]
        for pattern, why in CTP_TRAPS:
            if re.search(pattern, arg):
                bad.append((f, text[:m.start()].count('\n') + 1, why, arg))

for f, line, why, arg in bad:
    print(f'{f}:{line}: {why}  {arg}')
sys.exit(1 if bad else 0)
