#!/usr/bin/env python3
"""Extract taskmgr.exe and its MUI from WinRE.wim through 7z.dll."""

import argparse
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import os
import stat
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TARGET = os.path.join(HERE, "target")

RECOVERY_GPT = "{de94bba4-06d1-4d40-a16a-bfd50179d6ac}"
WIM_PATHS = (
    r"Recovery\WindowsRE\WinRE.wim",
    r"WindowsRE\WinRE.wim",
)
WIM_MEMBERS = {
    r"windows\system32\taskmgr.exe": "taskmgr.exe",
    r"windows\system32\en-us\taskmgr.exe.mui": "taskmgr.exe.mui",
}

S_OK = 0
S_FALSE = 1
E_NOINTERFACE = -2147467262
E_FAIL = -2147467259
E_INVALIDARG = -2147024809

VT_EMPTY = 0
VT_BSTR = 8
kpidPath = 3
STREAM_SEEK_SET = 0
STREAM_SEEK_CUR = 1
STREAM_SEEK_END = 2
kExtract = 0

PARTITION_SCRIPT = r"""
$ErrorActionPreference = 'Stop'
$items = Get-Partition | Where-Object {
    $_.Type -eq 'Recovery' -or $_.GptType -eq '{de94bba4-06d1-4d40-a16a-bfd50179d6ac}'
} | ForEach-Object {
    [pscustomobject]@{
        DiskNumber = $_.DiskNumber
        PartitionNumber = $_.PartitionNumber
        Type = $_.Type
        GptType = $_.GptType
        AccessPaths = @($_.AccessPaths)
    }
}
@($items) | ConvertTo-Json -Depth 4 -Compress
"""


class GUID(ctypes.Structure):
    _fields_ = [
        ("Data1", ctypes.c_ulong),
        ("Data2", ctypes.c_ushort),
        ("Data3", ctypes.c_ushort),
        ("Data4", ctypes.c_ubyte * 8),
    ]

    @classmethod
    def make(cls, data1, data2, data3, data4):
        return cls(data1, data2, data3, (ctypes.c_ubyte * 8)(*data4))


class PROPVARIANT(ctypes.Structure):
    _fields_ = [
        ("vt", ctypes.c_ushort),
        ("wReserved1", ctypes.c_ushort),
        ("wReserved2", ctypes.c_ushort),
        ("wReserved3", ctypes.c_ushort),
        ("value", ctypes.c_byte * 16),
    ]


class ComObject(ctypes.Structure):
    _fields_ = [
        ("lpVtbl", ctypes.c_void_p),
        ("refs", ctypes.c_ulong),
    ]


HRESULT = ctypes.c_int32
ULONG = ctypes.c_ulong
UInt32 = ctypes.c_uint32
UInt64 = ctypes.c_uint64
Int32 = ctypes.c_int32
Int64 = ctypes.c_int64

QI_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(GUID), ctypes.POINTER(ctypes.c_void_p))
REF_FUNC = ctypes.WINFUNCTYPE(ULONG, ctypes.c_void_p)
READ_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.c_void_p, UInt32, ctypes.POINTER(UInt32))
SEEK_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, Int64, UInt32, ctypes.POINTER(UInt64))
WRITE_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.c_void_p, UInt32, ctypes.POINTER(UInt32))
SET_TOTAL_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt64)
SET_COMPLETED_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(UInt64))
GET_STREAM_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt32, ctypes.POINTER(ctypes.c_void_p), Int32)
PREPARE_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, Int32)
RESULT_FUNC = ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, Int32)


class InStreamVtbl(ctypes.Structure):
    _fields_ = [
        ("QueryInterface", QI_FUNC),
        ("AddRef", REF_FUNC),
        ("Release", REF_FUNC),
        ("Read", READ_FUNC),
        ("Seek", SEEK_FUNC),
    ]


class OutStreamVtbl(ctypes.Structure):
    _fields_ = [
        ("QueryInterface", QI_FUNC),
        ("AddRef", REF_FUNC),
        ("Release", REF_FUNC),
        ("Write", WRITE_FUNC),
    ]


