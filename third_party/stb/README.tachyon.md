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

stb_truetype trusts its input: it's fine for Windows' own fonts, but fonts
downloaded from the web go through `src/core/tc_font_check.c` first
(decision 38), and the font fuzzer runs both.

**Local changes**, each marked "Tachyon:" in `stb_truetype.h`, found by the
font fuzzer (UBSan):

1. `ttULONG` and `ttLONG` shift each byte as unsigned: a byte of 128 or
   more shifted into an `int`'s sign bit is undefined behaviour.
2. A composite glyph's transformed coordinates are clamped to a `short`
   (`stbtt__clamp16`): a float out of range converted to a `short` is
   undefined behaviour.

To update, copy `stb_truetype.h` from a newer commit, update the commit
above, rebuild both presets and check that `win95check` still passes.
