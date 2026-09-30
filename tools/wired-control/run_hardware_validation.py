#!/usr/bin/env python3
"""
DUWN Mirror — Physical Hardware Validation Script
Runs the control_bridge, waits for the handshake, connects, and sends:
1. Ping latency test
2. Home button
3. Volume Up & Volume Down buttons
4. Lock button
5. Tap gesture
6. Swipe & Drag gestures
7. Queries bridge telemetry and reports precise latency metrics.
"""

import asyncio
import json
import subprocess
import sys
import time

async def main():
    print("=" * 60)
    print("DUWN Mirror — Wired USB Physical Hardware Control Validation")
    print("Target: Physical iPhone XS (iOS 18.5)")
    print("=" * 60)

    # 1. Locate python in wired-probe venv or current interpreter
    import os
    venv_py = os.path.abspath("tools/wired-probe/.venv/Scripts/python.exe")
    python_bin = venv_py if os.path.exists(venv_py) else sys.executable

    cmd = [python_bin, "tools/wired-control/control_bridge.py", "--port", "0"]
    print(f"[1] Launching sidecar: {' '.join(cmd)}")
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1
    )

    port = None
    t0_start = time.perf_counter()
    while True:
        line = proc.stdout.readline()
        if not line:
            if proc.poll() is not None:
                err = proc.stderr.read()
                print(f"[!] Bridge exited prematurely with code {proc.returncode}: {err}")
                return
            continue

        raw = line.strip()
        if raw.startswith("{") and "ready" in raw:
            try:
                data = json.loads(raw)
                if data.get("event") == "ready":
                    port = data.get("port")
                    print(f"[+] Bridge handshake received! Port: {port} (in {round((time.perf_counter() - t0_start)*1000, 1)}ms)")
                    break
            except Exception:
                pass

    if not port:
        print("[!] Failed to obtain port from handshake.")
        proc.terminate()
        return

    # 2. Connect client
    print(f"\n[2] Connecting TCP client to 127.0.0.1:{port}...")
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    print("[+] TCP client connected successfully (TCP_NODELAY).")

    results = []

    async def execute_cmd(label, cmd_dict):
        t0 = time.perf_counter()
        req = json.dumps(cmd_dict) + "\n"
        writer.write(req.encode("utf-8"))
        await writer.drain()

        resp_line = await reader.readline()
        t1 = time.perf_counter()
        rtt_ms = round((t1 - t0) * 1000, 2)
        resp = json.loads(resp_line.decode("utf-8").strip())
        results.append({
            "label": label,
            "cmd": cmd_dict.get("type"),
            "rtt_ms": rtt_ms,
            "status": resp.get("status"),
            "duration_ms": resp.get("duration_ms", 0.0),
        })
        print(f"    - {label:<24} | RTT: {rtt_ms:>6.2f}ms | Bridge duration: {resp.get('duration_ms', 0.0):>6.2f}ms | Status: {resp.get('status')}")
        return resp

    print("\n[3] Testing IPC Ping Latency (10 iterations)...")
    ping_latencies = []
    for i in range(10):
        t0 = time.perf_counter()
        writer.write((json.dumps({"type": "ping", "id": i + 1}) + "\n").encode("utf-8"))
        await writer.drain()
        line = await reader.readline()
        t1 = time.perf_counter()
        lat = (t1 - t0) * 1000.0
        ping_latencies.append(lat)

    p_avg = sum(ping_latencies) / len(ping_latencies)
    p_min = min(ping_latencies)
    p_max = max(ping_latencies)
    print(f"    Ping RTT: min={p_min:.2f}ms, max={p_max:.2f}ms, avg={p_avg:.2f}ms")

    print("\n[4] Testing Physical Hardware Buttons...")
    await execute_cmd("Volume Up", {"type": "button", "name": "volume-up", "state": "press", "id": 101})
    await asyncio.sleep(0.3)
    await execute_cmd("Volume Down", {"type": "button", "name": "volume-down", "state": "press", "id": 102})
    await asyncio.sleep(0.3)
    await execute_cmd("Home Button", {"type": "button", "name": "home", "state": "press", "id": 103})
    await asyncio.sleep(0.4)
    await execute_cmd("Lock / Wake Button", {"type": "button", "name": "lock", "state": "press", "id": 104})
    await asyncio.sleep(0.4)
    await execute_cmd("Wake Back Up", {"type": "button", "name": "lock", "state": "press", "id": 105})
    await asyncio.sleep(0.4)

    print("\n[5] Testing Touchscreen Gestures...")
    await execute_cmd("Center Tap", {"type": "tap", "x": 32768, "y": 32768, "id": 201})
    await asyncio.sleep(0.3)
    await execute_cmd("Swipe Up (Unlock/Home)", {"type": "swipe", "direction": "up", "id": 202})
    await asyncio.sleep(0.4)
    await execute_cmd("Drag Gesture", {
        "type": "drag",
        "x1": 32768,
        "y1": 50000,
        "x2": 32768,
        "y2": 35000,
        "duration_ms": 150,
        "id": 203
    })
    await asyncio.sleep(0.3)

    print("\n[6] Fetching Final Bridge Status & Telemetry...")
    status_resp = await execute_cmd("Status & Metrics", {"type": "status", "id": 301})

    # Close connection
    writer.write((json.dumps({"type": "quit"}) + "\n").encode("utf-8"))
    await writer.drain()
    await reader.readline()
    writer.close()
    await writer.wait_closed()
    proc.terminate()
    proc.wait(timeout=3)

    print("\n" + "=" * 60)
    print("HARDWARE VALIDATION SUMMARY REPORT")
    print("=" * 60)
    print(f"Transport: Localhost TCP (127.0.0.1:{port}) + Userspace RSD Tunnel over usbmux")
    print(f"Loopback IPC Ping Average: {p_avg:.2f} ms")
    if status_resp and "metrics" in status_resp:
        m = status_resp["metrics"]
        print(f"Events Sent:     {m.get('events_sent')}")
        print(f"Events Failed:   {m.get('events_failed')}")
        print(f"Avg Latency:     {m.get('latency_avg_ms')} ms")
        print(f"P50 Latency:     {m.get('latency_p50_ms')} ms")
        print(f"P95 Latency:     {m.get('latency_p95_ms')} ms")
        print(f"P99 Latency:     {m.get('latency_p99_ms')} ms")

    all_ok = all(r["status"] == "ok" for r in results)
    print(f"\nFinal Result: {'ALL PASS' if all_ok else 'SOME FAILED'}")

if __name__ == "__main__":
    asyncio.run(main())
