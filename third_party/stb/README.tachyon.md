# stb_truetype in Tachyon

| | |
|---|---|
| Upstream | https://github.com/nothings/stb |
| Version | **stb_truetype.h v1.26** |
| Commit | `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d` |
| License | MIT or public domain, your choice (see `LICENSE`) |

Only `stb_truetype.h` is here. It reads TrueType fonts and rasterises
glyphs with antialiasing. Tachyon uses it for the fonts Windows 95 already
has (decision 27); `src/core/tc_font_stb.c` compiles it, with Tachyon's
configuration: `STBTT_assert` does nothing, and the compiler warnings
Tachyon turns on are relaxed for that one file.

**No upstream file is modified.**

stb_truetype trusts its input: it's fine for Windows' own fonts, but not for
fonts downloaded from the web. `@font-face` (Sprint 4) needs a loader that
checks fonts first, or a different rasteriser.

To update, copy `stb_truetype.h` from a newer commit, update the commit
above, rebuild both presets and check that `win95check` still passes.