class ExtractCallbackVtbl(ctypes.Structure):
    _fields_ = [
        ("QueryInterface", QI_FUNC),
        ("AddRef", REF_FUNC),
        ("Release", REF_FUNC),
        ("SetTotal", SET_TOTAL_FUNC),
        ("SetCompleted", SET_COMPLETED_FUNC),
        ("GetStream", GET_STREAM_FUNC),
        ("PrepareOperation", PREPARE_FUNC),
        ("SetOperationResult", RESULT_FUNC),
    ]


class InArchiveVtbl(ctypes.Structure):
    _fields_ = [
        ("QueryInterface", QI_FUNC),
        ("AddRef", REF_FUNC),
        ("Release", REF_FUNC),
        ("Open", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(UInt64), ctypes.c_void_p)),
        ("Close", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p)),
        ("GetNumberOfItems", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(UInt32))),
        ("GetProperty", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt32, UInt32, ctypes.POINTER(PROPVARIANT))),
        ("Extract", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(UInt32), UInt32, Int32, ctypes.c_void_p)),
        ("GetArchiveProperty", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt32, ctypes.POINTER(PROPVARIANT))),
        ("GetNumberOfProperties", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(UInt32))),
        ("GetPropertyInfo", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt32, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(UInt32), ctypes.POINTER(ctypes.c_ushort))),
        ("GetNumberOfArchiveProperties", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, ctypes.POINTER(UInt32))),
        ("GetArchivePropertyInfo", ctypes.WINFUNCTYPE(HRESULT, ctypes.c_void_p, UInt32, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(UInt32), ctypes.POINTER(ctypes.c_ushort))),
    ]


def guid_bytes(value):
    return ctypes.string_at(ctypes.byref(value), 16)


IID_IUNKNOWN = GUID.make(0x00000000, 0x0000, 0x0000, (0xC0, 0, 0, 0, 0, 0, 0, 0x46))
IID_IIN_STREAM = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 3, 0, 3, 0, 0))
IID_ISEQUENTIAL_IN_STREAM = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 3, 0, 1, 0, 0))
IID_ISEQUENTIAL_OUT_STREAM = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 3, 0, 2, 0, 0))
IID_IPROGRESS = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 0, 0, 5, 0, 0))
IID_IARCHIVE_EXTRACT_CALLBACK = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 6, 0, 0x20, 0, 0))
IID_IIN_ARCHIVE = GUID.make(0x23170F69, 0x40C1, 0x278A, (0, 0, 0, 6, 0, 0x60, 0, 0))
CLSID_WIM = GUID.make(0x23170F69, 0x40C1, 0x278A, (0x10, 0, 0, 0x01, 0x10, 0xE6, 0, 0))

IID_IUNKNOWN_BYTES = guid_bytes(IID_IUNKNOWN)
IID_IIN_STREAM_BYTES = guid_bytes(IID_IIN_STREAM)
IID_ISEQUENTIAL_IN_STREAM_BYTES = guid_bytes(IID_ISEQUENTIAL_IN_STREAM)
IID_ISEQUENTIAL_OUT_STREAM_BYTES = guid_bytes(IID_ISEQUENTIAL_OUT_STREAM)
IID_IPROGRESS_BYTES = guid_bytes(IID_IPROGRESS)
IID_IARCHIVE_EXTRACT_CALLBACK_BYTES = guid_bytes(IID_IARCHIVE_EXTRACT_CALLBACK)

STREAM_STATE = {}
OUT_STATE = {}
CALLBACK_STATE = {}
OLEAUT32 = ctypes.WinDLL("oleaut32")
OLEAUT32.SysFreeString.argtypes = [ctypes.c_void_p]


def run_powershell(script):
    completed = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-NonInteractive",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            script,
        ],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        detail = completed.stderr.strip() or completed.stdout.strip()
        raise SystemExit("Get-Partition failed: %s" % detail)
    return completed.stdout.strip()


def recovery_partitions():
    output = run_powershell(PARTITION_SCRIPT)
    if not output:
        return []
    data = json.loads(output)
    if isinstance(data, dict):
        data = [data]

    partitions = []
    for item in data:
        paths = item.get("AccessPaths") or []
        if isinstance(paths, str):
            paths = [paths]
        roots = [
            path
            for path in paths
            if path.startswith("\\\\?\\Volume{") and path.endswith("\\")
        ]
        partitions.append(
            {
                "disk": item.get("DiskNumber"),
                "partition": item.get("PartitionNumber"),
                "type": item.get("Type"),
                "roots": roots,
            }
        )
    return partitions


