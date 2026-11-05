#!/usr/bin/env python3
"""Fail-closed checks for an AppDir or an extracted AppImage payload."""
import re
import struct
import subprocess
import sys
from pathlib import Path

BANNED_SUFFIXES = {
    ".iso", ".elf", ".rom", ".ngp", ".ptr", ".tex", ".rtx", ".pss",
    ".snd", ".vb", ".vh", ".hd", ".bd", ".cpp", ".hpp", ".h",
    ".inc", ".s", ".asm", ".o", ".obj", ".log",
}
BANNED_DIRS = {"disc", "discroot", "iso", "game", "mc0", "mc1", "mon", "lvl", "mov"}
BANNED_NAMES = {"save.json", "settings.ini", "wotm.ini", "wotm.log"}
ALLOWED_FILES = {
    "AppRun", "wotm.desktop", "wotm.svg", ".DirIcon",
    "usr/bin/assets/Oswald-Medium.ttf", "usr/bin/assets/Oswald_OFL.txt",
    "usr/bin/assets/prompts.png", "usr/bin/assets/prompts_LICENSE.txt",
    "usr/share/applications/wotm.desktop",
    "usr/share/icons/hicolor/scalable/apps/wotm.svg",
    "usr/share/doc/wotm/README.txt", "usr/share/doc/wotm/THIRD_PARTY.txt",
    "usr/share/doc/wotm/BUILD.txt", "usr/share/doc/wotm/FFmpeg-BUILD.txt",
    "usr/share/doc/wotm/FFmpeg-LICENSE.md", "usr/share/doc/wotm/LGPL-2.1.txt",
    "usr/share/doc/wotm/PS2Recomp-LICENSE.md", "usr/share/doc/wotm/raylib-LICENSE.txt",
    "usr/share/doc/wotm/GLFW-LICENSE.md", "usr/share/doc/wotm/LICENSE",
    "usr/share/doc/wotm/AppImageKit-LICENSE.txt",
    "usr/share/doc/wotm/GCC-Runtime-copyright.txt",
}
LIBRARY_NAME = re.compile(
    r"(?:libavcodec\.so\.61|libavutil\.so\.59|libswscale\.so\.8)(?:\.[0-9]+)*"
)
REQUIRED_FFMPEG_SONAMES = {"libavcodec.so.61", "libavutil.so.59", "libswscale.so.8"}
MAX_GLIBC_VERSION = (2, 35)
GLIBC_VERSION = re.compile(r"\bGLIBC_(\d+)\.(\d+)(?:\.(\d+))?\b")


def _within(path, root):
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except (OSError, RuntimeError, ValueError):
        return False


def scan_payload(root):
    root = Path(root)
    errors = []
    if not root.is_dir():
        return [f"not a directory: {root}"]

    for path in root.rglob("*"):
        rel = path.relative_to(root).as_posix()
        name = path.name.lower()

        if name in BANNED_NAMES or name.startswith(("scus_", "bascus")):
            errors.append(f"prohibited game/user-data path: {rel}")
        if path.is_dir() and name in BANNED_DIRS:
            errors.append(f"prohibited game-data directory: {rel}")

        if path.is_symlink():
            if not _within(path, root):
                errors.append(f"symlink escapes payload: {rel} -> {path.readlink()}")
                continue
            if not path.is_file():
                errors.append(f"broken or non-file symlink: {rel}")
                continue
        elif path.is_dir():
            continue
        elif not path.is_file():
            errors.append(f"unexpected special file: {rel}")
            continue
        if path.suffix.lower() in BANNED_SUFFIXES:
            errors.append(f"prohibited file type: {rel}")

        with path.open("rb") as stream:
            head = stream.read(32776)
        if head[32768:32775] == b"\x01CD001\x01":
            errors.append(f"ISO9660 volume descriptor: {rel}")

        if head.startswith(b"\x7fELF"):
            if len(head) < 20 or head[4] != 2 or head[5] != 1:
                errors.append(f"non-x86-64 or malformed ELF: {rel}")
            elif struct.unpack_from("<H", head, 18)[0] != 62:
                errors.append(f"non-x86-64 ELF: {rel}")
            elif rel != "usr/bin/wotm" and not (
                path.parent == root / "usr/lib"
                and LIBRARY_NAME.fullmatch(path.name)
            ):
                errors.append(f"unexpected ELF payload: {rel}")
        elif rel not in ALLOWED_FILES:
            errors.append(f"unexpected non-ELF payload: {rel}")

    return errors


def parse_glibc_versions(output):
    return {
        tuple(int(part) for part in match.groups() if part is not None)
        for match in GLIBC_VERSION.finditer(output)
    }


