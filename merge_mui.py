import argparse
import struct
import sys

import pefile

MERGE_TYPES = {4, 5, 6, 9, 11}


def align(v, a):
    return (v + a - 1) // a * a


def load_resources(path):
    raw = open(path, 'rb').read()
    pe = pefile.PE(data=raw)
    rdir = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_RESOURCE']]
    rva = rdir.VirtualAddress
    rsize = rdir.Size
    base = pe.get_offset_from_rva(rva)
    blob = raw[base:base + rsize]

    def entries(off):
        named = struct.unpack_from('<H', blob, off + 12)[0]
        ids = struct.unpack_from('<H', blob, off + 14)[0]
        out = []
        for i in range(named + ids):
            nv, tgt = struct.unpack_from('<II', blob, off + 16 + i * 8)
            if nv & 0x80000000:
                noff = nv & 0x7fffffff
                ln = struct.unpack_from('<H', blob, noff)[0]
                key = (0, blob[noff + 2:noff + 2 + ln * 2].decode('utf-16le'))
            else:
                key = (1, nv)
            out.append((key, tgt))
        return out

    tree = {}

    def walk(off, path):
        for key, tgt in entries(off):
            if tgt & 0x80000000:
                walk(tgt & 0x7fffffff, path + [key])
            else:
                drva, dsize, cp, _ = struct.unpack_from('<IIII', blob, tgt & 0x7fffffff)
                off2 = drva - rva
                tree[tuple(path + [key])] = (cp, blob[off2:off2 + dsize])

    walk(0, [])
    return tree


def keyorder(k):
    return (0 if k[0] == 0 else 1, k[1])


def build_resource_blob(items):
    root = {'children': {}, 'data': None}
    for path, (cp, content) in items:
        node = root
        for k in path:
            node = node['children'].setdefault(k, {'children': {}, 'data': None})
        node['data'] = (cp, content)

    bfs = [root]
    i = 0
    while i < len(bfs):
        node = bfs[i]
        i += 1
        for k in sorted(node['children'], key=keyorder):
            bfs.append(node['children'][k])

    dir_off = {}
    pos = 0
    for node in bfs:
        dir_off[id(node)] = pos
        pos += 16 + 8 * len(node['children'])

    data_off = {}
    for node in bfs:
        for k in sorted(node['children'], key=keyorder):
            ch = node['children'][k]
            if ch['data'] is not None:
                data_off[id(ch)] = pos
                pos += 16

    name_off = {}
    for node in bfs:
        for k in sorted(node['children'], key=keyorder):
            if k[0] == 0 and k[1] not in name_off:
                name_off[k[1]] = pos
                pos += 2 + len(k[1]) * 2
                if pos % 2:
                    pos += 1

    if pos % 4:
        pos += 4 - (pos % 4)

    payload_off = {}
    for node in bfs:
        if node['data'] is not None:
            payload_off[id(node)] = pos
            pos += len(node['data'][1])
            if pos % 4:
                pos += 4 - (pos % 4)

    out = bytearray(pos)

    for node in bfs:
        off = dir_off[id(node)]
        named = sum(1 for k in node['children'] if k[0] == 0)
        ids = sum(1 for k in node['children'] if k[0] == 1)
        struct.pack_into('<HH', out, off + 12, named, ids)
        eo = off + 16
        for k in sorted(node['children'], key=keyorder):
            ch = node['children'][k]
            nv = (0x80000000 | name_off[k[1]]) if k[0] == 0 else k[1]
            tgt = data_off[id(ch)] if ch['data'] is not None else (0x80000000 | dir_off[id(ch)])
            struct.pack_into('<II', out, eo, nv, tgt)
            eo += 8

    relocs = []
    for node in bfs:
        if node['data'] is not None:
            cp, content = node['data']
            de = data_off[id(node)]
            po = payload_off[id(node)]
            struct.pack_into('<IIII', out, de, 0, len(content), cp, 0)
            relocs.append((de, po, len(content), cp))

    for nm, off in name_off.items():
        enc = nm.encode('utf-16le')
        struct.pack_into('<H', out, off, len(nm))
        out[off + 2:off + 2 + len(enc)] = enc

    for node in bfs:
        if node['data'] is not None:
            off = payload_off[id(node)]
            content = node['data'][1]
            out[off:off + len(content)] = content

    return bytes(out), relocs


