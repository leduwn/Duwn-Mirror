#!/usr/bin/env python3
"""
DUWN Mirror — Wired USB Remote Control Bridge
Prototype sidecar providing persistent CoreDevice HID input dispatch.

IMPORTANT LICENSING NOTE:
This script uses pymobiledevice3 (GPL-3.0-or-later) and runs strictly
out-of-process as a development bridge over local loopback IPC (127.0.0.1 TCP / Named Pipe).
It must NOT be statically linked, dynamically linked, or copied into proprietary DUWN C++ binaries.
"""

import argparse
import asyncio
import json
import logging
import os
import sys
import time
from typing import Any, Dict, Optional

# pymobiledevice3 imports
try:
    from pymobiledevice3.remote import userspace_tunnel
    from pymobiledevice3.remote.core_device.device_info import DeviceInfoService
    from pymobiledevice3.remote.core_device.hid_service import (
        UniversalHIDServiceService,
        IndigoHIDService,
        HID_BUTTON_STATE_DOWN,
        HID_BUTTON_STATE_UP,
        TOUCHSCREEN_STATE_CONTACT,
        TOUCHSCREEN_STATE_RELEASE,
        DIGITIZER_SURFACE_MAIN_TOUCHSCREEN,
    )
except ImportError as e:
    sys.stderr.write(f"[control_bridge] Failed to import pymobiledevice3: {e}\n")
    sys.exit(1)

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[logging.StreamHandler(sys.stderr)]
)
logger = logging.getLogger("control_bridge")

# HID Usage Mappings for Indigo Hardware Buttons (Consumer Usage Page 0x0C)
BUTTON_MAP = {
    "home": (0x0C, 0x40, 0.05),          # Consumer / Menu
    "lock": (0x0C, 0x30, 0.5),           # Consumer / Power (sleep)
    "power": (0x0C, 0x30, 0.5),          # Consumer / Power (sleep)
    "volume-up": (0x0C, 0xE9, 0.05),     # Consumer / Volume Increment
    "volume_up": (0x0C, 0xE9, 0.05),
    "volume-down": (0x0C, 0xEA, 0.05),   # Consumer / Volume Decrement
    "volume_down": (0x0C, 0xEA, 0.05),
    "mute": (0x0C, 0xE2, 0.05),          # Consumer / Mute
    "siri": (0x0C, 0xCF, 1.0),           # Consumer / Voice Command
}

