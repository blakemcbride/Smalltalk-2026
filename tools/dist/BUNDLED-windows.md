# The libraries bundled with this distribution

Each is a copy of the library as the development package the build was
given holds it, unmodified, beside `st2026.exe`. Its licence text is in the
directory of `licenses/` named in the fourth column.

| File | What | Licence | Text | Source |
|---|---|---|---|---|
| `SDL3.dll` | SDL 3, the display, keyboard and mouse | zlib | `licenses/SDL3/` | <https://github.com/libsdl-org/SDL>, at the release in the DLL's properties (*Details*, *Product version*) |
| `libssl-*-x64.dll`, `libcrypto-*-x64.dll` | OpenSSL 3, for `https` — in a build without `NOTLS=1` | Apache-2.0 | `licenses/OpenSSL/` | <https://github.com/openssl/openssl>, at the version in the DLLs' properties |

ODBC is Windows' own `odbc32.dll` and the C runtime is Microsoft's
redistributable; neither is in this directory.
