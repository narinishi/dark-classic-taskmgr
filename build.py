import hashlib, os, struct, subprocess, sys
import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SRC_DIR = os.path.join(HERE, 'src')
BIN_DIR = os.path.join(HERE, 'bin')
os.makedirs(SRC_DIR, exist_ok=True)
os.makedirs(BIN_DIR, exist_ok=True)
MH = os.path.join(HERE, 'minhook-master')
MH_INC = ['-I', os.path.join(MH, 'include'),
          '-I', os.path.join(MH, 'src'),
          '-I', os.path.join(MH, 'src', 'hde')]
TARGET = os.path.abspath(os.environ.get(
    'UH_TARGET', os.path.join(PROJECT_ROOT, 'taskmgr-dark-neo.exe')))
MINGW = os.environ.get(
    'UH_MINGW', os.path.join(os.path.expanduser('~'), 'Downloads', 'MinGW', 'bin'))
BASE_SHA256 = os.environ.get('UH_BASE_SHA256',
    'ac757163d73491ea8cf6b94088eb34beccad9ee9f0ff47b297897b162e153cab')

args = [a for a in sys.argv[1:] if not a.startswith('--')]
defines = [a for a in sys.argv[1:] if a.startswith('-D')]
name = args[0] if args else 'uhooks5.exe'
if os.path.isabs(name):
    OUTEXE = name
else:
    OUTEXE = os.path.join(HERE, name)
if os.path.abspath(OUTEXE) == os.path.abspath(TARGET):
    raise SystemExit('refusing to overwrite the clean taskmgr-dark-neo.exe during prototyping')

raw = open(TARGET, 'rb').read()
digest = hashlib.sha256(raw).hexdigest()
print('base sha256', digest)
if digest != BASE_SHA256:
    raise SystemExit('unexpected taskmgr-dark-neo.exe base hash')

pe = pefile.PE(data=raw)
ib = pe.OPTIONAL_HEADER.ImageBase
iat = {(f.name or b'').decode(): f.address for e in pe.DIRECTORY_ENTRY_IMPORT for f in e.imports}
need = [
    'LoadLibraryW', 'GetProcAddress', 'GetModuleHandleW', 'GetCurrentProcessId',
    'GetClassNameW', 'SetWindowLongPtrW', 'CallWindowProcW', 'InvalidateRect',
    'GetSysColor', 'GetSysColorBrush', 'DrawTextW',
]
missing = [n for n in need if n not in iat]
if missing:
    raise SystemExit('missing imports: %r' % missing)

ep = pe.OPTIONAL_HEADER.AddressOfEntryPoint
ep_off = pe.get_offset_from_rva(ep)
orig_ctor = ib + ep + 9 + struct.unpack_from('<i', raw, ep_off + 5)[0]
orig_ret = ib + ep + 18 + struct.unpack_from('<i', raw, ep_off + 14)[0]
sec_align = pe.OPTIONAL_HEADER.SectionAlignment
base = ib + (pe.OPTIONAL_HEADER.SizeOfImage + sec_align - 1) // sec_align * sec_align
print('base', hex(base), 'orig_ctor', hex(orig_ctor), 'orig_ret', hex(orig_ret))

syms = {'iat_' + n: iat[n] for n in need}
syms.update(orig_ctor=orig_ctor, orig_ret=orig_ret)
lines = ['SECTIONS', '{', '  . = %#x;' % base]
lines += ['  %s = %#x;' % (k, v) for k, v in syms.items()]
lines += ['  .all %#x : AT(0) { *(.text.entry) *(.text*) *(.rdata*) *(.data*) *(.bss*) *(COMMON) }' % base,
          '  /DISCARD/ : { *(.pdata) *(.xdata) *(.debug*) *(.comment) *(.eh_frame) *(.reloc*) }', '}']
ldpath = os.path.join(BIN_DIR, 'proto.ld')
open(ldpath, 'w', newline='\n').write('\n'.join(lines) + '\n')


def run(cmd, allow_stderr=False):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit('failed: %r' % cmd)
    if r.stderr and not allow_stderr:
        sys.stderr.write(r.stderr)