def check_glibc_versions(output, label, maximum=MAX_GLIBC_VERSION):
    too_new = sorted(version for version in parse_glibc_versions(output) if version[:2] > maximum)
    return [
        f"{label} requires GLIBC_{'.'.join(map(str, version))}, above the GLIBC_{maximum[0]}.{maximum[1]} baseline"
        for version in too_new
    ]


def check_elf_abi(path, label=None):
    path = Path(path)
    label = label or path.name
    try:
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                return [f"not an ELF file: {label}"]
    except OSError as error:
        return [f"cannot read ELF file {label}: {error}"]
    result = subprocess.run(["readelf", "--version-info", str(path)], capture_output=True, text=True)
    if result.returncode:
        return [f"cannot inspect ELF symbol versions: {label}"]
    return check_glibc_versions(result.stdout, label)


def check_static_cpp_runtime(dynamic_output, version_output, dynsym_output, label):
    errors = []
    if re.search(r"\(NEEDED\).*\[lib(?:stdc\+\+|gcc_s)\.so(?:\.[^]]+)?\]", dynamic_output):
        errors.append(f"{label} dynamically depends on the GCC C++ runtime")

    in_needs = False
    for line in version_output.splitlines():
        if line.startswith("Version needs section"):
            in_needs = True
        elif line.startswith(("Version definition section", "Version symbols section")):
            in_needs = False
        elif in_needs and re.search(r"\bName:\s*(?:GLIBCXX|CXXABI)_", line):
            errors.append(f"{label} requires a dynamic GLIBCXX/CXXABI version")
            break

    runtime_symbol = re.compile(r"^(?:_Z|__cxa_|__gxx_|_Unwind_)")
    for line in dynsym_output.splitlines():
        fields = line.split()
        if len(fields) < 8 or fields[4] not in ("GLOBAL", "WEAK") or fields[5] not in ("DEFAULT", "PROTECTED") or fields[6] == "UND":
            continue
        name = fields[7].split("@", 1)[0]
        if runtime_symbol.match(name):
            errors.append(f"{label} exports GCC runtime symbol: {name}")
    return errors


def check_elf_paths(root):
    root = Path(root)
    errors = []
    for path in root.rglob("*"):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        label = path.relative_to(root).as_posix()
        errors.extend(check_elf_abi(path, label))
        result = subprocess.run(["readelf", "-d", str(path)], capture_output=True, text=True)
        if result.returncode:
            errors.append(f"cannot inspect ELF dynamic section: {label}")
            continue
        for line in result.stdout.splitlines():
            if "(RPATH)" not in line and "(RUNPATH)" not in line:
                continue
            value = line.rsplit("[", 1)[-1].split("]", 1)[0]
            for entry in value.split(":"):
                if not entry.startswith("$ORIGIN"):
                    errors.append(f"non-relative ELF search path in {label}: {entry}")
        if label == "usr/bin/wotm":
            versions = subprocess.run(["readelf", "--version-info", str(path)], capture_output=True, text=True)
            dynsym = subprocess.run(["readelf", "--dyn-syms", "--wide", str(path)], capture_output=True, text=True)
            if versions.returncode or dynsym.returncode:
                errors.append(f"cannot inspect static C++ runtime in {label}")
            else:
                errors.extend(check_static_cpp_runtime(result.stdout, versions.stdout, dynsym.stdout, label))
    return errors


def check_required(root):
    root = Path(root)
    errors = []
    required = (
        "AppRun", "usr/bin/wotm", "usr/bin/assets/Oswald-Medium.ttf",
        "usr/bin/assets/prompts.png", "usr/share/doc/wotm/GCC-Runtime-copyright.txt",
    )
    for rel in required:
        if not (root / rel).is_file():
            errors.append(f"missing required file: {rel}")
    apprun = root / "AppRun"
    if not apprun.is_file() or not (apprun.stat().st_mode & 0o111):
        errors.append("AppRun is not executable")

    sonames = {p.name for p in (root / "usr/lib").glob("*") if p.is_file()}
    for soname in sorted(REQUIRED_FFMPEG_SONAMES - sonames):
        errors.append(f"missing bundled FFmpeg soname: {soname}")
    return errors


def main(argv):
    if len(argv) == 3 and argv[1] == "--check-elf":
        path = Path(argv[2])
        errors = check_elf_abi(path)
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            return 1
        print(f"ELF ABI OK: {path}")
        return 0
    if len(argv) != 2:
        print(f"usage: {Path(argv[0]).name} <AppDir-or-extracted-AppImage> | --check-elf <file>", file=sys.stderr)
        return 2
    root = Path(argv[1])
    errors = scan_payload(root) + check_elf_paths(root) + check_required(root)
    if errors:
        print("AppImage payload check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1
    print(f"AppImage payload OK: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
