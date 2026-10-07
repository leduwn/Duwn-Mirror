import ctypes
import time
import json
import os
import sys

user32 = ctypes.windll.user32

class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long),
                ("top", ctypes.c_long),
                ("right", ctypes.c_long),
                ("bottom", ctypes.c_long)]

WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

def find_app_windows(target_pid):
    hwnds = {}
    def enum_cb(hwnd, lparam):
        pid = ctypes.c_ulong()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == target_pid:
            buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, buf, 256)
            cls = buf.value
            title_buf = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(hwnd, title_buf, 256)
            hwnds[cls] = (hwnd, title_buf.value)
        return True

    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return hwnds

def get_rect(hwnd):
    r = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top

def run_test(pid):
    print(f"Testing PID: {pid}")
    hwnds = find_app_windows(pid)
    for k, v in hwnds.items():
        print(f"  Window: {k} -> HWND=0x{v[0]:X}, Title='{v[1]}'")

    main_hwnd = hwnds.get("DUWNMirrorMainWindow", (0,))[0]
    output_hwnd = hwnds.get("DUWNMirrorOutputWindow", (0,))[0]

    if not main_hwnd or not output_hwnd:
        print("ERROR: Windows not found!")
        sys.exit(1)

    WM_APP = 0x8000

    # 1. Check initial visibility
    vis_0 = bool(user32.IsWindowVisible(output_hwnd))
    print(f"1. Initial Output visible: {vis_0}")

    # 2. Toggle Open
    user32.PostMessageW(main_hwnd, WM_APP + 0x022, 0, 0)
    time.sleep(0.6)
    vis_1 = bool(user32.IsWindowVisible(output_hwnd))
    rect_1 = get_rect(output_hwnd)
    print(f"2. After Toggle Open: visible={vis_1}, rect={rect_1}")

    # 3. Resize window (SetWindowPos) to simulate user dragging / resizing
    # New size: 800 x 1280 (preserving aspect / comfortable fit)
    SWP_NOZORDER = 0x0004
    SWP_NOACTIVATE = 0x0010
    target_x, target_y, target_w, target_h = rect_1[0], rect_1[1], 720, 1150
    user32.SetWindowPos(output_hwnd, 0, target_x, target_y, target_w, target_h, SWP_NOZORDER | SWP_NOACTIVATE)
    time.sleep(0.5)
    rect_resized = get_rect(output_hwnd)
    print(f"3. After Resize: rect={rect_resized}")

    # 4. Toggle Close
    user32.PostMessageW(main_hwnd, WM_APP + 0x022, 0, 0)
    time.sleep(0.6)
    vis_2 = bool(user32.IsWindowVisible(output_hwnd))
    print(f"4. After Toggle Close: visible={vis_2}")

    # 5. Toggle Open again
    user32.PostMessageW(main_hwnd, WM_APP + 0x022, 0, 0)
    time.sleep(0.6)
    vis_3 = bool(user32.IsWindowVisible(output_hwnd))
    rect_3 = get_rect(output_hwnd)
    print(f"5. After Reopen: visible={vis_3}, rect={rect_3}")

    # 6. Graceful close main window via WM_CLOSE
    WM_CLOSE = 0x0010
    print("6. Sending WM_CLOSE to MainWindow...")
    user32.PostMessageW(main_hwnd, WM_CLOSE, 0, 0)
    time.sleep(2.0)

    # 7. Check settings.json persistence
    settings_file = os.path.expandvars(r"%LOCALAPPDATA%\Duwn Mirror\settings.json")
    if os.path.exists(settings_file):
        with open(settings_file, "r", encoding="utf-8") as f:
            data = json.load(f)
        unclean = data.get("unclean_shutdown")
        out_w = data.get("output_window_w")
        out_h = data.get("output_window_h")
        out_x = data.get("output_x")
        out_y = data.get("output_y")
        print(f"7. Settings persisted: unclean_shutdown={unclean}, geometry=({out_x}, {out_y}, {out_w}x{out_h})")
    else:
        print("ERROR: settings.json not found!")

if __name__ == "__main__":
    pid = int(sys.argv[1]) if len(sys.argv) > 1 else 25924
    run_test(pid)
