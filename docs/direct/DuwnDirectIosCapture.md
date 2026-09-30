# Duwn Direct Architecture — Phase 3: iOS Capture Backend

## 1. Scope & Objective

Phase 3 establishes the sender-side screen capture abstraction for iOS and iPadOS.
This phase focuses exclusively on frame capture:
- **Frame capture only**: Acquires raw pixel buffers and audio samples directly from the operating system.
- **NO network transport**: Transport is decoupled and handled in subsequent phases.
- **NO video encoder**: Encoding (VideoToolbox / H.264 / HEVC) is strictly decoupled from capture.
- **NO Windows modifications**: Windows receiver, AirPlay core, and existing baseline remain completely untouched.

---

## 2. Core Architecture

The architecture consists of five primary structures and a capability-driven backend hierarchy:

```
                          [ iOS / iPadOS OS Engine ]
                                     |
                                     v
                  +--------------------------------------+
                  |         DuwnCaptureBackend           |
                  |     (Abstract Capture Boundary)      |
                  +--------------------------------------+
                                     |
               +---------------------+---------------------+
               |                                           |
               v                                           v
+-------------------------------+           +-------------------------------+
|  DuwnReplayKitCaptureBackend  |           |   DuwnLegacyCaptureBackend    |
|   (Modern: RPScreenRecorder)  |           | (Compatibility: DisplayLink)  |
+-------------------------------+           +-------------------------------+
               |                                           |
               +---------------------+---------------------+
                                     |
                                     v
                   [ Fast Capture Callback (< 5 μs) ]
                                     |
                                     v
                  +--------------------------------------+
                  |         DuwnCaptureFrameSlot         |
                  |   (1-Frame Latest-Frame Invariant)   |
                  +--------------------------------------+
                                     |
                        [ Producer Outruns Consumer? ]
                               /            \
                             YES             NO
                             /                \
             [ Replace Stale Pending ]     [ Deliver to Slot ]
```

### Components

1. **`DuwnCaptureBackend`**: Abstract interface decoupling the rest of the application from Apple SDK framework headers (`ReplayKit`, `UIKit`, `CoreMedia`).
2. **`CaptureCapabilities`**: Runtime-queried screen dimensions, native pixel formats, maximum display refresh cadence, and audio capabilities.
3. **`CapturedVideoFrame`**: Immutable frame container holding:
   - `CVPixelBufferRef`: Native hardware pixel buffer (retained via CoreFoundation).
   - `visible_width`, `visible_height`: Pixel dimensions.
   - `pixel_format`: FourCC format (`NV12_VideoRange`, `NV12_FullRange`, `BGRA32`).
   - `timestamp`: Monotonic nanosecond capture timestamps.
   - `orientation`: Hardware capture orientation.
   - `frame_sequence`: Strictly monotonic 64-bit frame counter.
4. **`CapturedAudioFrame`**: Raw PCM audio buffer, sample rate, channel count, and PTS.
5. **`CaptureTimestamp`**: High-precision source timestamp and callback entry timestamp.

---

## 3. Critical Invariant: 1-Frame Latest-Frame Slot

The capture callback is invoked on system-managed high-priority threads (e.g., ReplayKit capture queue or CoreAnimation DisplayLink thread).

### Strict Callback Invariant:
The capture callback must **NEVER**:
- Perform network transmission (no socket operations).
- Perform blocking video encoding (no synchronous VideoToolbox compression).
- Write to disk or flash storage.
- Acquire long locks or wait on synchronization primitives.

### Slot Policy:
- The callback hands the frame directly into `DuwnCaptureFrameSlot` and returns immediately.
- Execution time is bounded to $< 5\ \mu\text{s}$.
- If the downstream consumer (encoder/sender) has not drained the previously pending frame:
  - The older, unconsumed frame is **immediately replaced** by the newest frame.
  - The frame replacement counter (`frame_replacement_count`) is incremented.
  - Memory depth is strictly bounded to at most **1 pending frame**.
  - No frame queue accumulation is permitted under any condition.

---

## 4. Native YUV & Zero-Copy Policy

Modern ReplayKit screen capture (`RPScreenRecorder`) delivers frames in biplanar YUV format (`kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`, `'420v'`).

- **Zero Pixel Conversion**: `CapturedVideoFrame` takes direct ownership of the incoming `CVPixelBufferRef` by calling `CVPixelBufferRetain`.
- It does **not** convert YUV to RGB or reallocate memory.
- Downstream hardware encoders (VideoToolbox) consume the native biplanar YUV buffer directly with zero copy.

---

## 5. Capability-Driven Backend Selection

The capture backend is selected purely at runtime through capability queries, with **zero device-model branching**:

```cpp
// PROHIBITED:
// if (device == "iPhone15,2") { ... }

// ENFORCED:
if (DuwnReplayKitCaptureBackend::IsAvailable()) {
    return std::make_unique<DuwnReplayKitCaptureBackend>();
} else if (DuwnLegacyCaptureBackend::IsAvailable()) {
    return std::make_unique<DuwnLegacyCaptureBackend>();
}
```

- **Modern ReplayKit Backend**: Activated when `RPScreenRecorder.isAvailable` is true (iOS 11+). Supports in-app system audio, microphone capture, native YUV NV12, and ProMotion refresh cadences (up to 120 FPS).
- **Legacy DisplayLink Backend**: Fallback path using `CADisplayLink` paired with `UIGraphicsImageRenderer` / `drawViewHierarchyInRect` into a pre-allocated `CVPixelBufferPool` (BGRA32).

---

## 6. Instrumentation & Metrics

`DuwnCaptureFrameSlot` maintains real-time operational telemetry:

1. **Callback Interval (`last_callback_interval_ns`)**: Time between successive capture callbacks, measuring producer pacing and frame drop jitter.
2. **Delivery Delay (`DeliveryDelayNs`)**: Difference between hardware presentation timestamp (`CMSampleBufferGetPresentationTimeStamp`) and callback entry time (`mach_absolute_time`).
   - *IMPORTANT*: This measures capture-to-callback delivery timing inside iOS. It is **NOT** physical glass-to-glass latency.
3. **Frame Replacement Count (`frame_replacement_count`)**: Number of times a newer frame arrived before the previous frame was consumed.

---

## 7. Build & Environment Note

- **Target Platform**: iOS 11.0+ / iPadOS 11.0+.
- **Host Platform**: Windows 11 (MSVC environment).
- Because no macOS / Xcode toolchain is present in this Windows build environment, compilation and testing of the iOS objective-C++ translation units cannot be executed locally.
- Per project rules, this status is accurately reported as `BUILD_NOT_RUN` without fabricating iOS build results.