class ControlSession:
    def __init__(self, rsd_service):
        self.rsd = rsd_service
        self.indigo: Optional[IndigoHIDService] = None
        self.uhs: Optional[UniversalHIDServiceService] = None
        self.device_info: Optional[DeviceInfoService] = None
        self.display_info_cache: Optional[Dict[str, Any]] = None
        self.events_sent = 0
        self.events_failed = 0
        self.latencies_ms = []

    async def initialize(self):
        logger.info("Initializing CoreDevice HID services...")
        self.indigo = IndigoHIDService(self.rsd)
        await self.indigo.connect()

        self.uhs = UniversalHIDServiceService(self.rsd)
        await self.uhs.connect()

        self.device_info = DeviceInfoService(self.rsd)
        try:
            self.display_info_cache = await self.device_info.get_display_info()
            logger.info(f"Display info cached: {self.display_info_cache.get('orientation')}")
        except Exception as e:
            logger.warning(f"Failed to fetch initial display info: {e}")

        logger.info("CoreDevice Indigo + UHS services ready.")

    async def handle_button(self, name: str, state: str) -> None:
        key = name.lower()
        if key not in BUTTON_MAP:
            raise ValueError(f"Unknown button name: {name}")

        usage_page, usage_code, default_hold = BUTTON_MAP[key]
        state = state.lower()

        if state == "press":
            await self.indigo.send_button(usage_page, usage_code, HID_BUTTON_STATE_DOWN)
            await asyncio.sleep(default_hold)
            await self.indigo.send_button(usage_page, usage_code, HID_BUTTON_STATE_UP)
        elif state == "down":
            await self.indigo.send_button(usage_page, usage_code, HID_BUTTON_STATE_DOWN)
        elif state == "up":
            await self.indigo.send_button(usage_page, usage_code, HID_BUTTON_STATE_UP)
        else:
            raise ValueError(f"Unknown button state: {state}")

    async def handle_tap(self, x: int, y: int) -> None:
        # Clamp coordinates to UInt16
        cx = max(0, min(65535, int(x)))
        cy = max(0, min(65535, int(y)))
        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_CONTACT, cx, cy)
        await asyncio.sleep(0.04)
        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_RELEASE, cx, cy)

    async def handle_contact(self, x: int, y: int) -> None:
        cx = max(0, min(65535, int(x)))
        cy = max(0, min(65535, int(y)))
        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_CONTACT, cx, cy)

    async def handle_release(self, x: int, y: int) -> None:
        cx = max(0, min(65535, int(x)))
        cy = max(0, min(65535, int(y)))
        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_RELEASE, cx, cy)

    async def handle_drag(self, x1: int, y1: int, x2: int, y2: int, duration_ms: int = 250) -> None:
        cx1 = max(0, min(65535, int(x1)))
        cy1 = max(0, min(65535, int(y1)))
        cx2 = max(0, min(65535, int(x2)))
        cy2 = max(0, min(65535, int(y2)))
        duration = max(50, duration_ms) / 1000.0
        steps = max(int(duration * 60), 6)
        interval = duration / steps

        for i in range(steps):
            t = i / float(steps)
            curr_x = round(cx1 + (cx2 - cx1) * t)
            curr_y = round(cy1 + (cy2 - cy1) * t)
            await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_CONTACT, curr_x, curr_y)
            await asyncio.sleep(interval)

        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_CONTACT, cx2, cy2)
        await asyncio.sleep(0.02)
        await self.uhs.send_touchscreen(TOUCHSCREEN_STATE_RELEASE, cx2, cy2)

    async def handle_swipe(self, direction: str) -> None:
        d = direction.lower()
        if d == "up":
            # Unlock / go home swipe
            await self.handle_drag(32768, 62000, 32768, 30000, duration_ms=200)
        elif d == "down":
            # Control center / notifications
            await self.handle_drag(32768, 4000, 32768, 40000, duration_ms=250)
        elif d == "left":
            # Page left
            await self.handle_drag(55000, 32768, 10000, 32768, duration_ms=200)
        elif d == "right":
            # Page right
            await self.handle_drag(10000, 32768, 55000, 32768, duration_ms=200)
        else:
            raise ValueError(f"Unknown swipe direction: {direction}")

    async def get_metrics(self) -> Dict[str, Any]:
        count = len(self.latencies_ms)
        avg_lat = round(sum(self.latencies_ms) / count, 2) if count > 0 else 0.0
        sorted_lat = sorted(self.latencies_ms)
        p50 = sorted_lat[int(count * 0.50)] if count > 0 else 0.0
        p95 = sorted_lat[int(count * 0.95)] if count > 0 else 0.0
        p99 = sorted_lat[int(count * 0.99)] if count > 0 else 0.0

        return {
            "events_sent": self.events_sent,
            "events_failed": self.events_failed,
            "latency_avg_ms": avg_lat,
            "latency_p50_ms": p50,
            "latency_p95_ms": p95,
            "latency_p99_ms": p99,
        }

