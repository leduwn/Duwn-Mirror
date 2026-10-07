import ctypes
import time
import sys
import json
import os

user32 = ctypes.windll.user32
WM_APP = 0x8000
WM_CLOSE = 0x0010

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

pid = int(sys.argv[1])
hwnds = {}
def enum_cb(hwnd, lparam):
    wpid = ctypes.c_ulong()
    user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
    if wpid.value == pid:
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        hwnds[buf.value] = hwnd
    return True

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
user32.EnumWindows(WNDENUMPROC(enum_cb), 0)

main_hwnd = hwnds.get("DUWNMirrorMainWindow")
output_hwnd = hwnds.get("DUWNMirrorOutputWindow")

print(f"Main HWND: {hex(main_hwnd)}, Output HWND: {hex(output_hwnd)}")

# Open output window
user32.PostMessageW(main_hwnd, WM_APP + 0x022, 0, 0)
time.sleep(1.0)

r = RECT()
user32.GetWindowRect(output_hwnd, ctypes.byref(r))
w = r.right - r.left
h = r.bottom - r.top
print(f"Restored Output Window Rect: ({r.left}, {r.top}, {w}x{h})")

# Clean close
user32.PostMessageW(main_hwnd, WM_CLOSE, 0, 0)
time.sleep(1.0)
