# SPDX-License-Identifier: MIT
"""Combine project MCU firmware with FPGA image of FNIRSI's firmware file.

usage: python -B -I dla16_pack.py pack --mcu build/dla16_firmware.bin --vendor DLA32_V1.1_26052116.bin -o combined.bin
       python -B -I dla16_pack.py info FILE
       python -B -I dla16_pack.py unpack FILE [-d DIR]

pack     writes a firmware file in FNIRSI's container format: project MCU image, FPGA image from --vendor file.
         Everything else is copied from --vendor file (names, offsets, layout, padding); only size and
         last-byte fields of MCU part are recomputed. FPGA image must be a known one for --model (default 16).
info     prints parts of a container: name, offset, size, last byte, CRC-32.
unpack   writes parts of a container to files named as in container.

Standard library only, Python 3.10 or later.
"""
import argparse
import os
import struct
import sys
import zlib
from dataclasses import dataclass

HEADER_BASES = (0x00, 0x2C)
NAME_BYTES = 0x20
HEADER_END = 0x58
FIRST_OFFSET = 0x1000
MCU_OFFSET = 0x1000
FPGA_OFFSET = 0xD000
MCU_AREA = FPGA_OFFSET - MCU_OFFSET
MIN_MCU_BYTES = 1024
KNOWN_FPGA = {
    0x3A340F47: (16, "FPGA_DLA32_V2.0_260519.bin"),
    0x628C4E07: (32, "FPGA_DLA32_V1.0.4_260519.bin"),
}


class PackError(Exception):
    pass


@dataclass(frozen=True)
class Part:
    name: str
    offset: int
    size: int
    last: int
    data: bytes


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def model_of(fpga_image):
    known = KNOWN_FPGA.get(crc32(fpga_image))
    return known[0] if known else None


def parse_container(blob):
    if len(blob) < FIRST_OFFSET:
        raise PackError(f"{len(blob)} bytes is no firmware container: shorter than its header area")
    parts = []
    for index, base in enumerate(HEADER_BASES, start=1):
        raw = blob[base:base + NAME_BYTES]
        text = raw.split(b"\0")[0]
        try:
            name = text.decode("ascii")
        except UnicodeDecodeError:
            raise PackError(f"part {index}: name is not ASCII") from None
        if not name or not name.isprintable():
            raise PackError(f"part {index}: no valid name, file is no firmware container")
        offset, size, last = struct.unpack_from("<III", blob, base + NAME_BYTES)
        if size == 0 or last != offset + size - 1:
            raise PackError(f"part {index} ({name}): last byte {last:#x} is not offset {offset:#x} + size {size} - 1")
        if offset < FIRST_OFFSET:
            raise PackError(f"part {index} ({name}): offset {offset:#x} lies inside header area")
        if offset + size > len(blob):
            raise PackError(f"part {index} ({name}): ends beyond end of file ({offset + size} > {len(blob)})")
        parts.append(Part(name, offset, size, last, bytes(blob[offset:offset + size])))
    first, second = parts
    if second.offset < first.offset + first.size:
        raise PackError("parts are not in order or overlap: part 2 must follow part 1")
    return parts


