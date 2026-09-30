#!/usr/bin/env python3
"""
Test client for Wired USB Remote Control Bridge.
Connects over 127.0.0.1:58901 and issues sample hardware button and touch events.
"""

import asyncio
import json
import time

async def main():
    print("Connecting to Wired Control Bridge at 127.0.0.1:58901...")
    reader, writer = await asyncio.open_connection("127.0.0.1", 58901)
    print("Connected successfully!")

    async def send_cmd(cmd_dict):
        t0 = time.perf_counter()
        req_str = json.dumps(cmd_dict) + "\n"
        writer.write(req_str.encode("utf-8"))
        await writer.drain()

        line = await reader.readline()
        t1 = time.perf_counter()
        rtt_ms = round((t1 - t0) * 1000, 2)
        resp = json.loads(line.decode("utf-8").strip())
        print(f"[{cmd_dict.get('type')}] RTT: {rtt_ms}ms -> {resp}")
        return resp

    # 1. Ping
    await send_cmd({"type": "ping", "id": 1})

    # 2. Hardware buttons
    print("\n--- Testing Hardware Buttons ---")
    await send_cmd({"type": "button", "name": "volume-up", "state": "press", "id": 2})
    await asyncio.sleep(0.3)
    await send_cmd({"type": "button", "name": "volume-down", "state": "press", "id": 3})
    await asyncio.sleep(0.3)
    await send_cmd({"type": "button", "name": "home", "state": "press", "id": 4})
    await asyncio.sleep(0.5)

    # 3. Touch gestures
    print("\n--- Testing Touch Gestures ---")
    # Tap center
    await send_cmd({"type": "tap", "x": 32768, "y": 32768, "id": 5})
    await asyncio.sleep(0.3)

    # Swipe up (Home / Unlock)
    await send_cmd({"type": "swipe", "direction": "up", "id": 6})
    await asyncio.sleep(0.5)

    # Drag
    await send_cmd({
        "type": "drag",
        "x1": 32768,
        "y1": 50000,
        "x2": 32768,
        "y2": 35000,
        "duration_ms": 150,
        "id": 7
    })
    await asyncio.sleep(0.3)

    # 4. Status & Metrics
    print("\n--- Querying Status & Metrics ---")
    await send_cmd({"type": "status", "id": 8})

    print("\nClosing connection.")
    writer.close()
    await writer.wait_closed()
    print("Done.")

if __name__ == "__main__":
    asyncio.run(main())
