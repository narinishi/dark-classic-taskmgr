import struct, sys, os
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC = os.environ.get(
    'DARK_SRC', os.path.join(PROJECT_ROOT, 'taskmgr-dark-neo.exe'))
BLOB = os.environ.get('DARK_BLOB', os.path.join(HERE, 'bin', 'stub.bin'))
DST = os.environ.get('DARK_DST', os.path.join(HERE, 'uhooks5.exe'))
if len(sys.argv) > 1:
    BLOB = sys.argv[1]
if len(sys.argv) > 2:
    DST = sys.argv[2]

# Must match stub.ld
STUB_VA = int(os.environ.get('DARK_STUB_VA', '0x14004A000'), 0)
IAT_LoadLibraryW = 0x140024c80
IAT_GetProcAddress = 0x140024c38
IAT_GetModuleHandleW = 0x140024c58
IAT_GetCurrentProcessId = 0x140024d50
ORIG_CTOR = 0x140001984
ORIG_RET = 0x1400011c0
ELfanew = None


def align(v, a):
    return (v + a - 1) // a * a


def main():
    raw = bytearray(open(SRC, 'rb').read())
    pe = pefile.PE(data=bytes(raw))
    ib = pe.OPTIONAL_HEADER.ImageBase
    assert ib == 0x140000000, hex(ib)

    blob = open(BLOB, 'rb').read()
    print('stub blob', len(blob), 'bytes')

    fa = pe.OPTIONAL_HEADER.FileAlignment
    sa = pe.OPTIONAL_HEADER.SectionAlignment
    old_img = pe.OPTIONAL_HEADER.SizeOfImage
    new_rva = align(old_img, sa)
    new_va = ib + new_rva
    assert new_va == STUB_VA, 'section VA %#x != stub VA %#x' % (new_va, STUB_VA)
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
    sh[0:8] = b'.dark\0\0\0'
    struct.pack_into('<IIII', sh, 8, vsz, new_rva, rsz, new_raw_off)
    struct.pack_into('<IIHHI', sh, 24, 0, 0, 0, 0, 0xE0000020)  # code|execute|read|write
    body[hdr_end:hdr_end + 40] = sh

    fh = pe2.DOS_HEADER.e_lfanew + 4
    struct.pack_into('<H', body, fh + 2, num + 1)

    opt = fh + 20
    struct.pack_into('<I', body, opt + 56, new_rva + rsz)
    struct.pack_into('<I', body, opt + 8, pe2.OPTIONAL_HEADER.SizeOfInitializedData + rsz)

    # Clear ASLR / high-entropy / CFG so absolute addresses + callback stay valid.
    dllchar = pe2.OPTIONAL_HEADER.DllCharacteristics
    dllchar &= ~0x0040  # DYNAMIC_BASE
    dllchar &= ~0x0020  # HIGH_ENTROPY_VA
    dllchar &= ~0x4000  # GUARD_CF
    struct.pack_into('<H', body, opt + 70, dllchar)

    # Relocation directory no longer needed since ASLR is off; keep it (harmless).

    if cert:
        ddir = opt + (112 if pe2.OPTIONAL_HEADER.Magic == 0x20b else 96)
        struct.pack_into('<II', body, ddir + 4 * 8, new_cert_off, cert_size)

    # Trampoline at original entry point: jmp to stub, with displaced bytes intact.
    ep_rva = pe2.OPTIONAL_HEADER.AddressOfEntryPoint
    ep_off = pe2.get_offset_from_rva(ep_rva)
    ep_va = ib + ep_rva
    rel32 = STUB_VA - (ep_va + 5)
    if not (-0x80000000 <= rel32 < 0x80000000):
        raise SystemExit('entry relativjmp out of range')
    body[ep_off:ep_off + 5] = b'\xe9' + struct.pack('<i', rel32)
    print('entry trampoline at VA %#x -> %#x (rel %#x)' % (ep_va, STUB_VA, rel32 & 0xffffffff))

    struct.pack_into('<I', body, opt + 64, 0)
    chk = pefile.PE(data=bytes(body)).generate_checksum()
    struct.pack_into('<I', body, opt + 64, chk)

    open(DST, 'wb').write(bytes(body))
    print('wrote', DST, len(body), 'bytes; .dark VA', hex(new_va), 'RVA', hex(new_rva), 'raw', hex(new_raw_off), 'cert@', hex(new_cert_off), 'chk', hex(chk))
    print('DllCharacteristics ->', hex(dllchar))


if __name__ == '__main__':
    main()