async def process_command(session: ControlSession, cmd: Dict[str, Any]) -> Dict[str, Any]:
    cmd_type = cmd.get("type", "")
    t_recv = time.perf_counter()
    t_send_start = t_recv

    if cmd_type == "ping":
        t_send_end = time.perf_counter()
        return {
            "status": "ok",
            "type": "pong",
            "id": cmd.get("id"),
            "duration_ms": 0.0,
        }

    if cmd_type == "status":
        metrics = await session.get_metrics()
        return {
            "status": "ok",
            "type": "status",
            "id": cmd.get("id"),
            "metrics": metrics,
            "display": session.display_info_cache,
        }

    if cmd_type == "display_info":
        info = await session.device_info.get_display_info()
        session.display_info_cache = info
        return {
            "status": "ok",
            "id": cmd.get("id"),
            "display": info,
        }

    t_send_start = time.perf_counter()
    try:
        if cmd_type == "button":
            await session.handle_button(cmd.get("name", ""), cmd.get("state", "press"))
        elif cmd_type == "tap":
            await session.handle_tap(cmd.get("x", 32768), cmd.get("y", 32768))
        elif cmd_type == "contact":
            await session.handle_contact(cmd.get("x", 32768), cmd.get("y", 32768))
        elif cmd_type == "release":
            await session.handle_release(cmd.get("x", 32768), cmd.get("y", 32768))
        elif cmd_type == "drag":
            await session.handle_drag(
                cmd.get("x1", 32768),
                cmd.get("y1", 32768),
                cmd.get("x2", 32768),
                cmd.get("y2", 32768),
                cmd.get("duration_ms", 250),
            )
        elif cmd_type == "swipe":
            await session.handle_swipe(cmd.get("direction", "up"))
        else:
            raise ValueError(f"Unknown command type: {cmd_type}")

        t_send_end = time.perf_counter()
        duration_ms = round((t_send_end - t_send_start) * 1000, 2)
        session.events_sent += 1
        session.latencies_ms.append(duration_ms)
        if len(session.latencies_ms) > 1000:
            session.latencies_ms.pop(0)

        return {
            "status": "ok",
            "id": cmd.get("id"),
            "t_recv": round(t_recv, 6),
            "t_send_start": round(t_send_start, 6),
            "t_send_end": round(t_send_end, 6),
            "duration_ms": duration_ms,
        }
    except Exception as e:
        session.events_failed += 1
        logger.error(f"Error handling command {cmd_type}: {e}")
        return {
            "status": "error",
            "id": cmd.get("id"),
            "error": str(e),
        }

async def run_server(port: int = 58901):
    logger.info("Establishing userspace RSD tunnel...")
    async with await userspace_tunnel.establish_userspace_rsd() as rsd:
        session = ControlSession(rsd)
        await session.initialize()

        async def client_connected_cb(reader: asyncio.StreamReader, writer: asyncio.StreamWriter):
            peer = writer.get_extra_info("peername")
            logger.info(f"Client connected: {peer}")

            try:
                while True:
                    line = await reader.readline()
                    if not line:
                        break

                    raw = line.decode("utf-8").strip()
                    if not raw:
                        continue

                    try:
                        cmd = json.loads(raw)
                    except json.JSONDecodeError as err:
                        err_resp = {"status": "error", "error": f"Invalid JSON: {err}"}
                        writer.write((json.dumps(err_resp) + "\n").encode("utf-8"))
                        await writer.drain()
                        continue

                    if cmd.get("type") == "quit":
                        logger.info("Received quit command from client.")
                        resp = {"status": "ok", "message": "shutting down"}
                        writer.write((json.dumps(resp) + "\n").encode("utf-8"))
                        await writer.drain()
                        break

                    resp = await process_command(session, cmd)
                    writer.write((json.dumps(resp) + "\n").encode("utf-8"))
                    await writer.drain()
            except Exception as e:
                logger.error(f"Exception in client connection: {e}")
            finally:
                logger.info(f"Client disconnected: {peer}")
                writer.close()
                await writer.wait_closed()

        server = await asyncio.start_server(client_connected_cb, "127.0.0.1", port)
        actual_port = server.sockets[0].getsockname()[1]
        logger.info(f"Wired Control Bridge listening on 127.0.0.1:{actual_port}")

        # Emit JSON handshake on stdout for the parent C++ process
        ready_payload = {
            "event": "ready",
            "port": actual_port,
            "transport": "tcp_loopback",
            "host": "127.0.0.1",
        }
        sys.stdout.write(json.dumps(ready_payload) + "\n")
        sys.stdout.flush()

        async with server:
            await server.serve_forever()

def main():
    parser = argparse.ArgumentParser(description="DUWN Mirror Wired USB Remote Control Bridge")
    parser.add_argument("--port", type=int, default=58901, help="TCP port to bind on 127.0.0.1 (default: 58901, use 0 for dynamic)")
    args = parser.parse_args()

    try:
        asyncio.run(run_server(args.port))
    except (KeyboardInterrupt, asyncio.CancelledError):
        logger.info("Wired Control Bridge stopped.")
    except Exception as e:
        logger.fatal(f"Fatal error in Wired Control Bridge: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
