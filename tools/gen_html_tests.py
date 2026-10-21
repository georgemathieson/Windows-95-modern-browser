#!/usr/bin/env python3
"""Turn the HTML parser test suites into C tables for test_html.c.

Usage: gen_html_tests.py TOKENIZER_DIR TREE_DIR ENCODING_DIR KNOWN_FAILURES OUTPUT.c

- TOKENIZER_DIR: html5lib-tests' tokenizer/*.test (JSON).
- TREE_DIR: web-platform-tests' tree-construction *.dat files.
- ENCODING_DIR: html5lib-tests' encoding-sniffing *.dat files.
- KNOWN_FAILURES: test ids Tachyon is known to get wrong, one per line with a
  reason after '#'. A known failure that passes fails the run, so the list
  can't go stale.

Expected tokens are written in the same one-line-per-token format the C test
serializes Tachyon's tokens to (see ser() below and test_html.c).
"""

import glob
import json
import os
import re
import sys

STATES = {
    "Data state": "TC_TOK_DATA",
    "PLAINTEXT state": "TC_TOK_PLAINTEXT",
    "RCDATA state": "TC_TOK_RCDATA",
    "RAWTEXT state": "TC_TOK_RAWTEXT",
    "Script data state": "TC_TOK_SCRIPT_DATA",
    "CDATA section state": "TC_TOK_CDATA_SECTION",
}


def wtf8(s):
    """UTF-8, letting lone surrogates through (tests use them)."""
    s = s.encode("utf-16", "surrogatepass").decode("utf-16", "surrogatepass")  # join pairs
    return s.encode("utf-8", "surrogatepass")


def c_bytes(b):
    """A C string literal for any bytes ('?' escaped against trigraphs)."""
    if b is None:
        return "NULL"
    out = []
    for x in b:
        ch = chr(x)
        if ch in '"\\?':
            out.append("\\" + ch)
        elif 0x20 <= x < 0x7F:
            out.append(ch)
        else:
            out.append("\\%03o" % x)
    return '"' + "".join(out) + '"'


def c_str(s):
    return "NULL" if s is None else c_bytes(wtf8(s))


def ser(s):
    """A string in the tests' token format: JSON-like quoting, raw non-ASCII."""
    if s is None:
        return "null"
    out = ['"']
    for ch in s:
        cp = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif cp < 0x20 or cp == 0x7F:
            out.append("\\u%04x" % cp)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def ser_token(tok):
    kind = tok[0]
    if kind == "DOCTYPE":
        return '["DOCTYPE", %s, %s, %s, %s]' % (ser(tok[1]), ser(tok[2]), ser(tok[3]),
                                               "true" if tok[4] else "false")
    if kind == "StartTag":
        attrs = ", ".join("%s: %s" % (ser(k), ser(v)) for k, v in tok[2].items())
        return '["StartTag", %s, {%s}%s]' % (ser(tok[1]), attrs, ", true" if len(tok) > 3 and tok[3] else "")
    if kind == "EndTag":
        return '["EndTag", %s]' % ser(tok[1])
    if kind == "Comment":
        return '["Comment", %s]' % ser(tok[1])
    if kind == "Character":
        return '["Character", %s]' % ser(tok[1])
    if kind == "ProcessingInstruction":
        return '["ProcessingInstruction", %s, %s]' % (ser(tok[1]), ser(tok[2]))
    sys.exit("gen_html_tests.py: unknown token %r" % (tok,))


def unescape(s):
    return re.sub(r"\\u([0-9a-fA-F]{4})", lambda m: chr(int(m.group(1), 16)), s)


def coalesce(tokens):
    out = []
    for tok in tokens:
        if tok[0] == "Character" and out and out[-1][0] == "Character":
            out[-1] = ["Character", out[-1][1] + tok[1]]
        else:
            out.append(list(tok))
    return out


def tokenizer_cases(directory, known, used):
    rows = []
    for path in sorted(glob.glob(os.path.join(directory, "*.test"))):
        name = os.path.basename(path)
        data = json.load(open(path, encoding="utf-8"))
        if "tests" not in data:
            continue  # xmlViolation.test: XML-compatible output, not what browsers do
        for i, t in enumerate(data["tests"], 1):
            double = t.get("doubleEscaped", False)
            text = unescape(t["input"]) if double else t["input"]
            output = t["output"]
            if double:
                output = [[unescape(x) if isinstance(x, str) else
                           ({unescape(k): unescape(v) for k, v in x.items()} if isinstance(x, dict) else x)
                           for x in tok] for tok in output]
            expected = "\n".join(ser_token(tok) for tok in coalesce(output))
            # Sorted: errors at the same position come in either order.
            errors = "\n".join(sorted(e["code"] for e in t.get("errors", [])))
            for state in t.get("initialStates", ["Data state"]):
                ident = "%s:%d %s" % (name, i, state)
                reason = known.get(ident)
                if reason is not None:
                    used.add(ident)
                rows.append("    { %s, %s, %d, %s, %s, %s, %d, %s, %s },\n" % (
                    c_str(ident), c_bytes(wtf8(text)), len(wtf8(text)), STATES[state],
                    c_str(t.get("lastStartTag")), c_bytes(wtf8(expected)), len(wtf8(expected)),
                    c_str(errors), c_str(reason)))
    return rows


