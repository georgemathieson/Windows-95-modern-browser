#!/usr/bin/env python3
"""Write a seed corpus for tests/fuzz/fuzz_html.c from the parser test suites.

Usage: gen_fuzz_corpus.py TREE_DIR TOKENIZER_DIR OUT_DIR
"""

import glob
import hashlib
import json
import os
import re
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    tree_dir, tok_dir, out = sys.argv[1:]
    os.makedirs(out, exist_ok=True)
    inputs = []
    for path in glob.glob(os.path.join(tree_dir, "*.dat")):
        text = open(path, encoding="utf-8", newline="").read()
        inputs += re.findall(r"#data\n(.*?)\n#errors", text, re.S)
    for path in glob.glob(os.path.join(tok_dir, "*.test")):
        for t in json.load(open(path, encoding="utf-8")).get("tests", []):
            inputs.append(t["input"])
    for i, data in enumerate(inputs):
        body = data.encode("utf-8", "surrogatepass")
        # The first byte picks the mode: spread the seeds over all of them.
        blob = bytes([i % 256]) + body
        name = hashlib.sha1(blob).hexdigest()
        with open(os.path.join(out, name), "wb") as f:
            f.write(blob)
    print("%d seeds in %s" % (len(inputs), out))


if __name__ == "__main__":
    main()
