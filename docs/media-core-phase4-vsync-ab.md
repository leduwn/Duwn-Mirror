# Media Core Phase 4 — `-vsync no` Physical A/B

Date: 2026-09-21

## Controlled profile

- Profile: `LiveNoVsync`
- Source: 2560×1184 at approximately 60 FPS with continuous L16 44.1 kHz stereo audio
- UxPlay change from `CurrentSafe`: added `-vsync no` only
- Video and audio forwarding sinks remained at GStreamer defaults: `sync=true`, `async=true`
- No quality, decoder, renderer, WASAPI, buffer, servo, or resampler setting changed

## Measured result

| Metric | P50 | P95 | P99 | Maximum |
| :--- | ---: | ---: | ---: | ---: |
| Video T0→T7 | 5.67 ms | 19.74 ms | 24.10 ms | 28.73 ms |
| Video queue depth | 0 | 0 | — | 2 frames |
| Audio ring fill | 68.06 ms | 99.15 ms | — | 111.38 ms |
| WASAPI padding | 12.00 ms | 12.00 ms | — | 12.00 ms |

- Final sampled ring fill: 58.42 ms and draining toward the 40 ms target.
- Servo range: -300.0 to 0.0 ppm; final sample -194.6 ppm.
- Audio overrun frames: 0.
- Audio backlog recovery drops: 0.
- Video continued at approximately 60–61 FPS.

## Decision

`LiveNoVsync` produced no measurable forwarding-latency advantage over `CurrentSafe`. Its startup audio accumulation and servo drain behavior matched the unchanged path. The observed video variation is within run/load variation and does not justify promotion.

This confirms the UxPlay 1.74 source audit: the `-vsync` setting is used by the local presentation branch and does not alter the `-vrtp/-artp` forwarding branches. `CurrentSafe` remains the selected profile after Phase 4.
