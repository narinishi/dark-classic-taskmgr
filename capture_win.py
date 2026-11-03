"""Capture a Win32 window's rendered contents to PNG, occlusion-proof.

Uses PrintWindow (WM_PRINT), which asks the target to render its own
contents (non-client + client + children) into a caller-supplied DC,
so the result is correct even if the window is behind other windows or
partially off-screen.

Tab switching (for tabbed dialogs like Task Manager) is content-verified:
it switches via the tab control's notification-firing path (TCM_SETCURFOCUS
+ arrow-key messages routed to the tab control itself, which send
TCN_SELCHANGE so the app swaps its page) and then *verifies* the visible
page content actually matches the requested tab before capturing. This
avoids the TCM_SETCURSEL trap (which only moves the highlight without
swapping the page).
"""

import argparse
import ctypes as ct
from ctypes import wintypes as wt
import sys
import time
from pathlib import Path

from PIL import Image

u = ct.windll.user32
g = ct.windll.gdi32

u.SetProcessDPIAware()

u.PrintWindow.argtypes = [wt.HWND, wt.HDC, wt.UINT]
u.PrintWindow.restype = wt.BOOL
u.GetWindowRect.argtypes = [wt.HWND, ct.POINTER(wt.RECT)]
u.IsIconic.argtypes = [wt.HWND]
u.IsIconic.restype = wt.BOOL
u.ShowWindow.argtypes = [wt.HWND, ct.c_int]
u.SetForegroundWindow.argtypes = [wt.HWND]
u.GetWindowThreadProcessId.argtypes = [wt.HWND, ct.POINTER(wt.DWORD)]
u.GetWindowThreadProcessId.restype = wt.DWORD
u.EnumWindows.argtypes = [ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM), wt.LPARAM]
u.EnumWindows.restype = wt.BOOL
u.EnumChildWindows.argtypes = [wt.HWND, ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM), wt.LPARAM]
u.EnumChildWindows.restype = wt.BOOL
u.GetClassNameW.argtypes = [wt.HWND, wt.LPWSTR, ct.c_int]
u.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ct.c_int]
u.IsWindowVisible.argtypes = [wt.HWND]
u.IsWindowVisible.restype = wt.BOOL
u.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
u.SendMessageW.restype = wt.LPARAM
u.InvalidateRect.argtypes = [wt.HWND, ct.c_void_p, wt.BOOL]
u.UpdateWindow.argtypes = [wt.HWND]

g.CreateCompatibleDC.argtypes = [wt.HDC]
g.CreateCompatibleDC.restype = wt.HDC
g.CreateCompatibleBitmap.argtypes = [wt.HDC, ct.c_int, ct.c_int]
g.CreateCompatibleBitmap.restype = wt.HBITMAP
g.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
g.SelectObject.restype = wt.HGDIOBJ
g.DeleteObject.argtypes = [wt.HGDIOBJ]
g.DeleteDC.argtypes = [wt.HDC]
g.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, wt.LPVOID, wt.LPVOID, wt.UINT]

PW_RENDERFULLCONTENT = 2

# Tab-control messages (winuser.h / commctrl.h)
WM_KEYDOWN = 0x0100
TCM_GETCURSEL = 0x130B
TCM_SETCURSEL = 0x130C
TCM_SETCURFOCUS = 0x1330   # sets focus+selection AND fires TCN_SELCHANGE
TCM_GETCURFOCUS = 0x132F
VK_LEFT = 0x25
VK_RIGHT = 0x27

# Classic Task Manager's tab order by TCM index (verified: index 2 ==
# Performance, confirmed by UIA seeing the 'Performance' page pane after
# TCM_SETCURSEL(2)). NOTE: UIA child *enumeration* order differs and must
# not be used to derive indexes.
TAB_NAMES = ['Applications', 'Processes', 'Performance', 'Networking', 'Users', 'Services']

class BITMAPINFOHEADER(ct.Structure):
    _fields_ = [
        ('biSize', wt.DWORD), ('biWidth', wt.LONG), ('biHeight', wt.LONG),
        ('biPlanes', wt.WORD), ('biBitCount', wt.WORD),
        ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD),
        ('biXPelsPerMeter', wt.LONG), ('biYPelsPerMeter', wt.LONG),
        ('biClrUsed', wt.DWORD), ('biClrImportant', wt.DWORD),
    ]

class BITMAPINFO(ct.Structure):
    _fields_ = [('bmiHeader', BITMAPINFOHEADER), ('bmiColors', wt.DWORD * 4)]


