# Duwn Mirror 1.0.0 — Release Notes

Duwn Mirror is a high-performance AirPlay receiver for Windows designed for livestreaming, content creation, and real-time screen mirroring from iOS devices.

## Highlights

- **Dual-Mode Mirroring**:
  - **Wireless AirPlay**: Low-latency screen mirroring over standard Wi-Fi.
  - **Wired USB Connection**: Zero-configuration mirroring over Apple Mobile Device Ethernet with automatic interface discovery.
- **Hardware-Accelerated Video**:
  - Hardware-accelerated H.264 and HEVC (H.265) video decoding via Windows Media Foundation.
  - Direct3D 11 NV12 zero-copy pipeline delivering stable 60 FPS performance.
- **Broadcast & Capture Architecture**:
  - **Dedicated Output Window**: Borderless, titlebar-free canvas optimized for window capture in OBS Studio and TikTok Live Studio.
  - **Preview Window**: High-DPI preview window with aspect-ratio locking and automatic orientation adjustment.
- **Synchronized Low-Latency Audio**:
  - Direct WASAPI Shared event-driven audio renderer.
  - Adaptive buffer management with drift compensation for clean, continuous audio.
- **Orientation & Geometry**:
  - Instant portrait and landscape adaptation without window distortion or stale frames.
- **Bilingual Interface**:
  - Full English and Vietnamese localization across all settings and error states.

## System Requirements

- **Operating System**: Windows 10 (version 1809 or later) or Windows 11 (64-bit).
- **Sender Device**: Compatible iPhone or iPad running iOS 12 or newer.
- **Network (Wireless)**: 5 GHz Wi-Fi recommended for optimal streaming stability.
- **Connection (Wired)**: USB Lightning/USB-C cable with Apple Mobile Device Support (iTunes or Apple Devices app installed).
- **HEVC Support**: Microsoft HEVC Video Extensions from Microsoft Store required for H.265 decoding on wired connections.

## Known Limitations

- **Physical Latency**: Total latency depends on sender device processing, Wi-Fi interference, network congestion, and monitor refresh rate.
- **Wireless Conditions**: Wireless stream stability is directly influenced by local 2.4 GHz / 5 GHz Wi-Fi environment and channel congestion.
- **Wired HEVC Dependency**: Wired HEVC mode requires an active Apple USB network interface and Windows HEVC Video Extensions. If HEVC is unavailable on the system, standard H.264 is used.
- **SmartScreen Warning**: This release is currently unsigned (`UNSIGNED_RC`); Windows SmartScreen or User Account Control may present an "Unknown Publisher" warning upon installation.