def partition_name(part):
    return "disk %s partition %s (%s)" % (
        part["disk"],
        part["partition"],
        part["type"],
    )


def find_recovery_wim():
    partitions = recovery_partitions()
    if not partitions:
        raise SystemExit("no Windows recovery partition found")

    denied = []
    missing_roots = []
    for part in partitions:
        if not part["roots"]:
            missing_roots.append(partition_name(part))
            continue
        for root in part["roots"]:
            for relative in WIM_PATHS:
                candidate = root + relative
                try:
                    info = os.stat(candidate)
                except FileNotFoundError:
                    continue
                except PermissionError:
                    denied.append(candidate)
                    continue
                except OSError as exc:
                    denied.append("%s (%s)" % (candidate, exc))
                    continue
                if stat.S_ISREG(info.st_mode):
                    return part, candidate

    if denied:
        raise SystemExit(
            "could not read WinRE.wim from the recovery partition:\n  "
            + "\n  ".join(denied)
        )
    if missing_roots:
        raise SystemExit(
            "recovery partition has no volume access path: "
            + ", ".join(missing_roots)
        )
    raise SystemExit(
        "recovery partition found but no WinRE.wim was found in Recovery\\WindowsRE"
    )


def find_7zip_dll(override=None):
    candidates = []
    if override:
        candidates.append(override)
    for name in ("SEVENZIP_DLL", "7Z_DLL"):
        value = os.environ.get(name)
        if value:
            candidates.append(value)
    for base in (
        os.environ.get("ProgramW6432"),
        os.environ.get("ProgramFiles"),
        os.environ.get("ProgramFiles(x86)"),
    ):
        if base:
            candidates.append(os.path.join(base, "7-Zip", "7z.dll"))
    for candidate in candidates:
        if os.path.isfile(candidate):
            return os.path.abspath(candidate)
    raise SystemExit("7z.dll not found; pass --7z-dll or set SEVENZIP_DLL")


def stream_query_interface(this, riid, out):
    requested = ctypes.string_at(riid, 16)
    if requested in (
        IID_IUNKNOWN_BYTES,
        IID_IIN_STREAM_BYTES,
        IID_ISEQUENTIAL_IN_STREAM_BYTES,
    ):
        out[0] = this
        state = STREAM_STATE.get(this)
        if state is not None:
            state["refs"] += 1
        return S_OK
    out[0] = None
    return E_NOINTERFACE


def stream_add_ref(this):
    state = STREAM_STATE.get(this)
    if state is not None:
        state["refs"] += 1
        return state["refs"]
    return 1


def stream_release(this):
    state = STREAM_STATE.get(this)
    if state is not None:
        state["refs"] -= 1
        return state["refs"]
    return 0


def stream_read(this, data, size, processed):
    state = STREAM_STATE[this]
    try:
        chunk = state["file"].read(size)
        if chunk:
            ctypes.memmove(data, chunk, len(chunk))
        if processed:
            processed[0] = len(chunk)
        return S_OK
    except Exception:
        if processed:
            processed[0] = 0
        return E_FAIL


def stream_seek(this, offset, origin, new_position):
    state = STREAM_STATE[this]
    try:
        position = state["file"].seek(offset, origin)
        if new_position:
            new_position[0] = position
        return S_OK
    except Exception:
        return E_INVALIDARG


IN_STREAM_VTBL = InStreamVtbl(
    QI_FUNC(stream_query_interface),
    REF_FUNC(stream_add_ref),
    REF_FUNC(stream_release),
    READ_FUNC(stream_read),
    SEEK_FUNC(stream_seek),
)


def out_query_interface(this, riid, out):
    requested = ctypes.string_at(riid, 16)
    if requested in (IID_IUNKNOWN_BYTES, IID_ISEQUENTIAL_OUT_STREAM_BYTES):
        out[0] = this
        state = OUT_STATE.get(this)
        if state is not None:
            state["refs"] += 1
        return S_OK
    out[0] = None
    return E_NOINTERFACE