def class_name(hwnd):
    buf = ct.create_unicode_buffer(256)
    u.GetClassNameW(hwnd, buf, len(buf))
    return buf.value


def window_text(hwnd):
    buf = ct.create_unicode_buffer(512)
    u.GetWindowTextW(hwnd, buf, len(buf))
    return buf.value


def find_window(pid=None, title=None, process=None):
    if process:
        import subprocess
        out = subprocess.run(
            ['powershell', '-NoProfile', '-Command',
             f"(Get-Process {process} -ErrorAction SilentlyContinue | Select-Object -First 1).Id"],
            capture_output=True, text=True).stdout.strip()
        if out.isdigit():
            pid = int(out)
        else:
            return None
    found = []

    def cb(h, lp):
        if not u.IsWindowVisible(h):
            return True
        if pid:
            wpid = wt.DWORD()
            u.GetWindowThreadProcessId(h, ct.byref(wpid))
            if wpid.value != pid:
                return True
        if title:
            if title not in window_text(h):
                return True
        found.append(h)
        return True

    proc = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(cb)
    u.EnumWindows(proc, 0)
    if not found:
        return None
    if title:
        exact = [h for h in found if window_text(h) == title]
        return exact[0] if exact else found[0]
    return found[0]


def find_tab_control(root):
    result = []

    def cb(h, lp):
        if class_name(h) == 'SysTabControl32':
            result.append(h)
            return False
        return True

    proc = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)(cb)
    u.EnumChildWindows(root, proc, 0)
    return result[0] if result else None


def active_tab_name(root):
    """Return the name of the currently visible page (Task Manager creates one
    child dialog per page; only the active one is shown). Uses UIA, whose pane
    Name matches the tab name. This is the content-level source of truth —
    independent of TCM_GETCURSEL (which can move ahead of the real page)."""
    try:
        import uiautomation as auto
        root_c = auto.ControlFromHandle(root)
        for k in root_c.GetChildren():
            try:
                if k.ControlTypeName == 'PaneControl' and k.Name in TAB_NAMES:
                    return k.Name
            except Exception:
                continue
        return None
    except Exception:
        return None


def tab_item_order(root):
    """Return tab names in true left-to-right order using UIA bounding rects.
    The tab control's TCM index order matches visual order; UIA's child
    enumeration order does NOT. Deriving order from x-position avoids both
    hardcoding and cross-process TCITEM text reads."""
    try:
        import uiautomation as auto
        tc = find_tab_control(root)
        if not tc:
            return []
        items = []
        for item in auto.ControlFromHandle(tc).GetChildren():
            try:
                if item.ControlTypeName == 'TabItemControl':
                    rect = item.BoundingRectangle
                    items.append((rect.left, item.Name))
            except Exception:
                continue
        items.sort(key=lambda t: t[0])
        return [n for _, n in items]
    except Exception:
        return []


def select_tab(root, tab):
    if tab is None:
        return
    tc = find_tab_control(root)
    if not tc:
        print('no SysTabControl32 found; skipping tab switch', file=sys.stderr)
        return

    # resolve target index + name using UIA-derived visual order
    order = tab_item_order(root)
    if order:
        print(f'tab visual order (left-to-right): {order}')
    else:
        order = TAB_NAMES
    if isinstance(tab, int) or (isinstance(tab, str) and tab.isdigit()):
        idx = int(tab)
        target_name = order[idx] if 0 <= idx < len(order) else None
    else:
        target_name = tab
    if target_name not in order:
        print(f'unknown tab {tab!r}; known: {order}', file=sys.stderr)
        return
    target_idx = order.index(target_name)

    u.SetForegroundWindow(root)
    time.sleep(0.2)

    def current():
        return u.SendMessageW(tc, TCM_GETCURSEL, 0, 0)

    def page_now():
        return active_tab_name(root)

    # already there (content-verified)
    if page_now() == target_name:
        print(f'already on {target_name!r}')
        return

    # Method 1: TCM_SETCURFOCUS — unlike TCM_SETCURSEL this fires
    # TCN_SELCHANGE, so the app swaps its page dialog.
    u.SendMessageW(tc, TCM_SETCURFOCUS, target_idx, 0)
    u.InvalidateRect(root, None, True)
    u.UpdateWindow(root)
    time.sleep(0.4)
    if page_now() == target_name:
        print(f'selected {target_name!r} via TCM_SETCURFOCUS (content verified)')
        return

    # Method 2: arrow-key messages sent directly to the tab control. Its
    # WM_KEYDOWN handler moves the selection and sends TCN_SELCHANGE. Sending
    # to the tab control handle (not the root) drives that handler without
    # needing real keyboard focus, and is overlap-robust (direct message).
    cur = current()
    step = VK_RIGHT if cur < target_idx else VK_LEFT
    for _ in range(abs(target_idx - cur)):
        u.SendMessageW(tc, WM_KEYDOWN, step, 0)
        time.sleep(0.12)
    u.InvalidateRect(root, None, True)
    u.UpdateWindow(root)
    time.sleep(0.4)
    if page_now() == target_name:
        print(f'selected {target_name!r} via arrow keys (content verified)')
        return

    # Method 3: brute-force scan every index via TCM_SETCURFOCUS until the
    # visible page content matches. Survives wrong-index assumptions.
    count = u.SendMessageW(tc, 0x1304, 0, 0)  # TCM_GETITEMCOUNT
    for i in range(count):
        u.SendMessageW(tc, TCM_SETCURFOCUS, i, 0)
        u.InvalidateRect(root, None, True)
        u.UpdateWindow(root)
        time.sleep(0.5)
        if page_now() == target_name:
            print(f'selected {target_name!r} via brute-force index {i} (content verified)')
            return
    print(f'WARNING: could not reach {target_name!r} '
          f'(visible page now: {page_now()!r}); capture may show the wrong tab',
          file=sys.stderr)


