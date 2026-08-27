# Provenance

This directory is `src/main/frontend` of
[Kiss](https://github.com/blakemcbride/Kiss) at commit
`946c7774b1ea4a03722d37f07853930f5138efa6` (2026-08-18), copied whole:
`index.html`, `login.html`, `index.js`, `login.js`, `routes.js`,
`SystemInfo.js`, the `normalize` sheets, `robots.txt`, `kiss/` (the Kiss
front-end framework), `screens/` (the demo's ten screens), `mobile/`, and
`lib/`. Kiss is BSD 2-Clause by the same author, the licence of this tree.

**Three edits.** `SystemInfo.js`: `SystemInfo.sameOriginBackend = true`. Kiss's
source ships it `false` and its build stamps `true` into the copy Tomcat
serves; this copy is served by `RestServer` from `demo/server.json`'s
document root, so the stamp is applied here, with a comment saying so. With
it, `index.js` uses the page's own origin whatever the port. And the name the
pages show: where their text said `Kiss` it says `Smalltalk-2026` — the
title in `index.html`, the two login pages, and the `Framework`, `SQLAccess`,
`Export` and `Report` screens (Blake, 2026-08-26). The framework under
`kiss/`, its globals, and the comments keep their name. And the `RestServices`
screen: Kiss's has three buttons — *Call Groovy*, *Call Java*, *Call Lisp*,
one per language its back end can be written in — and this one has one,
*Call Service*, calling `services/MyService`, since every service here is
Smalltalk and three buttons doing the same thing offered a choice that was
not one (Blake, 2026-08-26). `RestServices.html` and `RestServices.js` are
rewritten for it, and `SQLAccess.js` names the same service for
`hasDatabase`. Nothing else differs: `diff -r` against the Kiss commit above
shows those lines and this file.

**Third-party files in `lib/`**

| file | what | licence |
|---|---|---|
| `ag-grid-community.min.noStyle.js` | AG Grid Community 34.0.1 | MIT |
| `ckeditor.js` | CKEditor 5 (CKSource) | GPL-2.0-or-later, or commercial |

Both are as Kiss ships them. CKEditor is loaded on every page by
`kiss/bootstrap.js` and used by the framework's `Editor` component; no demo
screen uses it, and it is kept because the copy is verbatim (Blake,
2026-08-26).

**Not carried over.** Kiss's build stamps `app-mode` in `index.html` to
`production` for a release; this copy stays `development`, so the browser
caches nothing between reloads — right for a demo whose point is editing it
while it runs. Kiss's `SecurityHeadersFilter` sends a Content-Security-Policy
that pins the hash of `index.html`'s inline bootstrap kernel; this server
sends no CSP, so the kernel runs unpinned.

**Bringing it up to date** is `cp -r` from Kiss, the stamp, the rename, the
one button, and a new commit hash here. `WebDemoTest` checks the stamp and
the button, so a copy that forgot either fails the suite.
