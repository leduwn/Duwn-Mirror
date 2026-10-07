import os
import sys
import time
import json
import ctypes
from ctypes import wintypes
import subprocess
import glob

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
psapi = ctypes.windll.psapi

class PROCESS_MEMORY_COUNTERS(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("PageFaultCount", wintypes.DWORD),
        ("PeakWorkingSetSize", ctypes.c_size_t),
        ("WorkingSetSize", ctypes.c_size_t),
        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPagedPoolUsage", ctypes.c_size_t),
        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
        ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
        ("PagefileUsage", ctypes.c_size_t),
        ("PeakPagefileUsage", ctypes.c_size_t),
    ]

def get_process_metrics_win32(pid, hwnd):
    # Returns (is_responding, working_set_mb, handle_count)
    is_responding = True
    if hwnd:
        is_hung = user32.IsHungAppWindow(hwnd)
        is_responding = not bool(is_hung)

    ws_mb = 0.0
    handle_count = 0

    h_proc = kernel32.OpenProcess(0x0400 | 0x1000, False, pid) # PROCESS_QUERY_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION
    if h_proc:
        pmc = PROCESS_MEMORY_COUNTERS()
        pmc.cb = ctypes.sizeof(PROCESS_MEMORY_COUNTERS)
        if psapi.GetProcessMemoryInfo(h_proc, ctypes.byref(pmc), pmc.cb):
            ws_mb = pmc.WorkingSetSize / (1024.0 * 1024.0)
        h_cnt = wintypes.DWORD()
        if kernel32.GetProcessHandleCount(h_proc, ctypes.byref(h_cnt)):
            handle_count = h_cnt.value
        kernel32.CloseHandle(h_proc)

    return (is_responding, ws_mb, handle_count)

def count_crashes_since(start_time):
    local_app_data = os.environ.get("LOCALAPPDATA", "")
    crash_dirs = [
        os.path.join(local_app_data, "Duwn Mirror", "Crashes"),
        os.path.join(local_app_data, "CrashDumps")
    ]
    new_dumps = []
    for cdir in crash_dirs:
        if os.path.exists(cdir):
            for f in os.listdir(cdir):
                if f.endswith(".dmp") or f.endswith(".txt"):
                    full_p = os.path.join(cdir, f)
                    if os.path.getmtime(full_p) >= start_time:
                        new_dumps.append(full_p)
    return new_dumps

def run_ten_cycles():
    exe_path = r"C:\Program Files\Duwn Mirror\duwn-mirror.exe"
    shortcut_path = r"C:\Users\Public\Desktop\Duwn Mirror.lnk"

    print("=== STARTING 10 OPEN/CLOSE CYCLES ===")
    results = []

    for i in range(1, 11):
        is_shortcut = (i % 2 == 0)
        mode_str = "Desktop Shortcut" if is_shortcut else "Direct Executable"
        print(f"\n--- Cycle {i}/10: {mode_str} ---")

        cycle_start_time = time.time()
        initial_pids = set()
        try:
            out = subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-Process duwn-mirror -ErrorAction SilentlyContinue).Id"], text=True)
            for line in out.strip().splitlines():
                if line.strip():
                    initial_pids.add(int(line.strip()))
        except Exception:
            pass

        if is_shortcut:
            subprocess.Popen(["cmd.exe", "/c", "start", "", shortcut_path], shell=False)
            proc_pid = None
            for _ in range(80):
                time.sleep(0.1)
                try:
                    out = subprocess.check_output(["powershell", "-NoProfile", "-Command", "(Get-Process duwn-mirror -ErrorAction SilentlyContinue).Id"], text=True)
                    for line in out.strip().splitlines():
                        if line.strip():
                            p = int(line.strip())
                            if p not in initial_pids:
                                proc_pid = p
                                break
                except Exception:
                    pass
                if proc_pid:
                    break
            proc = None
        else:
            proc = subprocess.Popen([exe_path])
            proc_pid = proc.pid

        if not proc_pid:
            print(f"FAILED to start process in cycle {i}")
            results.append({"cycle": i, "mode": mode_str, "status": "FAIL_TO_START"})
            continue

        print(f"PID: {proc_pid}")

        # Wait for main window
        hwnd = None
        for _ in range(80):
            hwnd = find_window_by_class("DUWNMirrorMainWindow", proc_pid)
            if hwnd:
                break
            time.sleep(0.1)

        if not hwnd:
            print(f"FAILED: Main window DUWNMirrorMainWindow not found for PID {proc_pid}")
            results.append({"cycle": i, "mode": mode_str, "pid": proc_pid, "status": "FAIL_WINDOW_NOT_FOUND"})
            if proc:
                proc.kill()
            continue

        print(f"Main window HWND: {hex(hwnd)}")

        # Monitor for >= 10 seconds
        obs_duration = 10.5
        t0 = time.time()
        samples = []
        while (time.time() - t0) < obs_duration:
            resp, ws, handles = get_process_metrics(proc_pid)
            samples.append((resp, ws, handles))
            time.sleep(1.0)

        avg_ws = sum(s[1] for s in samples) / len(samples) if samples else 0
        all_resp = all(s[0] for s in samples)
        max_h = max(s[2] for s in samples) if samples else 0
        print(f"Observation complete ({obs_duration:.1f}s): Responding={all_resp}, Avg WS={avg_ws:.1f}MB, Max Handles={max_h}")

        # Close cleanly with WM_CLOSE
        print("Sending WM_CLOSE...")
        user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)

        # Wait for exit
        t_exit = time.time()
        exited = False
        while (time.time() - t_exit) < 6.0:
            try:
                subprocess.check_output(["powershell", "-NoProfile", "-Command", f"Get-Process -Id {proc_pid} -ErrorAction Stop"], text=True)
                time.sleep(0.2)
            except subprocess.CalledProcessError:
                exited = True
                break

        exit_time = time.time() - t_exit
        dumps = count_crashes_since(cycle_start_time)

        if exited and len(dumps) == 0:
            print(f"Clean exit in {exit_time:.2f}s, 0 crash dumps. SUCCESS.")
            results.append({
                "cycle": i, "mode": mode_str, "pid": proc_pid, "status": "PASS",
                "responding": all_resp, "avg_ws_mb": avg_ws, "exit_time_sec": exit_time,
                "dumps": len(dumps)
            })
        else:
            print(f"Exit failed or crash dump found: exited={exited}, dumps={dumps}")
            results.append({
                "cycle": i, "mode": mode_str, "pid": proc_pid, "status": "FAIL",
                "exited": exited, "dumps": dumps
            })
            if not exited:
                try:
                    subprocess.call(["taskkill", "/F", "/PID", str(proc_pid)])
                except Exception:
                    pass

        time.sleep(1.0)

    return results

if __name__ == "__main__":
    res = run_ten_cycles()
    print("\n=== SUMMARY OF 10 CYCLES ===")
    for r in res:
        print(r)
