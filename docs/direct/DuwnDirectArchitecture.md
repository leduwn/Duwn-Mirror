# Duwn Direct Architecture — Phase 1: Protocol & Capability Foundation

## 1. Purpose

Duwn Direct is an upcoming custom, low-latency streaming protocol designed to bypass standard AirPlay mirroring limitations (RTSP overhead, fixed protocol constraints) when a compatible Duwn sender client is available.

Phase 1 establishes the formal capability, protocol versioning, and session negotiation model. It intentionally introduces no media streaming, no capture, no encoding, and no transport implementation, leaving existing AirPlay streaming completely intact.

## 2. Session Model

A Direct session connects a sender capture/encoder source to a receiver decoder/render display. The session state machine is driven entirely by declarative capabilities rather than device models or heuristic identifiers:

```
Sender Capabilities:
  + CaptureCapabilities   (width, height, FPS, native source resolution)
  + EncoderCapabilities   (codecs, max bitrate, HW acceleration, intra-refresh)
  + TransportCapabilities (supported datagram / transport mechanisms, MTU)

Receiver Capabilities:
  + ReceiverCapabilities  (decoders, display canvas, HW decode, zero-copy)

Session Inputs:
  + SessionPreferences    (quality policy, requested FPS, preferred codec)
  + RuntimeNetworkMetrics (bandwidth, RTT, packet loss, jitter)

                     ||
                     \/
       [DirectNegotiator::Negotiate]
                     ||
                     \/
                SessionPlan
  (selected codec, transport, FPS, separate resolution tiers, latency mode)
```

### Telemetry Isolation Rule
Device model identifiers (e.g., `iPhone15,2`) and OS version strings exist purely as telemetry fields (`device_model_telemetry`, `os_version_telemetry`). The negotiation engine is strictly forbidden from branching on device models or OS versions.

## 3. Capability-Driven Negotiation

The `SessionPlan` is derived strictly from the mathematical intersection of capabilities:

1. **Protocol Version Compatibility**: The receiver and sender must share the same `protocol_major`. Minor version differences are forward-compatible (unknown optional capabilities are ignored).
2. **Codec Intersection**: Only codecs supported by both the sender hardware encoder and the receiver hardware decoder are considered. Priority defaults to AV1 -> HEVC -> H.264 unless user preferences specify otherwise.
3. **Transport Intersection**: Supported transports are intersected (e.g., `DirectDatagram` over custom UDP, `DirectQuic`, or `DirectTcp`).
4. **Resolution Intersection**:
   - `width  = min(sender_capture.max_width, sender_encoder.max_width, receiver.max_width)`
   - `height = min(sender_capture.max_height, sender_encoder.max_height, receiver.max_height)`
   - Future devices exceeding 1920x1080 (e.g. 2.5K, 4K) are never artificially capped if both endpoints support higher resolutions.
5. **Framerate Intersection**:
   - `fps = min(sender_capture.max_fps, sender_encoder.max_fps, receiver.max_fps, prefs.requested_fps)`

## 4. Quality vs. Latency Principle

Direct Mode defines three distinct quality policy targets:

- **LOWEST_LATENCY**: Aggressively bounds resolution (typically 1080p or lower) and encoding parameters to achieve minimum end-to-end glass-to-glass delay (<16 ms target).
- **BALANCED**: Minimizes latency while preventing unacceptable compression blur on high-motion content. Uses native capture bounds up to capability limits with adaptive bitrates.
- **HIGH_QUALITY**: Preserves maximum source fidelity and color accuracy, prioritizing sharpness and visual clarity.

### Strict Resolution Separation
To prevent scaling distortion and telemetry ambiguity, four resolution concepts are tracked separately and never conflated:
1. `requested_quality`: The user-specified policy enum.
2. `actual_source_resolution`: Physical dimensions of the captured screen.
3. `encoded_resolution`: Dimensions of the compressed bitstream transmitted over the wire.
4. `render_resolution`: Dimensions of the receiver swapchain / canvas surface.

## 5. Buffering Invariant

**Invariant**: Direct video must never accumulate an arbitrary FIFO queue.

The architecture enforces:
- Exactly **0 or 1** pending freshest frame before presentation.
- When a new decoded frame arrives, any older unrendered pending frame is immediately superseded (dropped/replaced).
- No multi-frame buffering queue may grow under packet burst or network jitter.

## 6. Fallback Behavior

If Direct negotiation cannot achieve a valid `SessionPlan`:
- `SessionPlan::negotiated` is set to `false`.
- `SessionPlan::fallback_to_airplay` is set to `true`.
- Detailed `failure_reason` is logged.
- The connection falls back transparently to standard AirPlay mirroring.

Failure triggers requiring fallback:
- Protocol major version mismatch.
- Empty codec intersection (no shared video codec).
- Empty transport intersection (no shared transport protocol).
- Zero or invalid resolution capability.

## 7. Future Transport Boundary

Phase 1 defines `DirectTransportType` (`DirectDatagram`, `DirectQuic`, `DirectTcp`). In Phase 2, concrete socket reader and sender interfaces will attach to this boundary without altering session planning or capability negotiation logic.
