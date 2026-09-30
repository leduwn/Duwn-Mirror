# Media Core Phase 5 — Forwarding Sink `sync=false` Physical A/B

Date: 2026-09-21

## Controlled profile

- Profile: `LiveSinkAsync`
- UxPlay `-vsync` policy unchanged
- Only forwarding change: `sync=false` on both video and audio `udpsink`
- `async` remained at the GStreamer default; `async=false` was not conflated with this test
- Source remained full quality at 2560×1184/60 and L16 44.1 kHz stereo

## Abort evidence

The profile crossed its safety threshold within seconds:

- Video forwarding burst: 1,000–1,480 RTP packets/s
- Video queue overflow counter increased from 7 to 32
- Audio RTP stopped for 3.356 seconds
- Audio real-underrun total increased from 4 to 20
- Audio underrun frames increased from 1,309 to 4,082
- Controlled audio backlog drops reached 4,505 frames
- One `AudioDiscontinuityRecovery` was required
- Audio ring swung from 115.56 ms to 0 ms during the disruption

The source content was prerecorded video, so rendered FPS is intentionally excluded from the pass/fail decision. The latest audio recovery guard prevented a ring-capacity overrun, but only by dropping stale resume backlog. The independent continuity signals still reject this profile: audio stopped for 3.356 seconds, real underruns increased, stale audio had to be dropped, and the video overflow counter increased.

## Decision

`sync=false` is rejected for both forwarding sinks. `LiveFullLowLatency` contains the same harmful sink setting and is not promoted for another physical run. Phase 6 freezes `CurrentSafe`, retaining `sync=true`, `async=true`, and no effective `-vsync no` override.

No resolution, frame-rate, audio-format, or decoder-quality reduction was used.
