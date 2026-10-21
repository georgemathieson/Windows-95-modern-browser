#!/bin/sh
# Fuzz the HTML parser for a while: tools/fuzz.sh [SECONDS] [JOBS]
# Needs clang and its runtime libraries (Ubuntu: clang libclang-rt-18-dev).
# Crashes are written to build/fuzz/crashes/.
set -e
seconds=${1:-60}
jobs=${2:-1}
cmake --preset fuzz >/dev/null
cmake --build --preset fuzz --target fuzz_html
python3 tools/gen_fuzz_corpus.py tests/data/wpt-html-parsing tests/data/html5lib-tests/tokenizer build/fuzz/corpus
mkdir -p build/fuzz/crashes
cd build/fuzz # libFuzzer writes its job logs to the current directory
exec tests/fuzz/fuzz_html -max_total_time="$seconds" -jobs="$jobs" -workers="$jobs" \
    -max_len=4096 -timeout=10 -rss_limit_mb=2048 -artifact_prefix=crashes/ corpus
