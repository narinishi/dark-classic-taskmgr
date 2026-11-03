#!/usr/bin/env python3
"""Apply the DPI-unaware compatibility patch to a Task Manager PE."""

import argparse
import os
import re
import struct

import pefile

RT_MANIFEST = 24
DPI_AWARE_RE = re.compile(
    rb"[ \t]*<dpiAware(?:\s[^>]*)?>\s*true\s*</dpiAware>[ \t]*(?:\r\n|\n|\r)?[ \t]*",
    re.IGNORECASE,
)


def resource_leaves(pe, raw):
    directory = pe.OPTIONAL_HEADER.DATA_DIRECTORY[
        pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]
    ]
    if not directory.VirtualAddress or not directory.Size:
        return []
    base = pe.get_offset_from_rva(directory.VirtualAddress)
    leaves = []

    def walk(relative, path):
        offset = base + relative
        named, ids = struct.unpack_from("<HH", raw, offset + 12)
        for index in range(named + ids):
            name_value, target = struct.unpack_from(
                "<II", raw, offset + 16 + index * 8
            )
            if name_value & 0x80000000:
                name_offset = name_value & 0x7FFFFFFF
                length = struct.unpack_from("<H", raw, base + name_offset)[0]
                key = (
                    0,
                    raw[
                        base + name_offset + 2 : base + name_offset + 2 + length * 2
                    ].decode("utf-16le"),
                )
            else:
                key = (1, name_value)
            child_path = path + [key]
            if target & 0x80000000:
                walk(target & 0x7FFFFFFF, child_path)
            else:
                entry_offset = base + (target & 0x7FFFFFFF)
                data_rva, size, codepage, _ = struct.unpack_from(
                    "<IIII", raw, entry_offset
                )
                leaves.append(
                    {
                        "path": child_path,
                        "entry_offset": entry_offset,
                        "data_offset": pe.get_offset_from_rva(data_rva),
                        "size": size,
                        "codepage": codepage,
                    }
                )

    walk(0, [])
    return leaves


def find_import_slot(pe, name):
    wanted = name.lower()
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imported in entry.imports:
            if imported.name and imported.name.decode("ascii", "ignore").lower() == wanted:
                return imported.address
    return None


def patch_code(raw, pe):
    slot = find_import_slot(pe, "SetProcessDPIAware")
    if not slot:
        raise SystemExit("import not found: SetProcessDPIAware")

    text = next(
        (section for section in pe.sections if section.Name.startswith(b".text")),
        None,
    )
    if text is None:
        raise SystemExit("no .text section")

    data = text.get_data()
    patched = []
    base_va = pe.OPTIONAL_HEADER.ImageBase + text.VirtualAddress
    for offset in range(len(data) - 5):
        if data[offset] != 0xFF or data[offset + 1] != 0x15:
            continue
        displacement = struct.unpack_from("<i", data, offset + 2)[0]
        call_va = base_va + offset
        target = call_va + 6 + displacement
        if target != slot:
            continue
        raw_offset = text.PointerToRawData + offset
        raw[raw_offset : raw_offset + 6] = b"\x90" * 6
        patched.append(call_va)
    return patched


def patch_manifest(raw, pe):
    patched = []
    for leaf in resource_leaves(pe, raw):
        if leaf["path"][0] != (1, RT_MANIFEST):
            continue
        start = leaf["data_offset"]
        size = leaf["size"]
        manifest = bytes(raw[start : start + size])
        updated = DPI_AWARE_RE.sub(b"", manifest)
        if updated == manifest:
            continue

        raw[start : start + len(updated)] = updated
        raw[start + len(updated) : start + size] = b"\0" * (size - len(updated))
        struct.pack_into("<I", raw, leaf["entry_offset"] + 4, len(updated))
        patched.append((leaf["path"], size, len(updated)))

    return patched


def update_checksum(raw, pe):
    offset = pe.OPTIONAL_HEADER.get_field_absolute_offset("CheckSum")
    struct.pack_into("<I", raw, offset, 0)
    checksum = pefile.PE(data=bytes(raw), fast_load=True).generate_checksum()
    struct.pack_into("<I", raw, offset, checksum)
    return checksum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="source Task Manager PE")
    parser.add_argument("-o", "--output", help="output PE (default: <name>-dpifix.exe)")
    args = parser.parse_args()

    source = os.path.abspath(args.input)
    if args.output:
        destination = os.path.abspath(args.output)
    else:
        stem, extension = os.path.splitext(source)
        destination = stem + "-dpifix" + extension

    raw = bytearray(open(source, "rb").read())
    pe = pefile.PE(data=bytes(raw))

    code_calls = patch_code(raw, pe)
    manifests = patch_manifest(raw, pe)
    checksum = update_checksum(raw, pe)

    open(destination, "wb").write(bytes(raw))
    print("source:", source)
    print(
        "code:",
        ("patched %d call(s)" % len(code_calls))
        if code_calls
        else "no SetProcessDPIAware call found",
    )
    for call_va in code_calls:
        print("  NOP at VA", hex(call_va))
    if manifests:
        for path, old_size, new_size in manifests:
            print(
                "manifest:",
                " / ".join(str(value) for _, value in path),
                "%d -> %d bytes" % (old_size, new_size),
            )
    else:
        print("manifest: no dpiAware=true element found")
    print("checksum:", hex(checksum))
    print("wrote:", destination)


if __name__ == "__main__":
    main()