def out_add_ref(this):
    state = OUT_STATE.get(this)
    if state is not None:
        state["refs"] += 1
        return state["refs"]
    return 1


def out_release(this):
    state = OUT_STATE.get(this)
    if state is not None:
        state["refs"] -= 1
        return state["refs"]
    return 0


def out_write(this, data, size, processed):
    state = OUT_STATE[this]
    try:
        if size:
            state["file"].write(ctypes.string_at(data, size))
        if processed:
            processed[0] = size
        return S_OK
    except Exception:
        if processed:
            processed[0] = 0
        return E_FAIL


OUT_STREAM_VTBL = OutStreamVtbl(
    QI_FUNC(out_query_interface),
    REF_FUNC(out_add_ref),
    REF_FUNC(out_release),
    WRITE_FUNC(out_write),
)


def callback_query_interface(this, riid, out):
    requested = ctypes.string_at(riid, 16)
    if requested in (
        IID_IUNKNOWN_BYTES,
        IID_IPROGRESS_BYTES,
        IID_IARCHIVE_EXTRACT_CALLBACK_BYTES,
    ):
        out[0] = this
        state = CALLBACK_STATE.get(this)
        if state is not None:
            state["refs"] += 1
        return S_OK
    out[0] = None
    return E_NOINTERFACE


def callback_add_ref(this):
    state = CALLBACK_STATE.get(this)
    if state is not None:
        state["refs"] += 1
        return state["refs"]
    return 1


def callback_release(this):
    state = CALLBACK_STATE.get(this)
    if state is not None:
        state["refs"] -= 1
        return state["refs"]
    return 0


def callback_set_total(this, total):
    return S_OK


def callback_set_completed(this, complete):
    return S_OK


def callback_get_stream(this, index, out_stream, ask_mode):
    state = CALLBACK_STATE[this]
    state["current_index"] = index
    destination = state["destinations"].get(index)
    if ask_mode != kExtract or destination is None:
        out_stream[0] = None
        return S_OK
    stream = ComObject()
    stream.refs = 1
    stream.lpVtbl = ctypes.cast(ctypes.pointer(OUT_STREAM_VTBL), ctypes.c_void_p)
    address = ctypes.addressof(stream)
    file = open(destination, "wb")
    state["streams"].append((stream, file))
    OUT_STATE[address] = {"file": file, "refs": 1}
    out_stream[0] = address
    return S_OK


def callback_prepare(this, ask_mode):
    return S_OK


def callback_result(this, op_res):
    state = CALLBACK_STATE[this]
    if op_res != 0:
        state["errors"].append(op_res)
    return S_OK


EXTRACT_CALLBACK_VTBL = ExtractCallbackVtbl(
    QI_FUNC(callback_query_interface),
    REF_FUNC(callback_add_ref),
    REF_FUNC(callback_release),
    SET_TOTAL_FUNC(callback_set_total),
    SET_COMPLETED_FUNC(callback_set_completed),
    GET_STREAM_FUNC(callback_get_stream),
    PREPARE_FUNC(callback_prepare),
    RESULT_FUNC(callback_result),
)


def get_property_path(archive, vtable, index):
    prop = PROPVARIANT()
    hr = vtable.GetProperty(archive, index, kpidPath, ctypes.byref(prop))
    if hr != S_OK or prop.vt != VT_BSTR:
        return None
    pointer = ctypes.c_void_p.from_address(ctypes.addressof(prop) + 8).value
    if not pointer:
        return None
    value = ctypes.wstring_at(pointer)
    OLEAUT32.SysFreeString(pointer)
    return value


