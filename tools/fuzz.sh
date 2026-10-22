#!/bin/sh
# Fuzz for a while: tools/fuzz.sh [SECONDS] [JOBS] [TARGET]
#   html    the HTML parser (the default)
#   render  CSS, styles, layout and painting, on pages and style sheets
#   image   the image decoders (Wuffs, ICO) and animation
# Needs clang and its runtime libraries (Ubuntu: clang libclang-rt-18-dev).
# Crashes are written to build/fuzz/crashes/.
set -e
seconds=${1:-60}
jobs=${2:-1}
target=${3:-html}
cmake --preset fuzz >/dev/null
cmake --build --preset fuzz --target "fuzz_$target"
case $target in
html)
    python3 tools/gen_fuzz_corpus.py tests/data/wpt-html-parsing tests/data/html5lib-tests/tokenizer \
        build/fuzz/corpus
    corpus=corpus max_len=4096 ;;
render)
    python3 tools/gen_render_corpus.py tests/reftests tests/data/css-parsing-tests src/core/ua.css \
        build/fuzz/corpus-render
    corpus=corpus-render max_len=8192 ;;
image)
    mkdir -p build/fuzz/corpus-image
    cp tests/data/images/* build/fuzz/corpus-image/
    corpus=corpus-image max_len=65536 ;;
*)
    echo "fuzz.sh: no target $target (html, render or image)" >&2
    exit 2 ;;
esac
mkdir -p build/fuzz/crashes
cd build/fuzz # libFuzzer writes its job logs to the current directory
exec "tests/fuzz/fuzz_$target" -max_total_time="$seconds" -jobs="$jobs" -workers="$jobs" \
    -max_len="$max_len" -timeout=10 -rss_limit_mb=2048 -artifact_prefix="crashes/$target-" "$corpus"
