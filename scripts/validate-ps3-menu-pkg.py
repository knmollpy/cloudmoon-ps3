#!/usr/bin/env python3
"""Fail-closed validation for the PS3 menu-only (no-WebRTC) diagnostic PKG."""
from __future__ import annotations
import argparse
import struct
import sys
from pathlib import Path

def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)

def check_png(path: Path, expected: tuple[int, int]) -> None:
    if not path.is_file():
        fail(f"required XMB image is missing: {path.name}")
    data = path.read_bytes()
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        fail(f"{path.name} is not a valid PNG")
    width, height = struct.unpack(">II", data[16:24])
    if (width, height) != expected:
        fail(f"{path.name} must be {expected[0]}x{expected[1]}, got {width}x{height}")
    print(f"PASS: {path.name} {width}x{height}")

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pkg", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    args = parser.parse_args()

    if not args.pkg.is_file():
        fail("PKG missing")
    pkg = args.pkg.read_bytes()
    if len(pkg) < 0x80 or pkg[:4] != b"\x7fPKG":
        fail("invalid PS3 PKG magic/header")
    if int.from_bytes(pkg[4:8], "big") != 1:
        fail("unexpected PS3 PKG type")
    declared = int.from_bytes(pkg[0x18:0x20], "big")
    if declared != len(pkg):
        fail(f"PKG size mismatch: header={declared}, actual={len(pkg)}")
    content_id = pkg[0x30:0x30+48].split(b"\0", 1)[0].decode("ascii", "replace")
    if not content_id.startswith("UP0001-MNLT00001_"):
        fail(f"unexpected content ID: {content_id!r}")
    print(f"PASS: PS3 PKG header, {len(pkg)} bytes, {content_id}")

    elf = args.elf.read_bytes() if args.elf.is_file() else b""
    if len(elf) < 64 or elf[:4] != b"\x7fELF" or elf[4] != 2 or elf[5] != 2:
        fail("PPU ELF missing or not ELF64 big-endian")
    machine = int.from_bytes(elf[18:20], "big")
    entry = int.from_bytes(elf[24:32], "big")
    if machine != 21 or entry != 0x10000000:
        fail(f"unexpected PPU ELF machine/entry: machine={machine}, entry=0x{entry:x}")
    # The B diagnostic must carry the CloudMoon settings UI but must not contain
    # the WebRTC coordinator worker that was not proven to boot on real hardware.
    required_home_strings = (
        b"CloudMoon PS3",
        b"01  /  STREAM PROFILE",
        b"02  /  CLOUD SESSION",
        b"CONFIGURE STREAM",
        b"CONNECT TO CLOUDMOON",
        b"ACTIVE STREAM PROFILE",
    )
    missing_home_strings = [item.decode("ascii") for item in required_home_strings if item not in elf]
    if missing_home_strings:
        fail("PPU ELF is missing redesigned CloudMoon home-screen strings: " +
             ", ".join(missing_home_strings))
    if b"CloudMoon: connecting directly to coordinator" in elf:
        fail("menu-only diagnostic unexpectedly contains the WebRTC coordinator path")
    print(f"PASS: menu-only PPU ELF64 PowerPC64, entry 0x{entry:x}, {len(elf)} bytes")

    stage = args.stage
    sfo = stage / "PARAM.SFO"
    if not sfo.is_file() or not sfo.read_bytes().startswith(b"\0PSF"):
        fail("PARAM.SFO missing or invalid")
    if b"CloudMoon PS3" not in sfo.read_bytes():
        fail("PARAM.SFO does not identify CloudMoon PS3")
    print("PASS: PARAM.SFO identity")

    eboot = stage / "USRDIR" / "EBOOT.BIN"
    if not eboot.is_file() or eboot.stat().st_size < 512 * 1024:
        fail("signed EBOOT.BIN missing or implausibly small")
    if eboot.read_bytes()[:4] != b"SCE\0":
        fail("EBOOT.BIN does not have the expected SELF signature")
    print(f"PASS: signed EBOOT.BIN ({eboot.stat().st_size} bytes)")
    check_png(stage / "ICON0.PNG", (320, 176))
    check_png(stage / "PIC1.PNG", (1920, 1080))
    print("PASS: all menu-only PKG checks completed")

if __name__ == "__main__":
    main()