def normalize_path(value):
    return value.replace("/", "\\").lstrip("\\").lower()


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def check_pe(path):
    with open(path, "rb") as stream:
        if stream.read(2) != b"MZ":
            raise SystemExit("extracted file is not a PE image: %s" % path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--wim", help="use this WinRE.wim instead of discovering one")
    parser.add_argument("--7z-dll", dest="sevenzip_dll")
    args = parser.parse_args()

    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise SystemExit("64-bit Python is required")

    if args.wim:
        wim = os.path.abspath(args.wim)
        info = os.stat(wim)
        if not stat.S_ISREG(info.st_mode):
            raise SystemExit("WIM is not a file: %s" % wim)
        part = None
    else:
        part, wim = find_recovery_wim()

    sevenzip_dll = find_7zip_dll(args.sevenzip_dll)
    target = os.path.abspath(args.target)
    os.makedirs(target, exist_ok=True)

    if part:
        print("recovery partition:", partition_name(part))
    print("WinRE WIM:", wim)
    print("7-Zip DLL:", sevenzip_dll)
    print("target:", target)

    module = ctypes.WinDLL(sevenzip_dll)
    module.CreateObject.argtypes = [
        ctypes.POINTER(GUID),
        ctypes.POINTER(GUID),
        ctypes.POINTER(ctypes.c_void_p),
    ]
    module.CreateObject.restype = HRESULT

    archive = ctypes.c_void_p()
    hr = module.CreateObject(
        ctypes.byref(CLSID_WIM),
        ctypes.byref(IID_IIN_ARCHIVE),
        ctypes.byref(archive),
    )
    if hr != S_OK or not archive:
        raise SystemExit("CreateObject(WIM) failed: 0x%08x" % (hr & 0xFFFFFFFF))

    stream = ComObject()
    stream.refs = 1
    stream.lpVtbl = ctypes.cast(ctypes.pointer(IN_STREAM_VTBL), ctypes.c_void_p)
    stream_address = ctypes.addressof(stream)
    wim_file = open(wim, "rb")
    STREAM_STATE[stream_address] = {"file": wim_file, "refs": 1}

    archive_value = archive.value
    vtable_value = ctypes.cast(archive_value, ctypes.POINTER(ctypes.c_void_p))[0]
    archive_vtable = ctypes.cast(vtable_value, ctypes.POINTER(InArchiveVtbl)).contents
    hr = archive_vtable.Open(archive_value, stream_address, None, None)
    if hr != S_OK:
        raise SystemExit("IInArchive::Open failed: 0x%08x" % (hr & 0xFFFFFFFF))

    count = UInt32()
    hr = archive_vtable.GetNumberOfItems(archive_value, ctypes.byref(count))
    if hr != S_OK:
        raise SystemExit("GetNumberOfItems failed: 0x%08x" % (hr & 0xFFFFFFFF))

    indices = {}
    for index in range(count.value):
        path = get_property_path(archive_value, archive_vtable, index)
        if path is None:
            continue
        normalized = normalize_path(path)
        if normalized in WIM_MEMBERS and normalized not in indices:
            indices[normalized] = index

    missing = [name for name in WIM_MEMBERS if name not in indices]
    if missing:
        raise SystemExit("WIM members not found: " + ", ".join(missing))

    selected = []
    destinations = {}
    for name, output_name in WIM_MEMBERS.items():
        index = indices[name]
        selected.append(index)
        destinations[index] = os.path.join(target, output_name)

    callback = ComObject()
    callback.refs = 1
    callback.lpVtbl = ctypes.cast(ctypes.pointer(EXTRACT_CALLBACK_VTBL), ctypes.c_void_p)
    callback_address = ctypes.addressof(callback)
    callback_state = {
        "refs": 1,
        "destinations": destinations,
        "streams": [],
        "errors": [],
        "current_index": None,
    }
    CALLBACK_STATE[callback_address] = callback_state

    index_array = (UInt32 * len(selected))(*selected)
    print("extracting taskmgr.exe and taskmgr.exe.mui...")
    hr = archive_vtable.Extract(archive_value, index_array, len(selected), 0, callback_address)

    for _, file in callback_state["streams"]:
        file.close()

    archive_vtable.Close(archive_value)
    archive_vtable.Release(archive_value)
    wim_file.close()

    if hr != S_OK:
        raise SystemExit("Extract failed: 0x%08x" % (hr & 0xFFFFFFFF))
    if callback_state["errors"]:
        raise SystemExit("Extract operation errors: %s" % callback_state["errors"])

    paths = [destinations[indices[name]] for name in WIM_MEMBERS]
    for path in paths:
        check_pe(path)
        print(
            "wrote %s (%d bytes, sha256 %s)"
            % (path, os.path.getsize(path), sha256_file(path))
        )


if __name__ == "__main__":
    main()
