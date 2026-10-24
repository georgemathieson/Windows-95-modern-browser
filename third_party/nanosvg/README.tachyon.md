# NanoSVG

| | |
|---|---|
| Upstream | https://github.com/memononen/nanosvg |
| Commit | `239e102ec2c691f2902e20ace2ed36ee4a35cfe6` (2026-07-09) |
| License | zlib (see `LICENSE.txt`) |

- `nanosvg.h`: the SVG parser (its own small XML reader, paths, shapes,
  transforms, viewBox, fills, strokes, gradients, basic style sheets).
- `nanosvgrast.h`: the anti-aliased scanline rasteriser.

Left out: the examples and build files. Tachyon compiles both headers in
`src/core/tc_svg.c` (decision 41), which feeds them SVG files and the
markup of inline `<svg>` elements and converts the result to
premultiplied pixels.

Local changes, each marked "Tachyon:" in the code:

1. `nanosvg.h`, `nsvg__gradientCoord`: a gradient's coordinates in
   `objectBoundingBox` units (the default) that are plain numbers, like
   `x2="1"`, are fractions of the shape's box, as SVG says; NanoSVG read
   them as pixels, so such gradients came out as their last colour.
2. `nanosvgrast.h`: float to int conversions that could be out of range,
   which is undefined behaviour (the image fuzzer found the first). Edges
   are kept within `NSVG__COORD_LIMIT` (262,144 pixels across, subsamples
   down, far past any canvas) as they're translated and scaled, so that
   stepping along them in fixed point can't overflow; `nsvg__addActive`
   limits an almost horizontal edge's step (`nsvg__fixLimit`); a round
   join's segment count survives a NaN angle; and `nsvg__curveDivs` caps
   its count at 1,000, where a huge radius made it infinite.
