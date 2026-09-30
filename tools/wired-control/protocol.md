# DUWN Mirror — Wired USB Remote Control Protocol (v1.0)

This document defines the local inter-process communication (IPC) protocol between the DUWN Mirror C++ core and the out-of-process wired control bridge sidecar (`control_bridge.py`).

## 1. Architectural & Licensing Boundary

- **Licensing Separation:** The bridge uses `pymobiledevice3` (GPL-3.0-or-later) and executes strictly out-of-process as an isolated sidecar. No GPL code or symbols are linked or compiled into the proprietary DUWN Mirror C++ executable.
- **Transport Security:** Communication occurs strictly over local loopback (`127.0.0.1`) TCP or a Windows Named Pipe (`\\.\pipe\duwn-wired-control`). No network sockets are bound to external or public interfaces (`0.0.0.0` is strictly forbidden).

## 2. Transport & Framing

- **Transport:** Localhost TCP stream (`127.0.0.1:58901` default, or dynamic port assigned at startup).
- **Framing:** Line-delimited UTF-8 JSON. Every message (request and response) is terminated by a single newline character (`\n`).
- **Persistence:** A single TCP connection is maintained across the entire lifetime of the control session. Connections must NOT be opened and closed per input event.

## 3. Startup Handshake

When `control_bridge.py` establishes the Apple Remote Service Discovery (RSD) userspace tunnel and connects to the CoreDevice HID services (`IndigoHIDService` and `UniversalHIDServiceService`), it emits a single JSON line to standard output:

```json
{"event": "ready", "port": 58901, "transport": "tcp_loopback", "host": "127.0.0.1"}
```

DUWN Mirror monitors this handshake before transitioning the control state machine to `Ready`.

## 4. Message Definitions

### 4.1 Hardware Buttons (`button`)

Dispatches physical device button actions through `IndigoHIDService` (Consumer Usage Page `0x0C`).

**Request:**
```json
{
  "type": "button",
  "name": "home" | "lock" | "power" | "volume-up" | "volume-down" | "mute" | "siri",
  "state": "press" | "down" | "up",
  "id": 1
}
```

- `"press"`: Dispatches a DOWN event, sleeps for the button's hold duration (50ms for volume/home/mute; 500ms for lock/power; 1000ms for siri), then dispatches an UP event.
- `"down"`: Dispatches a persistent DOWN event (for hold gestures).
- `"up"`: Dispatches an UP event to release a held button.

### 4.2 Single Tap (`tap`)

Dispatches an instantaneous touch contact and release at normalized screen coordinates.

**Request:**
```json
{
  "type": "tap",
  "x": 32768,
  "y": 32768,
  "id": 2
}
```

- `x`, `y`: Screen-normalized coordinates (`0..65535`). Top-left is `(0, 0)`, bottom-right is `(65535, 65535)`.

### 4.3 Direct Contact & Release (`contact`, `release`)

Dispatches discrete touch lifecycle events for interactive mouse dragging or pointer tracking.

**Contact Request:**
```json
{
  "type": "contact",
  "x": 32768,
  "y": 32768,
  "id": 3
}
```

**Release Request:**
```json
{
  "type": "release",
  "x": 32768,
  "y": 32768,
  "id": 4
}
```

### 4.4 Interpolated Drag (`drag`)

Dispatches a stream of touch contact points interpolated between start and end coordinates over a specified duration, terminated by a release.

**Request:**
```json
{
  "type": "drag",
  "x1": 32768,
  "y1": 55000,
  "x2": 32768,
  "y2": 15000,
  "duration_ms": 250,
  "id": 5
}
```

### 4.5 Standard Swipes (`swipe`)

Executes common system gestures:

**Request:**
```json
{
  "type": "swipe",
  "direction": "up" | "down" | "left" | "right",
  "id": 6
}
```

- `"up"`: Swipes up from bottom (Home / App Switcher / Unlock).
- `"down"`: Swipes down from top (Control Center / Notifications).
- `"left"` / `"right"`: Swipes horizontally across home screen pages.

### 4.6 Status & Latency Metrics (`status`)

Retrieves execution metrics and hardware status.

**Request:**
```json
{
  "type": "status",
  "id": 7
}
```

**Response:**
```json
{
  "status": "ok",
  "type": "status",
  "id": 7,
  "metrics": {
    "events_sent": 142,
    "events_failed": 0,
    "latency_avg_ms": 2.45,
    "latency_p50_ms": 1.82,
    "latency_p95_ms": 4.10,
    "latency_p99_ms": 7.30
  },
  "display": { ... }
}
```

### 4.7 Display Information (`display_info`)

Queries display dimensions, physical orientation, and rotation state from `com.apple.coredevice.deviceinfo`.

**Request:**
```json
{
  "type": "display_info",
  "id": 8
}
```

### 4.8 Ping (`ping`)

Round-trip latency probe.

**Request:**
```json
{
  "type": "ping",
  "id": 9
}
```

**Response:**
```json
{
  "status": "ok",
  "type": "pong",
  "id": 9,
  "duration_ms": 0.0
}
```

### 4.9 Clean Shutdown (`quit`)

Gracefully releases CoreDevice services and shuts down the bridge process.

**Request:**
```json
{
  "type": "quit"
}
```

## 5. Standard Response Format

Every action returns a response object with timing diagnostics:

```json
{
  "status": "ok",
  "id": 1,
  "t_recv": 128456.789123,
  "t_send_start": 128456.789150,
  "t_send_end": 128456.791520,
  "duration_ms": 2.37
}
```

In the event of an error:

```json
{
  "status": "error",
  "id": 1,
  "error": "Device disconnected during HID write"
}
```
