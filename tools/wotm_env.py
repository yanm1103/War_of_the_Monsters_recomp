#!/usr/bin/env python3
"""Confere a tabela de variaveis WOTM_* de docs/guides/NATIVE.md contra o codigo.

O codigo (native/ e o patch do runtime em tools/runtime_patch/) e a fonte: toda
variavel lida por getenv("WOTM_...") tem que estar na tabela entre os marcadores
<!-- WOTM_ENV:START --> e <!-- WOTM_ENV:END -->, e toda variavel da tabela tem que
existir no codigo. As descricoes sao escritas a mao na propria tabela (uma linha
pode agrupar varias variaveis na primeira coluna).

  python tools/wotm_env.py           # lista o que falta na tabela e o que sobrou nela; sai 1 se houver
  python tools/wotm_env.py --write   # acrescenta ao fim da tabela as que faltam (descricao vazia) e tira as que sobraram
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOC = os.path.join(ROOT, "docs", "guides", "NATIVE.md")
SRC_DIRS = [os.path.join(ROOT, "native"), os.path.join(ROOT, "tools", "runtime_patch")]
SRC_EXT = (".hpp", ".h", ".cpp", ".inc", ".patch")
START, END = "<!-- WOTM_ENV:START -->", "<!-- WOTM_ENV:END -->"
VAR = re.compile(r'"(WOTM_[A-Z0-9_]+)"')
VAR_DOC = re.compile(r"`(WOTM_[A-Z0-9_]+)")


def scan_code():
    found = {}
    for d in SRC_DIRS:
        for dirpath, _, files in os.walk(d):
            for f in files:
                if not f.endswith(SRC_EXT):
                    continue
                p = os.path.join(dirpath, f)
                with open(p, encoding="utf-8", errors="replace") as fh:
                    for n, line in enumerate(fh, 1):
                        for v in VAR.findall(line):
                            found.setdefault(v, "%s:%d" % (os.path.relpath(p, ROOT).replace("\\", "/"), n))
    return found


def read_doc():
    with open(DOC, encoding="utf-8") as fh:
        text = fh.read()
    a, b = text.index(START), text.index(END)
    block = text[a + len(START):b]
    rows = [l for l in block.splitlines() if l.startswith("|")]
    head, body = rows[:2], rows[2:]
    return text, a, b, head, body


def main():
    write = "--write" in sys.argv
    code = scan_code()
    text, a, b, head, body = read_doc()
    in_doc = {}
    for row in body:
        first = row.split("|")[1]
        for v in VAR_DOC.findall(first):
            in_doc.setdefault(v, row)
    missing = sorted(v for v in code if v not in in_doc)
    stale = sorted(v for v in in_doc if v not in code)
    for v in missing:
        print("FALTA na tabela: %s (%s)" % (v, code[v]))
    for v in stale:
        print("SOBRA na tabela (nao existe no codigo): %s" % v)
    if not write:
        print("%d variaveis no codigo, %d na tabela" % (len(code), len(in_doc)))
        return 1 if (missing or stale) else 0
    kept = [r for r in body if not any(v in stale for v in VAR_DOC.findall(r.split("|")[1]))]
    for v in missing:
        kept.append("| `%s` | (descrever; lida em `%s`) |" % (v, code[v]))
    block = "\n" + "\n".join(head + kept) + "\n"
    with open(DOC, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text[:a + len(START)] + block + text[b:])
    print("tabela reescrita: +%d -%d" % (len(missing), len(stale)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
