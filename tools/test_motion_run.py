import subprocess
import time
import ctypes
import os

user32 = ctypes.windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)

app_exe = r"C:\Program Files\Duwn Mirror\duwn-mirror.exe"
print(f"[TEST] Launching: {app_exe} --test-motion")
proc = subprocess.Popen([app_exe, "--test-motion"])
pid = proc.pid
print(f"[TEST] Started PID: {pid}")

found = {}
try:
    for i in range(15):
        time.sleep(1.0)
        found = {}
        def cb(hwnd, lparam):
            p = ctypes.c_ulong()
            user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
            if p.value == pid:
                buf = ctypes.create_unicode_buffer(256)
                user32.GetClassNameW(hwnd, buf, 256)
                tbuf = ctypes.create_unicode_buffer(256)
                user32.GetWindowTextW(hwnd, tbuf, 256)
                found[buf.value] = (hwnd, tbuf.value, bool(user32.IsWindowVisible(hwnd)))
            return True
        user32.EnumWindows(WNDENUMPROC(cb), 0)
        print(f"[{i+1}s] Windows for PID {pid}: {[(k, v[2]) for k, v in found.items()]}")
        if "DUWNMirrorOutputWindow" in found and found["DUWNMirrorOutputWindow"][2]:
            print(f"[PASS] OutputWindow is VISIBLE: HWND=0x{found['DUWNMirrorOutputWindow'][0]:X}")
            break
finally:
    if "DUWNMirrorMainWindow" in found:
        print("[TEST] Sending WM_CLOSE to MainWindow...")
        user32.PostMessageW(found["DUWNMirrorMainWindow"][0], 0x0010, 0, 0)
        time.sleep(2.0)
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except Exception:
            proc.kill()