def capture_printwindow(hwnd):
    wr = wt.RECT()
    u.GetWindowRect(hwnd, ct.byref(wr))
    w = wr.right - wr.left
    h = wr.bottom - wr.top
    if w <= 0 or h <= 0:
        return None
    u.GetWindowDC.argtypes = [ct.c_void_p]
    u.GetWindowDC.restype = ct.c_void_p
    hdc = u.GetWindowDC(hwnd)
    memdc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, h)
    old = g.SelectObject(memdc, bmp)
    ok = u.PrintWindow(hwnd, memdc, PW_RENDERFULLCONTENT)
    bi = BITMAPINFO()
    bi.bmiHeader.biSize = ct.sizeof(BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h  # top-down
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 32
    bi.bmiHeader.biCompression = 0
    buf = ct.create_string_buffer(w * h * 4)
    lines = g.GetDIBits(memdc, bmp, 0, h, buf, ct.byref(bi), 0)
    g.SelectObject(memdc, old)
    g.DeleteObject(bmp)
    g.DeleteDC(memdc)
    u.ReleaseDC.argtypes = [ct.c_void_p, ct.c_void_p]
    u.ReleaseDC.restype = ct.c_int
    u.ReleaseDC(ct.c_void_p(hwnd), ct.c_void_p(hdc))
    if not ok or lines != h:
        return None
    return Image.frombytes('RGB', (w, h), buf.raw, 'raw', 'BGRX')


def is_flat(img):
    extrema = img.convert('RGB').getextrema()
    return all(lo == hi for lo, hi in extrema)


# ---- menu-open + popup capture (state-dependent element support) ----
WM_SYSCOMMAND = 0x0112
SC_KEYMENU = 0xF100
VK_MENU = 0x12
VK_DOWN = 0x28
KEYEVENTF_KEYUP = 0x0002

u.keybd_event.argtypes = [ct.c_ubyte, ct.c_ubyte, wt.DWORD, ct.c_size_t]
u.keybd_event.restype = None


def pid_of(hwnd):
    wpid = wt.DWORD()
    u.GetWindowThreadProcessId(wt.HWND(hwnd), ct.byref(wpid))
    return wpid.value


def open_menu(hwnd, menu_accel=0):
    """Open a menu popup without triggering focused buttons.

    Primary: SendMessageW(SC_KEYMENU, accel) -- synchronous, processed
    in the window's own thread like a real menu click. No keyboard
    input exists to land on a focused control (the earlier keybd_event
    Alt/arrow injection triggered the focused Resource Monitor button).

    Fallback (if no popup opens): keyboard Alt+arrow, but ONLY after
    moving keyboard focus to the main window itself via
    AttachThreadInput+SetFocus, so keys can never reach a button.
    """
    u.SetForegroundWindow(wt.HWND(hwnd))
    time.sleep(0.3)

    # SendMessageW is SYNCHRONOUS: the target's SC_KEYMENU handler opens
    # the menu and runs its modal loop inside the call -> our script
    # would block until the menu closes. Run it on a daemon thread so
    # we can continue (capture etc.) while the menu stays open.
    import threading
    t = threading.Thread(
        target=lambda: u.SendMessageW(wt.HWND(hwnd), WM_SYSCOMMAND, SC_KEYMENU, menu_accel),
        daemon=True)
    t.start()
    time.sleep(0.8)
    if find_popup_windows(pid_of(hwnd)):
        return

    # fallback: move focus off any control, then inject Alt+Down
    k32 = ct.windll.kernel32
    k32.GetCurrentThreadId.restype = wt.DWORD
    k32.AttachThreadInput.argtypes = [wt.DWORD, wt.DWORD, wt.BOOL]
    u.SetFocus.argtypes = [wt.HWND]
    u.SetFocus.restype = wt.HWND
    wpid = wt.DWORD()
    wtid = u.GetWindowThreadProcessId(wt.HWND(hwnd), ct.byref(wpid))
    ctid = k32.GetCurrentThreadId()
    if wtid and k32.AttachThreadInput(ctid, wtid, True):
        u.SetFocus(wt.HWND(hwnd))
        k32.AttachThreadInput(ctid, wtid, False)
    time.sleep(0.2)
    u.keybd_event(VK_MENU, 0, 0, 0)
    u.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.2)
    u.keybd_event(VK_DOWN, 0, 0, 0)
    u.keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0)
    time.sleep(0.5)


