#!/usr/bin/env python3
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_appimage import check_glibc_versions, check_required, check_static_cpp_runtime, parse_glibc_versions, scan_payload


def elf_header(machine=62):
    data = bytearray(64)
    data[:4] = b"\x7fELF"
    data[4:6] = b"\x02\x01"
    struct.pack_into("<H", data, 18, machine)
    return data


class AppImagePayloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "usr/bin").mkdir(parents=True)

    def tearDown(self):
        self.temp.cleanup()

    def test_accepts_x86_64_runtime(self):
        (self.root / "usr/bin/wotm").write_bytes(elf_header())
        self.assertEqual(scan_payload(self.root), [])

    def test_rejects_mips_elf(self):
        lib = self.root / "usr/lib/libavcodec.so.61"
        lib.parent.mkdir(parents=True)
        lib.write_bytes(elf_header(machine=8))
        self.assertTrue(any("non-x86-64 ELF" in error for error in scan_payload(self.root)))

    def test_rejects_renamed_iso(self):
        image = self.root / "usr/bin/assets/data.bin"
        image.parent.mkdir(parents=True)
        data = bytearray(32776)
        data[32768:32775] = b"\x01CD001\x01"
        image.write_bytes(data)
        self.assertTrue(any("ISO9660" in error for error in scan_payload(self.root)))

    def test_rejects_unknown_art(self):
        image = self.root / "usr/bin/assets/texture.png"
        image.parent.mkdir(parents=True)
        image.write_bytes(b"not an allowed asset")
        self.assertTrue(any("unexpected non-ELF" in error for error in scan_payload(self.root)))

    def test_rejects_symlink_outside_payload(self):
        link = self.root / "usr/bin/escape"
        link.symlink_to(self.root.parent / "outside")
        self.assertTrue(any("symlink escapes" in error for error in scan_payload(self.root)))

    def test_rejects_unlisted_internal_symlink(self):
        (self.root / "usr/bin/wotm").write_bytes(elf_header())
        (self.root / "usr/bin/extra").symlink_to("wotm")
        self.assertTrue(any("unexpected ELF" in e for e in scan_payload(self.root)))

    def test_accepts_desktop_and_icon_symlinks(self):
        (self.root / "wotm.svg").write_text("svg")
        (self.root / ".DirIcon").symlink_to("wotm.svg")
        self.assertEqual(scan_payload(self.root), [])

    def test_rejects_library_prefix_lookalike(self):
        lib = self.root / "usr/lib/libavcodec.so.61-extra"
        lib.parent.mkdir()
        lib.write_bytes(elf_header())
        self.assertTrue(any("unexpected ELF" in e for e in scan_payload(self.root)))

    def test_rejects_bundled_cpp_runtime(self):
        libdir = self.root / "usr/lib"
        libdir.mkdir()
        for name in ("libstdc++.so.6", "libgcc_s.so.1"):
            (libdir / name).write_bytes(elf_header())
        errors = scan_payload(self.root)
        self.assertEqual(sum("unexpected ELF payload" in error for error in errors), 2)

    def test_rejects_broken_soname_and_directory(self):
        lib = self.root / "usr/lib"
        lib.mkdir()
        (lib / "libavcodec.so.61").symlink_to("missing")
        (lib / "libavutil.so.59").mkdir()
        errors = check_required(self.root)
        self.assertTrue(any("missing bundled FFmpeg soname: libavcodec.so.61" in e for e in errors))
        self.assertTrue(any("missing bundled FFmpeg soname: libavutil.so.59" in e for e in errors))
        self.assertTrue(any("broken or non-file symlink" in e for e in scan_payload(self.root)))

    def test_rejects_fifo(self):
        os.mkfifo(self.root / "AppRun")
        self.assertTrue(any("unexpected special file" in e for e in scan_payload(self.root)))

    def test_rejects_dynamic_cpp_runtime_requirements_and_exports(self):
        versions = "Version needs section '.gnu.version_r' contains 1 entry:\n  Name: GLIBCXX_3.4.30"
        dynsym = "  1: 00000000 100 FUNC GLOBAL DEFAULT 12 __cxa_throw"
        for soname in ("libstdc++.so.6", "libgcc_s.so.1"):
            dynamic = f"(NEEDED) Shared library: [{soname}]"
            errors = check_static_cpp_runtime(dynamic, versions, dynsym, "wotm")
            self.assertTrue(any("dynamically depends" in error for error in errors))
            self.assertTrue(any("GLIBCXX/CXXABI" in error for error in errors))
            self.assertTrue(any("exports GCC runtime symbol" in error for error in errors))

    def test_rejects_protected_cpp_runtime_exports(self):
        dynsym = "  1: 00000000 100 FUNC GLOBAL PROTECTED 12 __cxa_throw"
        errors = check_static_cpp_runtime("", "", dynsym, "wotm")
        self.assertTrue(any("exports GCC runtime symbol" in error for error in errors))

    def test_accepts_static_cpp_runtime(self):
        dynamic = "(NEEDED) Shared library: [libc.so.6]"
        versions = "Version needs section '.gnu.version_r' contains 1 entry:\n  Name: GLIBC_2.35"
        dynsym = "  1: 00000000 10 FUNC GLOBAL DEFAULT UND __cxa_finalize@GLIBC_2.2.5"
        self.assertEqual(check_static_cpp_runtime(dynamic, versions, dynsym, "wotm"), [])

    def test_accepts_jammy_glibc_floor(self):
        output = "Name: GLIBC_2.2.5 Name: GLIBC_2.35 Name: GLIBCXX_3.4.29 Name: GLIBC_PRIVATE"
        self.assertEqual(parse_glibc_versions(output), {(2, 2, 5), (2, 35)})
        self.assertEqual(check_glibc_versions(output, "wotm"), [])

    def test_rejects_newer_glibc_requirement(self):
        errors = check_glibc_versions("Name: GLIBC_2.36", "libexample.so")
        self.assertEqual(len(errors), 1)
        self.assertIn("requires GLIBC_2.36", errors[0])


if __name__ == "__main__":
    unittest.main()
