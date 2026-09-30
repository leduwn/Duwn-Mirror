# Wired USB Remote Control Bridge Prototype

This directory contains the out-of-process prototype bridge for the **Device Control** capability in DUWN Mirror's Wired USB mode.

## Architecture

```
DUWN Mirror (C++ GUI)
      │
      ▼  Localhost TCP (127.0.0.1:58901) / Line-delimited JSON
control_bridge.py (Python sidecar)
      │
      ▼  Userspace RSD Tunnel over Usbmux (Apple Remote Service Discovery)
CoreDevice Services (RemoteXPC)
   ├── com.apple.coredevice.hid.indigo           (Hardware buttons)
   ├── com.apple.coredevice.hid.universalhidservice (Touchscreen digitizer)
   └── com.apple.coredevice.deviceinfo          (Display & orientation info)
      │
      ▼
Physical iOS Device (iPhone / iPad)
```

## Licensing Notice

- The bridge uses `pymobiledevice3`, which is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later).
- To preserve the proprietary license of DUWN Mirror, this bridge is strictly isolated as an external subprocess.
- No source code from `pymobiledevice3` is copied into or compiled with the DUWN Mirror C++ binaries.

## Prerequisites

- Python 3.10+
- Virtual environment with `pymobiledevice3` installed:
  ```powershell
  ..\wired-probe\.venv\Scripts\python.exe control_bridge.py --port 58901
  ```
- Physical iOS device connected via USB with trusted pairing and Developer Mode enabled.

## Protocol

Refer to [protocol.md](protocol.md) for full JSON message schemas.