def find_popup_windows(pid):
    """Find all visible popup windows (menus) owned by the target PID."""
    popups = []
    _ENUM = ct.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(h, _lp):
        wpid = wt.DWORD()
        u.GetWindowThreadProcessId(h, ct.byref(wpid))
        if wpid.value == pid and u.IsWindowVisible(h):
            cls = class_name(h)
            if cls in ('#32768', '#32770', '#32769'):
                popups.append(int(h))
        return True

    u.EnumWindows(_ENUM(cb), 0)
    return popups


def main():
    ap = argparse.ArgumentParser()
    sel = ap.add_mutually_exclusive_group(required=True)
    sel.add_argument('--pid', type=int)
    sel.add_argument('--title')
    sel.add_argument('--process')
    ap.add_argument('--tab', help='tab name (e.g. Performance) or index')
    ap.add_argument('--output', required=True)
    ap.add_argument('--delay', type=float, default=0.3,
                    help='settle seconds between tab switch / menu open and capture (default 0.3; capped at 0.3)')
    ap.add_argument('--rect', help='unused, kept for compatibility')
    ap.add_argument('--client', action='store_true')
    ap.add_argument('--print-window', action='store_true')
    ap.add_argument('--menu-accel', help="open a menu by accelerator char (e.g. 'V' for View) before capture")
    ap.add_argument('--popups-prefix', help='also capture popup windows (menus) as <prefix>-NNN.png')
    args = ap.parse_args()

    hwnd = find_window(pid=args.pid, title=args.title, process=args.process)
    if not hwnd:
        print('no matching visible top-level window found', file=sys.stderr)
        sys.exit(1)

    print(f'target hwnd={hwnd} title={window_text(hwnd)!r} class={class_name(hwnd)!r}')

    if args.tab is not None:
        select_tab(hwnd, args.tab)
        # content-level confirmation before capturing
        vis = active_tab_name(hwnd)
        print(f'visible page after switch: {vis!r}')

    if args.menu_accel is not None:
        accel = ord(args.menu_accel[0]) if args.menu_accel else 0
        open_menu(hwnd, accel)
        print(f'menu open requested (accel={args.menu_accel!r})')

    if args.delay > 1.0:
        args.delay = 0.3
    time.sleep(args.delay)

    img = capture_printwindow(hwnd)
    method = 'PrintWindow(PW_RENDERFULLCONTENT)'
    if img is None:
        print('PrintWindow failed', file=sys.stderr)
        sys.exit(1)

    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f'captured {img.size[0]}x{img.size[1]} via {method} -> {out}')

    if args.popups_prefix:
        pid = pid_of(hwnd)
        popups = find_popup_windows(pid)
        print(f'popup windows found for pid {pid}: {len(popups)}')
        for i, ph in enumerate(popups):
            pimg = capture_printwindow(ph)
            if pimg is None:
                print(f'  popup {i} hwnd={ph}: PrintWindow failed')
                continue
            pout = out.parent / f'{args.popups_prefix}-{i:03d}.png'
            pimg.save(pout)
            print(f'  popup {i} hwnd={ph} class={class_name(ph)!r} size={pimg.size} -> {pout}')


if __name__ == '__main__':
    main()