def build_container(vendor_blob, mcu, allow_unknown_fpga=False, model=16):
    mcu_part, fpga_part = parse_container(vendor_blob)
    if (mcu_part.offset, fpga_part.offset) != (MCU_OFFSET, FPGA_OFFSET):
        raise PackError(
            f"vendor layout not supported: offsets {mcu_part.offset:#x} and {fpga_part.offset:#x}, "
            f"expected {MCU_OFFSET:#x} and {FPGA_OFFSET:#x}")
    found = model_of(fpga_part.data)
    if found is None and not allow_unknown_fpga:
        raise PackError(f"unknown FPGA image (CRC-32 {crc32(fpga_part.data):08x}); --allow-unknown-fpga overrides")
    if found is not None and found != model:
        raise PackError(f"FPGA image of vendor file is for {found}-channel model, not for {model}-channel model")
    if len(mcu) < MIN_MCU_BYTES:
        raise PackError(f"MCU image of {len(mcu)} bytes is no firmware (at least {MIN_MCU_BYTES} bytes)")
    if len(mcu) > MCU_AREA:
        raise PackError(f"MCU image of {len(mcu)} bytes is too large: container has room for {MCU_AREA} bytes")
    out = bytearray(vendor_blob)
    out[MCU_OFFSET:FPGA_OFFSET] = bytes(MCU_AREA)
    out[MCU_OFFSET:MCU_OFFSET + len(mcu)] = mcu
    struct.pack_into("<II", out, 0x24, len(mcu), MCU_OFFSET + len(mcu) - 1)
    return bytes(out)


def describe(parts, blob):
    lines = [f"file: {len(blob)} bytes"]
    for index, part in enumerate(parts, start=1):
        lines.append(
            f"part {index}: {part.name}  offset {part.offset:#x}  size {part.size}  "
            f"last {part.last:#x}  CRC-32 {crc32(part.data):08x}")
    fpga_model = model_of(parts[1].data)
    lines.append(f"FPGA image is for {fpga_model}-channel model" if fpga_model else "FPGA image is not a known one")
    return "\n".join(lines)


def read_file(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as error:
        raise PackError(f"cannot read {path}: {error.strerror}") from None


def command_pack(args):
    vendor = read_file(args.vendor)
    mcu = read_file(args.mcu)
    out = build_container(vendor, mcu, args.allow_unknown_fpga, args.model)
    try:
        with open(args.output, "wb") as handle:
            handle.write(out)
    except OSError as error:
        raise PackError(f"cannot write {args.output}: {error.strerror}") from None
    print(f"MCU image {args.mcu}: {len(mcu)} bytes, CRC-32 {crc32(mcu):08x}")
    print(f"FPGA image from {args.vendor}")
    print(f"written {args.output}")
    print(describe(parse_container(out), out))
    return 0


def command_info(args):
    blob = read_file(args.file)
    print(describe(parse_container(blob), blob))
    return 0


def command_unpack(args):
    blob = read_file(args.file)
    parts = parse_container(blob)
    try:
        os.makedirs(args.directory, exist_ok=True)
        for part in parts:
            name = os.path.basename(part.name)
            with open(os.path.join(args.directory, name), "wb") as handle:
                handle.write(part.data)
            print(f"written {os.path.join(args.directory, name)}: {part.size} bytes, CRC-32 {crc32(part.data):08x}")
    except OSError as error:
        raise PackError(f"cannot write parts: {error.strerror}") from None
    return 0


def make_parser():
    parser = argparse.ArgumentParser(prog="dla16_pack.py", description=__doc__.split("\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="combine project MCU image with FPGA image of vendor file")
    pack.add_argument("--mcu", required=True, help="project MCU image, for example build/dla16_firmware.bin")
    pack.add_argument("--vendor", required=True, help="vendor firmware file holding FPGA image")
    pack.add_argument("-o", "--output", required=True, help="combined file to write")
    pack.add_argument("--model", type=int, choices=(16, 32), default=16, help="channel count of analyzer")
    pack.add_argument("--allow-unknown-fpga", action="store_true", help="accept FPGA image that is not a known one")
    pack.set_defaults(run=command_pack)
    info = commands.add_parser("info", help="print parts of a container")
    info.add_argument("file")
    info.set_defaults(run=command_info)
    unpack = commands.add_parser("unpack", help="write parts of a container to files")
    unpack.add_argument("file")
    unpack.add_argument("-d", "--directory", default=".", help="folder for part files")
    unpack.set_defaults(run=command_unpack)
    return parser


def main(argv=None):
    args = make_parser().parse_args(argv)
    try:
        return args.run(args)
    except PackError as error:
        print(f"dla16_pack: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
