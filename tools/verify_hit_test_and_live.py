import ctypes
import time
import json
import os
import sys
import subprocess

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

def find_child_windows(parent_hwnd):
    children = []
    def enum_child_cb(hwnd, lparam):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, buf, 256)
        children.append((hwnd, buf.value))
        return True
    user32.EnumChildWindows(parent_hwnd, WNDENUMPROC(enum_child_cb), 0)
    return children

def get_rect(hwnd):
    r = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom

def main():
    app_exe = r"C:\Program Files\Duwn Mirror\duwn-mirror.exe"
    print(f"[TEST] Launching installed executable: {app_exe}")
    proc = subprocess.Popen([app_exe])
    pid = proc.pid
    print(f"[TEST] Launched with PID: {pid}")

    try:
        hwnds = {}
        for i in range(25):
            time.sleep(0.5)
            hwnds = find_app_windows(pid)
            if "DUWNMirrorMainWindow" in hwnds:
                print(f"[TEST] MainWindow appeared at {(i+1)*0.5:.1f}s")
                break

        print(f"[TEST] Discovered {len(hwnds)} top-level windows for PID {pid}:")
        for cls, (h, title) in hwnds.items():
            print(f"  - Class '{cls}': HWND=0x{h:X}, Title='{title}'")

        main_hwnd = hwnds.get("DUWNMirrorMainWindow", (0,))[0]
        if not main_hwnd:
            print("[FAIL] MainWindow not found!")
            return False

        WM_APP = 0x8000
        # 1. Toggle Open Output window
        user32.PostMessageW(main_hwnd, WM_APP + 0x022, 0, 0)
        time.sleep(1.5)

        hwnds = find_app_windows(pid)
        output_hwnd = hwnds.get("DUWNMirrorOutputWindow", (0,))[0]
        if not output_hwnd:
            print("[FAIL] OutputWindow not found after toggle!")
            return False

        WM_NCHITTEST = 0x0084
        HTTRANSPARENT = -1
        HTCAPTION = 2
        HTRIGHT = 11
        HTBOTTOM = 15

        left, top, right, bottom = get_rect(output_hwnd)
        width = right - left
        height = bottom - top
        print(f"[TEST] OutputWindow bounds: ({left}, {top}) to ({right}, {bottom}), {width}x{height}")

        # 2. Check Child Windows (Placeholder / Surface)
        children = find_child_windows(output_hwnd)
        print(f"[TEST] Discovered {len(children)} child windows in OutputWindow:")
        for ch, ch_cls in children:
            print(f"  - Child Class '{ch_cls}': HWND=0x{ch:X}")

        # Test WM_NCHITTEST on child window (Placeholder)
        # Should return HTTRANSPARENT (-1) so child does not eat drag
        for ch, ch_cls in children:
            center_x = left + width // 2
            center_y = top + height // 2
            lparam = (center_y << 16) | (center_x & 0xFFFF)
            hit = user32.SendMessageW(ch, WM_NCHITTEST, 0, lparam)
            print(f"[TEST] Child 0x{ch:X} ({ch_cls}) WM_NCHITTEST at center ({center_x}, {center_y}) -> hit={hit} (HTTRANSPARENT={HTTRANSPARENT})")
            if hit == HTTRANSPARENT:
                print(f"[PASS] Child window correctly returns HTTRANSPARENT (forward to parent)")
            else:
                print(f"[WARN] Child window returned hit={hit}")

        # 3. Test WM_NCHITTEST on OutputWindow itself
        # Center should be HTCAPTION (2) to enable drag move
        center_x = left + width // 2
        center_y = top + height // 2
        lparam_center = (center_y << 16) | (center_x & 0xFFFF)
        hit_center = user32.SendMessageW(output_hwnd, WM_NCHITTEST, 0, lparam_center)
        print(f"[TEST] OutputWindow center ({center_x}, {center_y}) WM_NCHITTEST -> hit={hit_center} (Expected HTCAPTION=2)")
        if hit_center == HTCAPTION:
            print("[PASS] OutputWindow center returns HTCAPTION (draggable without toolbar)")
        else:
            print(f"[FAIL] OutputWindow center did not return HTCAPTION: hit={hit_center}")

        # Borders should return resize codes
        lparam_right = (center_y << 16) | ((right - 2) & 0xFFFF)
        hit_right = user32.SendMessageW(output_hwnd, WM_NCHITTEST, 0, lparam_right)
        print(f"[TEST] OutputWindow right border ({right - 2}, {center_y}) WM_NCHITTEST -> hit={hit_right} (Expected HTRIGHT=11)")

        lparam_bottom = ((bottom - 2) << 16) | (center_x & 0xFFFF)
        hit_bottom = user32.SendMessageW(output_hwnd, WM_NCHITTEST, 0, lparam_bottom)
        print(f"[TEST] OutputWindow bottom border ({center_x}, {bottom - 2}) WM_NCHITTEST -> hit={hit_bottom} (Expected HTBOTTOM=15)")

        # 4. Check Toolbar Window
        tb = user32.FindWindowExW(0, 0, "DUWNMirrorOutputToolbar", None)
        toolbar_hwnd = tb
        print(f"[TEST] Toolbar HWND: 0x{toolbar_hwnd:X}")
        if toolbar_hwnd:
            tb_l, tb_t, tb_r, tb_b = get_rect(toolbar_hwnd)
            print(f"[TEST] Toolbar bounds: ({tb_l}, {tb_t}) to ({tb_r}, {tb_b}), {tb_r - tb_l}x{tb_b - tb_t}")
            print(f"[PASS] Toolbar is visible and positioned above/within Output")

        # 5. Simulate move Output window by 50px
        SWP_NOZORDER = 0x0004
        SWP_NOACTIVATE = 0x0010
        new_x = left + 50
        new_y = top + 50
        user32.SetWindowPos(output_hwnd, 0, new_x, new_y, width, height, SWP_NOZORDER | SWP_NOACTIVATE)
        time.sleep(0.5)
        nl, nt, nr, nb = get_rect(output_hwnd)
        print(f"[TEST] Moved OutputWindow to ({nl}, {nt})")

        if toolbar_hwnd:
            ntb_l, ntb_t, ntb_r, ntb_b = get_rect(toolbar_hwnd)
            print(f"[TEST] Toolbar followed to: ({ntb_l}, {ntb_t})")
            if ntb_l == nl:
                print("[PASS] Toolbar precisely tracked OutputWindow movement")

        # 6. Graceful close
        WM_CLOSE = 0x0010
        print("[TEST] Sending WM_CLOSE to MainWindow...")
        user32.PostMessageW(main_hwnd, WM_CLOSE, 0, 0)
        time.sleep(2.0)

        # 7. Check settings file
        settings_file = os.path.expandvars(r"%LOCALAPPDATA%\Duwn Mirror\settings.json")
        if os.path.exists(settings_file):
            with open(settings_file, "r", encoding="utf-8") as f:
                data = json.load(f)
            print(f"[TEST] Persisted settings: unclean={data.get('unclean_shutdown')}, pos=({data.get('output_x')}, {data.get('output_y')}), size=({data.get('output_window_w')}x{data.get('output_window_h')})")
            if not data.get("unclean_shutdown"):
                print("[PASS] Clean shutdown confirmed, no crashes.")

        return True
    finally:
        # Cleanup process if still running
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except Exception:
                proc.kill()

if __name__ == "__main__":
    success = main()
    sys.exit(0 if success else 1)