gcc = os.path.join(MINGW, 'gcc.exe')
gxx = os.path.join(MINGW, 'g++.exe')
ld = os.path.join(MINGW, 'ld.exe')
objcopy = os.path.join(MINGW, 'objcopy.exe')
tramp_o = os.path.join(BIN_DIR, 'tramp.o')
MODULES = ['stub', 'util', 'syscolor', 'theme', 'textsink', 'stock', 'swtheme', 'uahmenu', 'graphedge', 'titlebar', 'dui']
mod_objs = [os.path.join(BIN_DIR, m + '_c.o') for m in MODULES]
shim_o = os.path.join(BIN_DIR, 'shim.o')
mh_extra_o = os.path.join(BIN_DIR, 'mh_extra.o')
mh_imp_o = os.path.join(BIN_DIR, 'mh_imp.o')
MH_SRCS = [
    (os.path.join(MH, 'src', 'hook.c'),        os.path.join(BIN_DIR, 'mh_hook.o')),
    (os.path.join(MH, 'src', 'buffer.c'),      os.path.join(BIN_DIR, 'mh_buffer.o')),
    (os.path.join(MH, 'src', 'trampoline.c'),  os.path.join(BIN_DIR, 'mh_trampoline.o')),
    (os.path.join(MH, 'src', 'hde', 'hde64.c'),os.path.join(BIN_DIR, 'mh_hde64.o')),
]
elf = os.path.join(BIN_DIR, 'stub.elf')
binp = os.path.join(BIN_DIR, 'stub.bin')

CXXFLAGS = ['-Os', '-std=c++14', '-fno-exceptions', '-fno-rtti',
            '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
            '-fno-ident', '-mno-stack-arg-probe', '-fno-builtin',
            '-fno-zero-initialized-in-bss', '-fno-common', '-flto',
            '-w', '-Wno-unknown-pragmas']
# NOTE: -fno-zero-initialized-in-bss -fno-common are mandatory (same reason
# stub.c uses them): objcopy's raw .all dump does not reliably carry .bss,
# so hooking-library zero-init globals must
# land in .data or they sit past the mapped blob and AV at first access.
CFLAGS = ['-Os', '-ffreestanding', '-DPACHENKO', *MH_INC, *defines,
          '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
          '-fno-ident', '-mno-stack-arg-probe', '-fno-builtin',
          '-fno-zero-initialized-in-bss', '-fno-common', '-flto',
          '-Wall', '-Werror', '-Wno-unused-parameter']
MH_CFLAGS = ['-Os',
             '-fno-stack-protector', '-fno-asynchronous-unwind-tables',
             '-fno-ident', '-mno-stack-arg-probe', '-fno-builtin',
             '-fno-zero-initialized-in-bss', '-fno-common', '-flto',
             '-w', *MH_INC]

run([gcc, '-c', os.path.join(SRC_DIR, 'tramp.S'), '-o', tramp_o])
for m, mo in zip(MODULES, mod_objs):
    run([gcc, '-c', os.path.join(SRC_DIR, m + '.c'), '-o', mo, *CFLAGS])
run([gxx, '-c', os.path.join(SRC_DIR, 'shim.cpp'), '-o', shim_o, *CXXFLAGS], allow_stderr=True)
run([gxx, '-c', os.path.join(SRC_DIR, 'mh_extra.cpp'), '-o', mh_extra_o, *CXXFLAGS], allow_stderr=True)
run([gxx, '-c', os.path.join(SRC_DIR, 'mh_imp.cpp'), '-o', mh_imp_o, *CXXFLAGS], allow_stderr=True)
for src, obj in MH_SRCS:
    run([gcc, '-c', src, '-o', obj, *MH_CFLAGS])
run([gxx, '-nostdlib', '-flto', '-Wl,-T,' + ldpath, '-Wl,-e,entry_stub', '-o', elf, tramp_o, *mod_objs, shim_o, mh_extra_o, mh_imp_o, *[o for _, o in MH_SRCS]])
run([objcopy, '-O', 'binary', '-j', '.all', elf, binp])
print('uhooks5 stub size', os.path.getsize(binp), 'output', OUTEXE)

env = os.environ.copy()
env.update(DARK_SRC=TARGET, DARK_STUB_VA=hex(base))
subprocess.run([sys.executable, os.path.join(HERE, 'inject.py'), binp, OUTEXE], check=True, env=env)
print('done')

