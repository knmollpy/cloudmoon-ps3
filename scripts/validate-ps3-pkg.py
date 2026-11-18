#!/usr/bin/env python3
"""Fail-closed validation for a CloudMoon PS3 retail PKG before upload."""
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
    if len(data) < 33 or data[:8] != b"\x89PNG\r\n\x1a\n":
        fail(f"{path.name} is not a valid PNG")
    if data[12:16] != b"IHDR":
        fail(f"{path.name} has no PNG IHDR header")
    width, height = struct.unpack(">II", data[16:24])
    if (width, height) != expected:
        fail(f"{path.name} must be {expected[0]}x{expected[1]}, got {width}x{height}")
    print(f"PASS: {path.name} PNG {width}x{height} ({len(data)} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pkg", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path,
                        help="PS3-Moonlight/build/pkg staging directory")
    parser.add_argument("--elf", required=True, type=Path,
                        help="unstripped PPU ELF from PS3-Moonlight")
    parser.add_argument("--mode", choices=("full", "safe"), default="full",
                        help="full static-WebRTC build or safe UI-only build")
    args = parser.parse_args()

    if not args.pkg.is_file():
        fail(f"PKG not found: {args.pkg}")
    pkg = args.pkg.read_bytes()
    if len(pkg) < 0x80 or pkg[:4] != b"\x7fPKG":
        fail("invalid PS3 PKG magic/header")
    if int.from_bytes(pkg[4:8], "big") != 1:
        fail("unexpected PS3 PKG type")
    declared_size = int.from_bytes(pkg[0x18:0x20], "big")
    if declared_size != len(pkg):
        fail(f"PKG size mismatch: header={declared_size}, actual={len(pkg)}")
    content_id = pkg[0x30:0x30 + 48].split(b"\0", 1)[0].decode("ascii", "replace")
    if not content_id.startswith("UP0001-MNLT00001_"):
        fail(f"unexpected content ID: {content_id!r}")
    print(f"PASS: PS3 PKG header, content ID {content_id}, {len(pkg)} bytes")

    elf = args.elf.read_bytes() if args.elf.is_file() else b""
    if len(elf) < 64 or elf[:4] != b"\x7fELF":
        fail("PPU ELF is missing or invalid")
    if elf[4] != 2 or elf[5] != 2:
        fail("PPU ELF must be ELF64 big-endian")
    machine = int.from_bytes(elf[18:20], "big")
    entry = int.from_bytes(elf[24:32], "big")
    if machine != 21 or entry != 0x10000000:
        fail(f"unexpected PPU ELF machine/entry: machine={machine}, entry=0x{entry:x}")
    # Validate the exact runtime mode that was requested for this artifact.
    if args.mode == "full":
        markers = (b"CloudMoon: network initialization result %d",
                   b"CloudMoon PS3 UI Initialized",
                   b"CloudMoon: Connect to CloudMoon selected",
                   b"CONNECTING TO CLOUDMOON",
                   b"CloudMoon: connecting directly to coordinator",
                   b"CloudMoon: phone pairing page ready at %s",
                   b"SCAN THIS QR CODE WITH YOUR PHONE",
                   b"CLOUDMOON SESSION LINKED",
                   b"USRDIR/cloudmoon.ini")
    else:
        markers = (b"1 main", b"2 ui_init complete",
                   b"3 ps3input_start complete",
                   b"CloudMoon QR: network initialization result %d",
                   b"CloudMoon QR: local phone link ready at %s",
                   b"SCAN THIS QR CODE WITH YOUR PHONE",
                   b"[ SIGN IN WITH PHONE QR ]",
                   b"cloudmoon-mobile-link.html",
                   b"USRDIR/cloudmoon_boot.log")
    for marker in markers:
        if marker not in elf:
            fail(f"PPU ELF is missing required {args.mode} build marker: {marker.decode()}")
    for forbidden in (b"Sunshine Pairing Required",
                      b"Open Sunshine Web UI (PIN Tab)",
                      b"PIN:  %s"):
        if forbidden in elf:
            fail(f"Legacy Sunshine PIN workflow remains in PPU ELF: {forbidden.decode()}")
    print(f"PASS: PPU ELF64 PowerPC64, entry 0x{entry:x}, {len(elf)} bytes")

    stage = args.stage
    sfo = stage / "PARAM.SFO"
    if not sfo.is_file() or not sfo.read_bytes().startswith(b"\0PSF"):
        fail("PARAM.SFO missing or invalid")
    if b"CloudMoon PS3" not in sfo.read_bytes():
        fail("PARAM.SFO does not identify the application as CloudMoon PS3")
    print("PASS: PARAM.SFO identity")

    eboot = stage / "USRDIR" / "EBOOT.BIN"
    if not eboot.is_file() or eboot.stat().st_size < 512 * 1024:
        fail("signed USRDIR/EBOOT.BIN missing or implausibly small")
    if eboot.read_bytes()[:4] != b"SCE\0":
        fail("USRDIR/EBOOT.BIN does not have the expected SELF signature")
    print(f"PASS: signed EBOOT.BIN ({eboot.stat().st_size} bytes)")

    helper = stage / "USRDIR" / "cloudmoon-mobile-link.html"
    if not helper.is_file() or helper.stat().st_size < 4_000:
        fail("USRDIR/cloudmoon-mobile-link.html missing or incomplete")
    helper_text = helper.read_text(encoding="utf-8")
    for marker in ('fetch("/submit?k="', "android_instance_id",
                   "https://web.cloudmoonapp.com/es/"):
        if marker not in helper_text:
            fail(f"mobile session helper is missing required marker: {marker}")
    if 'inputType="password"' in helper_text or 'type="password"' in helper_text:
        fail("mobile session helper must never collect a CloudMoon or Google password")
    print(f"PASS: local mobile session helper ({helper.stat().st_size} bytes)")

    ca = stage / "USRDIR" / "cacert.pem"
    if not ca.is_file() or ca.stat().st_size < 10_000:
        fail("USRDIR/cacert.pem missing or incomplete")
    print(f"PASS: CA bundle ({ca.stat().st_size} bytes)")

    check_png(stage / "ICON0.PNG", (320, 176))
    # The generated CloudMoon artwork is part of the requested XMB identity,
    # so a package without its full-size preview background is incomplete.
    check_png(stage / "PIC1.PNG", (1920, 1080))
    print("PASS: all pre-upload PKG checks completed")


if __name__ == "__main__":
    main()
