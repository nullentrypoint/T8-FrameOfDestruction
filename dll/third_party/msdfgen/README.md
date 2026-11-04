# MSDFgen core

Unmodified `core/`, `msdfgen.h` and `LICENSE.txt` from
[Chlumsky/msdfgen v1.13](https://github.com/Chlumsky/msdfgen/tree/v1.13).
License: MIT (see `LICENSE.txt`).

`msdfgen.props` is the local MSBuild integration. It lists the core compilation
units needed for generation and defines `MSDFGEN_PUBLIC` for a static build.
No FreeType, Skia, OpenMP or external DLL is required. Font outline extraction
uses the project's existing stb_truetype; see `dll/msdf_text.cpp`.
