#!/usr/bin/env python3
"""Refuses float tests in native/ that the runtime's compiler flags make meaningless.

The Windows runtime is built with /fp:fast, where the compiler assumes no NaN: a self-comparison (`x == x`, `x != x`) is
folded to a constant, so it cannot tell whether a value was set or is a NaN. Use a separate bool for "was this set" and
std::isnan / std::isfinite (library calls, not folded) to test a computed value.

    python tools/fplint.py            # every file of native/; exit 1 with findings
    python tools/fplint.py --staged   # only the files staged for the commit (the pre-commit hook)

A line that needs the comparison on purpose ends with the comment `fplint: ok`.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXT = (".hpp", ".h", ".cpp", ".inc")
# the same operand on both sides: a name with members, indexes and calls without arguments
OPERAND = r"[A-Za-z_][\w]*(?:(?:\.|->)[A-Za-z_]\w*|\[[\w+\- ]*\]|\(\))*"
SELF = re.compile(r"(?<![\w.>\]])(%s)\s*([!=]=)\s*\1(?![\w.\[(]|->)" % OPERAND)
COMMENT = re.compile(r"//.*$")
STRING = re.compile(r'"(?:\\.|[^"\\])*"')


def files(staged):
    if staged:
        out = subprocess.run(["git", "diff", "--cached", "--name-only", "--diff-filter=ACM", "--", "native"], cwd=ROOT,
                             capture_output=True, text=True).stdout.split("\n")
        return [os.path.join(ROOT, f) for f in out if f.endswith(EXT)]
    found = []
    for dirpath, _dirs, names in os.walk(os.path.join(ROOT, "native")):
        found += [os.path.join(dirpath, n) for n in names if n.endswith(EXT)]
    return sorted(found)


def scan(path):
    """[(line number, operand, operator, text)] of the self-comparisons of one file."""
    out = []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for n, line in enumerate(fh, 1):
            if "fplint: ok" in line:
                continue
            code = STRING.sub('""', COMMENT.sub("", line))
            for m in SELF.finditer(code):
                out.append((n, m.group(1), m.group(2), line.strip()))
    return out


def findings(staged=False):
    """[{file, line, text, message}] (file relative to the repo, forward slashes)."""
    out = []
    for p in files(staged):
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        for n, operand, op, text in scan(p):
            out.append({"file": rel, "line": n, "text": text,
                        "message": "`%s %s %s` is constant under /fp:fast: keep a bool for \"was set\", std::isnan for a computed value" % (operand, op, operand)})
    return out


def main():
    found = findings("--staged" in sys.argv[1:])
    for f in found:
        print("%s:%d: %s\n    %s" % (f["file"], f["line"], f["message"], f["text"]))
    if found:
        print("fplint: %d self-comparison(s). See tools/fplint.py." % len(found))
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