def split_dat(text):
    """Split a .dat file into tests: dicts of section name -> text."""
    tests = []
    for chunk in re.split(r"\n\n(?=#data\n|#description\n)", text):
        sections, current = {}, None
        lines = chunk.split("\n")
        for line in lines:
            if line in ("#data", "#errors", "#new-errors", "#document-fragment", "#script-off",
                        "#script-on", "#document", "#description", "#encoding"):
                current = line[1:]
                sections[current] = []
            elif current is not None:
                sections[current].append(line)
        tests.append({k: "\n".join(v) for k, v in sections.items()})
    return tests


def tree_cases(directory, known, used):
    rows = []
    skipped = 0
    for path in sorted(glob.glob(os.path.join(directory, "*.dat"))):
        name = os.path.basename(path)
        text = open(path, encoding="utf-8", newline="").read()  # keep CRs: tests use them
        if text.endswith("\n"):
            text = text[:-1]
        for i, t in enumerate(split_dat(text), 1):
            if name.startswith("scripted_"):
                skipped += 1
                continue
            document = t["document"]
            if document.endswith("\n"):
                document = document[:-1]
            if "script-on" in t:
                modes = [1]
            elif "script-off" in t:
                modes = [0]
            else:
                modes = [0, 1]
            context = t.get("document-fragment")
            for scripting in modes:
                ident = "%s:%d%s" % (name, i, " script" if scripting else "")
                reason = known.get(ident)
                if reason is not None:
                    used.add(ident)
                data = wtf8(t["data"])
                rows.append("    { %s, %s, %d, %s, %d, %s, %s },\n" % (
                    c_str(ident), c_bytes(data), len(data), c_str(context.strip() if context else None),
                    scripting, c_str(document), c_str(reason)))
    return rows, skipped


def encoding_cases(directory, known, used):
    rows = []
    for path in sorted(glob.glob(os.path.join(directory, "*.dat"))):
        name = os.path.basename(path)
        text = open(path, encoding="utf-8", errors="surrogateescape").read()
        for i, t in enumerate(split_dat(text.rstrip("\n")), 1):
            ident = "%s:%d" % (name, i)
            reason = known.get(ident)
            if reason is not None:
                used.add(ident)
            data = t["data"].encode("utf-8", "surrogateescape")
            rows.append("    { %s, %s, %d, %s, %s },\n" % (
                c_str(ident), c_bytes(data), len(data), c_str(t["encoding"].strip()), c_str(reason)))
    return rows


def load_known(path):
    known = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            body, _, reason = line.partition("#")
            body = body.strip()
            if body:
                known[body] = reason.strip() or "known failure"
    return known


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    known = load_known(sys.argv[4])
    used = set()
    tok = tokenizer_cases(sys.argv[1], known, used)
    tree, skipped = tree_cases(sys.argv[2], known, used)
    enc = encoding_cases(sys.argv[3], known, used)
    stale = sorted(set(known) - used)
    if stale:
        sys.exit("gen_html_tests.py: known failures that aren't tests: %r" % stale)
    with open(sys.argv[5], "w", encoding="utf-8") as out:
        out.write("/* Generated by tools/gen_html_tests.py from html5lib-tests and web-platform-tests. "
                  "Do not edit. */\n")
        out.write("#include \"html_cases.h\"\n#include \"tc_html_tokenizer.h\"\n\n")
        out.write("const tok_case tok_cases[] = {\n")
        out.writelines(tok)
        out.write("};\nconst size_t tok_case_count = %d;\n\n" % len(tok))
        out.write("const tree_case tree_cases[] = {\n")
        out.writelines(tree)
        out.write("};\nconst size_t tree_case_count = %d;\n" % len(tree))
        out.write("const size_t tree_case_skipped = %d;\n\n" % skipped)
        out.write("const enc_case enc_cases[] = {\n")
        out.writelines(enc)
        out.write("};\nconst size_t enc_case_count = %d;\n" % len(enc))


if __name__ == "__main__":
    main()