def main():
    ap = argparse.ArgumentParser(
        description='Merge MUI resources into a Task Manager EXE.')
    ap.add_argument('src', help='source EXE')
    ap.add_argument('mui', help='source MUI file')
    ap.add_argument('dst', help='output EXE')
    args = ap.parse_args()

    exe_tree = load_resources(args.src)
    mui_tree = load_resources(args.mui)

    merged = {}
    for path, val in exe_tree.items():
        if path[0] == (0, 'MUI'):
            continue
        merged[path] = val

    added = 0
    for path, val in mui_tree.items():
        if path[0] == (0, 'MUI'):
            continue
        t = path[0]
        if t[0] == 1 and t[1] in MERGE_TYPES:
            merged[path] = val
            added += 1

    blob, relocs = build_resource_blob(sorted(merged.items(), key=lambda kv: [keyorder(k) for k in kv[0]]))
    print('resources:', len(merged), 'merged from mui:', added, 'blob', hex(len(blob)))

    raw = bytearray(open(args.src, 'rb').read())
    pe = pefile.PE(data=bytes(raw))
    fa = pe.OPTIONAL_HEADER.FileAlignment
    sa = pe.OPTIONAL_HEADER.SectionAlignment
    old_img = pe.OPTIONAL_HEADER.SizeOfImage
    new_va = align(old_img, sa)
    vsz = len(blob)
    rsz = align(vsz, fa)

    sec_dir = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_SECURITY']]
    cert_off = sec_dir.VirtualAddress
    cert_size = sec_dir.Size
    last_end = max(s.PointerToRawData + s.SizeOfRawData for s in pe.sections)
    if cert_off and cert_off == last_end:
        cert = bytes(raw[cert_off:cert_off + cert_size])
        body = bytearray(raw[:cert_off])
    else:
        cert = b''
        body = bytearray(raw)

    new_raw_off = align(len(body), fa)
    if new_raw_off > len(body):
        body += b'\0' * (new_raw_off - len(body))
    body += blob
    while len(body) % fa:
        body.append(0)

    new_cert_off = len(body)
    if cert:
        body += cert
        while len(body) % 8:
            body.append(0)

    pe2 = pefile.PE(data=bytes(body))
    num = pe2.FILE_HEADER.NumberOfSections
    optsz = pe2.FILE_HEADER.SizeOfOptionalHeader
    hdr_end = pe2.DOS_HEADER.e_lfanew + 4 + 20 + optsz + 40 * num
    if hdr_end + 40 > pe2.OPTIONAL_HEADER.SizeOfHeaders:
        raise SystemExit('no room for section header')

    sh = bytearray(40)
    sh[0:8] = b'.rsrc2\0\0'
    struct.pack_into('<IIII', sh, 8, vsz, new_va, rsz, new_raw_off)
    struct.pack_into('<IIHHI', sh, 24, 0, 0, 0, 0, 0x40000040)
    body[hdr_end:hdr_end + 40] = sh

    fh = pe2.DOS_HEADER.e_lfanew + 4
    struct.pack_into('<H', body, fh + 2, num + 1)

    opt = fh + 20
    struct.pack_into('<I', body, opt + 56, new_va + rsz)
    struct.pack_into('<I', body, opt + 8, pe2.OPTIONAL_HEADER.SizeOfInitializedData + rsz)

    ddir = opt + (112 if pe2.OPTIONAL_HEADER.Magic == 0x20b else 96)
    struct.pack_into('<II', body, ddir + 2 * 8, new_va, vsz)
    if cert:
        struct.pack_into('<II', body, ddir + 4 * 8, new_cert_off, cert_size)

    for de, po, ln, cp in relocs:
        struct.pack_into('<IIII', body, new_raw_off + de, new_va + po, ln, cp, 0)

    struct.pack_into('<I', body, opt + 64, 0)
    chk = pefile.PE(data=bytes(body)).generate_checksum()
    struct.pack_into('<I', body, opt + 64, chk)

    open(args.dst, 'wb').write(bytes(body))
    print('wrote', args.dst, len(body), 'bytes; .rsrc2 VA', hex(new_va), 'raw', hex(new_raw_off), 'rsz', hex(rsz), 'cert@', hex(new_cert_off), 'chk', hex(chk))


if __name__ == '__main__':
    main()
