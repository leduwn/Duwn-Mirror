import ctypes, time, os, cv2, numpy as np
from PIL import ImageGrab

user32 = ctypes.windll.user32
class RECT(ctypes.Structure):
    _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                ("right", ctypes.c_long), ("bottom", ctypes.c_long)]
class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

def get_window_rect(hwnd):
    rc = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(rc))
    return rc.left, rc.top, rc.right, rc.bottom

def real_drag(start_x, start_y, end_x, end_y, steps=25, delay=0.025, record_cb=None):
    user32.SetCursorPos(start_x, start_y)
    time.sleep(0.08)
    user32.mouse_event(0x0002, 0, 0, 0, 0) # Left down
    time.sleep(0.05)
    for i in range(1, steps + 1):
        cx = int(start_x + (end_x - start_x) * i / steps)
        cy = int(start_y + (end_y - start_y) * i / steps)
        user32.SetCursorPos(cx, cy)
        time.sleep(delay)
        if record_cb: record_cb()
    time.sleep(0.05)
    user32.mouse_event(0x0004, 0, 0, 0, 0) # Left up
    time.sleep(0.1)
    if record_cb: record_cb()

def find_window_by_class(class_name):
    hwnds = []
    def enum_cb(hwnd, lparam):
        if user32.IsWindowVisible(hwnd):
            buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, buf, 256)
            if buf.value == class_name: hwnds.append(hwnd)
        return True
    WNDENUMPROC = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
    user32.EnumWindows(WNDENUMPROC(enum_cb), 0)
    return hwnds[0] if hwnds else None

def main():
    out_dir = "artifacts/ui_verification"
    os.makedirs(out_dir, exist_ok=True)
    video_path = os.path.join(out_dir, "resize_aspect_ratio_demo.mp4")

    out_hwnd = find_window_by_class("DUWNMirrorOutputWindow")
    if not out_hwnd:
        main_hwnd = find_window_by_class("DUWNMirrorMainWindow")
        if main_hwnd:
            user32.ShowWindow(main_hwnd, 9)
            user32.SetForegroundWindow(main_hwnd)
            time.sleep(0.3)
            pt = POINT(449, 526)
            user32.ClientToScreen(main_hwnd, ctypes.byref(pt))
            user32.SetCursorPos(pt.x, pt.y)
            time.sleep(0.08)
            user32.mouse_event(0x0002, 0, 0, 0, 0)
            time.sleep(0.08)
            user32.mouse_event(0x0004, 0, 0, 0, 0)
            time.sleep(1.0)
            out_hwnd = find_window_by_class("DUWNMirrorOutputWindow")

    if not out_hwnd:
        print("[ERROR] Could not find DUWNMirrorOutputWindow!")
        return 1

    user32.ShowWindow(out_hwnd, 9)
    user32.SetForegroundWindow(out_hwnd)
    time.sleep(0.3)

    screen_w = user32.GetSystemMetrics(0)
    screen_h = user32.GetSystemMetrics(1)
    writer = cv2.VideoWriter(video_path, cv2.VideoWriter_fourcc(*'mp4v'), 20.0, (screen_w, screen_h))

    frames_captured = 0
    def record_frame():
        nonlocal frames_captured
        img = ImageGrab.grab()
        frame = cv2.cvtColor(np.array(img), cv2.COLOR_RGB2BGR)
        writer.write(frame)
        frames_captured += 1

    print("[INFO] Starting real mouse drag resize verification...")
    for _ in range(8):
        record_frame()
        time.sleep(0.03)

    l, t, r, b = get_window_rect(out_hwnd)
    print(f"[INITIAL] OutputWindow rect: ({l}, {t}, {r}, {b}) -> {r-l}x{b-t}")

    # 1. Drag Right edge outward
    real_drag(r - 2, t + (b - t) // 2, r + 130, t + (b - t) // 2, steps=25, delay=0.03, record_cb=record_frame)
    time.sleep(0.2)
    l1, t1, r1, b1 = get_window_rect(out_hwnd)
    print(f"[ACTION 1: Right Edge Drag] New size: {r1-l1}x{b1-t1}")
    for _ in range(8): record_frame(); time.sleep(0.03)

    # 2. Drag Bottom-Right corner outward
    real_drag(r1 - 3, b1 - 3, r1 + 100, b1 + 100, steps=25, delay=0.03, record_cb=record_frame)
    time.sleep(0.2)
    l2, t2, r2, b2 = get_window_rect(out_hwnd)
    print(f"[ACTION 2: Bottom-Right Corner Drag] New size: {r2-l2}x{b2-t2} (Pinned top-left delta: dx={l2-l1}, dy={t2-t1})")
    for _ in range(8): record_frame(); time.sleep(0.03)

    # 3. Drag Bottom-Right corner inward (shrink)
    real_drag(r2 - 3, b2 - 3, r2 - 160, b2 - 160, steps=25, delay=0.03, record_cb=record_frame)
    time.sleep(0.2)
    l3, t3, r3, b3 = get_window_rect(out_hwnd)
    print(f"[ACTION 3: Corner Shrink] New size: {r3-l3}x{b3-t3}")
    for _ in range(8): record_frame(); time.sleep(0.03)

    # 4. Drag Bottom edge outward
    real_drag(l3 + (r3 - l3) // 2, b3 - 2, l3 + (r3 - l3) // 2, b3 + 70, steps=20, delay=0.03, record_cb=record_frame)
    time.sleep(0.2)
    l4, t4, r4, b4 = get_window_rect(out_hwnd)
    print(f"[ACTION 4: Bottom Edge Drag] New size: {r4-l4}x{b4-t4}")
    for _ in range(12): record_frame(); time.sleep(0.03)

    writer.release()
    print(f"[PASS] Video demo saved to {video_path} ({frames_captured} frames)")

    snap_path = os.path.join(out_dir, "output_window_resized.png")
    l, t, r, b = get_window_rect(out_hwnd)
    if r > l and b > t:
        snap = ImageGrab.grab(bbox=(max(0, l), max(0, t), min(screen_w, r), min(screen_h, b)))
        snap.save(snap_path)
        print(f"[PASS] Saved resized screenshot to {snap_path}")
    return 0

if __name__ == "__main__":
    import sys
    sys.exit(main())