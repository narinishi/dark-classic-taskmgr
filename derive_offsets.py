#!/usr/bin/env python3
"""Derive inject.py constants from a Task Manager PE.

Usage:
    python derive_offsets.py [target.exe]
    python derive_offsets.py --json [target.exe]

Defaults to ../../taskmgr-dark-neo.exe relative to this script, or the
UH_TARGET / DARK_SRC environment variable.
"""

import argparse
import json
import os
import struct
import sys

import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
DEFAULT_TARGET = os.path.join(PROJECT_ROOT, "taskmgr-dark-neo.exe")

IAT_NAMES = [
    "LoadLibraryW",
    "GetProcAddress",
    "GetModuleHandleW",
    "GetCurrentProcessId",
]


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def derive(target):
    raw = open(target, "rb").read()
    pe = pefile.PE(data=raw)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    imports = {}
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            name = (imp.name or b"").decode()
            if name and name not in imports:
                imports[name] = imp.address

    missing = [name for name in IAT_NAMES if name not in imports]
    if missing:
        raise SystemExit("missing imports in %s: %s" % (target, ", ".join(missing)))

    entry_rva = pe.OPTIONAL_HEADER.AddressOfEntryPoint
    entry_off = pe.get_offset_from_rva(entry_rva)
    # The entry stub is expected to start with two relative jumps. These are
    # the displaced targets from build.py's trampoline contract.
    orig_ctor = image_base + entry_rva + 9 + struct.unpack_from(
        "<i", raw, entry_off + 5
    )[0]
    orig_ret = image_base + entry_rva + 18 + struct.unpack_from(
        "<i", raw, entry_off + 14
    )[0]

    section_alignment = pe.OPTIONAL_HEADER.SectionAlignment
    stub_va = image_base + align(
        pe.OPTIONAL_HEADER.SizeOfImage, section_alignment
    )

    return {
        "target": os.path.abspath(target),
        "image_base": image_base,
        "STUB_VA": stub_va,
        "IAT_LoadLibraryW": imports["LoadLibraryW"],
        "IAT_GetProcAddress": imports["GetProcAddress"],
        "IAT_GetModuleHandleW": imports["GetModuleHandleW"],
        "IAT_GetCurrentProcessId": imports["GetCurrentProcessId"],
        "ORIG_CTOR": orig_ctor,
        "ORIG_RET": orig_ret,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "target",
        nargs="?",
        default=os.environ.get("UH_TARGET") or os.environ.get("DARK_SRC") or DEFAULT_TARGET,
        help="clean Task Manager PE (default: UH_TARGET/DARK_SRC or ../../taskmgr-dark-neo.exe)",
    )
    ap.add_argument(
        "--json",
        action="store_true",
        help="print a JSON object instead of Python assignments",
    )
    args = ap.parse_args()

    values = derive(args.target)

    if args.json:
        print(json.dumps(values, indent=2))
        return

    print("# derived from %s" % values["target"])
    for name in (
        "STUB_VA",
        "IAT_LoadLibraryW",
        "IAT_GetProcAddress",
        "IAT_GetModuleHandleW",
        "IAT_GetCurrentProcessId",
        "ORIG_CTOR",
        "ORIG_RET",
    ):
        print("%s = %s" % (name, hex(values[name])))


if __name__ == "__main__":
    main()
