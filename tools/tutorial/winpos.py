"""Move and resize a top-level window by title, so takes have a known rectangle to crop later.

    python tools/tutorial/winpos.py "WotM - Mod kit" 480 270 1600 900
    python tools/tutorial/winpos.py "WotM - Mod kit"          # prints the current rectangle
"""
import ctypes
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
user32.SetProcessDPIAware()


def find(title):
    hits = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        n = user32.GetWindowTextLengthW(hwnd)
        if n and user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            if title.lower() in buf.value.lower():
                hits.append((hwnd, buf.value))
        return True

    user32.EnumWindows(cb, 0)
    return hits


def main():
    hits = find(sys.argv[1])
    if not hits:
        sys.exit('no window matches %r' % sys.argv[1])
    hwnd, name = hits[0]
    if len(sys.argv) >= 6:
        x, y, w, h = map(int, sys.argv[2:6])
        user32.ShowWindow(hwnd, 9)   # SW_RESTORE
        user32.MoveWindow(hwnd, x, y, w, h, True)
        user32.SetForegroundWindow(hwnd)
    r = wintypes.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    print('%s: %d %d %d %d' % (name, r.left, r.top, r.right - r.left, r.bottom - r.top))


if __name__ == '__main__':
    main()
