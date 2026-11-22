#!/usr/bin/env python3
"""Entrada do kit de mods (tools/modkit.py): abre o kit numa aba do navegador (tools/modkit_web.py, pagina em tools/modkit_web/).

  python tools/modkit_gui.py        (ou pythonw, sem console; o launcher tem o botao "Mod kit"; e o WotM_ModKit.exe)
      --no-browser, --port N, --conf ARQ: os do tools/modkit_web.py

E o arquivo que o WotM_ModKit.exe empacota (tools/build_modkit.ps1). O exe tambem roda o proprio modkit.py com --modkit-cli: as
tarefas longas do kit (build, imports, export) rodam assim num processo separado (modkit_web.py, run_proc).
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modkit  # noqa: E402


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--modkit-cli":   # the exe running modkit.py for a task of the kit
        for fd, name in ((1, "stdout"), (2, "stderr")):
            if getattr(sys, name) is None:
                try:
                    setattr(sys, name, open(fd, "w", encoding="utf-8", buffering=1, closefd=False))
                except OSError:
                    setattr(sys, name, io.StringIO())
        try:
            sys.exit(modkit.main(sys.argv[2:]))
        except SystemExit as ex:
            if ex.code not in (None, 0) and not isinstance(ex.code, int):
                print(ex.code)
                sys.exit(1)
            raise
    import modkit_web
    sys.exit(modkit_web.main([a for a in sys.argv[1:] if a not in ("--web", "--tk")]))   # --web, --tk: flags of older kits


if __name__ == "__main__":
    main()
