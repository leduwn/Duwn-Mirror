# Duwn Mirror media-core handoff — 2026-09-21

## Read first

The authoritative user request is:

`C:\Users\duwn\.codex\attachments\dd713853-5952-418d-a000-cdbc8419353e\Văn bản đã dán.txt`

The user-provided Phase 3 physical report is:

`C:\Users\duwn\.codex\attachments\95dd34a7-993c-4d7a-955a-99aad0748c3c\Văn bản đã dán.txt`

Workspace: `D:\Projects\Duwn Mirror`

There is no Git repository. Do not create an installer. Required verification is:

```powershell
cmake --build build-msvc --config Release --target duwn-mirror duwn-unit-tests
.\build-msvc\bin\Release\duwn-unit-tests.exe
```

Latest verification: **210 passed, 0 failed**.

## Current runtime state

- Latest binary is running from `build-msvc\bin\Release\duwn-mirror.exe`.
- UxPlay is running with the `CurrentSafe` development profile.
- At handoff time the iPhone was connected and streaming 2560×1184 with L16 44.1 kHz stereo.
- Log: `%LOCALAPPDATA%\Duwn Mirror\Logs\duwn-mirror.log` with five rotated backups.
- The newest 30-second discontinuity guard is built and running, but **has not passed physical acceptance**.

## Completed audit and physical phases

Read these reports in order:

1. `docs/media-core-phase1-2-audit.md`
2. `docs/media-core-phase3-physical-baseline.md`
3. `docs/media-core-phase4-vsync-ab.md`
4. `docs/media-core-phase5-sink-sync-ab.md`

Key decisions:

- UxPlay source was audited at upstream commit `57ea83411d5f7e0b38c5841987439340543f025c`; the exact packaged binary commit remains unprovable.
- `-vsync no` does not change the `-vrtp/-artp` forwarding branches. Physical A/B showed no benefit, so it was rejected.
- `sync=false` on both forwarding sinks caused RTP bursts, video queue overflow, a 3.356-second audio stop, underruns, and stale backlog drops. It was rejected.
- Keep UxPlay forwarding at `sync=true`, `async=true`. Do not run `LiveFullLowLatency`; it contains the rejected `sync=false` setting.
- Phase 6 therefore freezes `CurrentSafe` at the UxPlay boundary.
- A common sender audio/video epoch is still unavailable. `AV_skew` must remain reported as unavailable. Do not treat the placeholder `A/V=0.0ms drift=0.00ms/min` line as a real skew measurement.

## Preview-window clarification

The user asked why the Preview window could be stretched.

- Preview is intentionally resizable, but `WM_SIZING` locks its client aspect ratio to the current source.
- A live probe showed source 2560×1184 (AR 2.162) and Preview client 853×394 (AR 2.165); the small difference is pixel/border rounding.
- Resizing Preview does not change OutputWindow or OBS capture resolution.
- Maximize, fullscreen, or Windows Snap may create a differently shaped outer window; the renderer fits the video inside it.
- The user has not asked to make Preview fixed-size. Do not remove resize borders unless they explicitly request that behavior.

## Audio work now present in the tree

Existing before this handoff:

- Event-driven `IAudioClient3` shared-mode WASAPI, 10 ms selected period, 12 ms steady padding.
- Adaptive buffer controller.
- `AudioClockServo` with normal ±300 ppm correction.
- Continuous 33-tap windowed-sinc resampler. The Phase 3 pasted report calls it Speex-based; that statement is stale.
- Controlled backlog drop path and WASAPI startup prebuffer.

Changes made during this continuation:

1. Recovery rebuffer now uses the adaptive target instead of always restarting at 20 ms.
   - First startup remains 20 ms.
   - Files: `src/audio/WasapiOutput.h`, `src/audio/WasapiOutput.cpp`.
2. Added audio arrival-gap telemetry and detailed underrun logs.
   - Fields: `audio_arrival_gap_ms`, `audio_arrival_gap_max_ms`.
   - Underrun log records requested/pulled/available frames, padding, target, RTP age, last gap, and peak gap.
   - Files: `src/common/metrics/Metrics.h`, `src/audio/AudioEngine.cpp`, `src/audio/WasapiOutput.cpp`, `src/app/App.cpp`.
3. Adaptive emergency ceiling was raised from 40 ms to 100 ms based on measured 84–104 ms delivery gaps.
   - This is not a fixed prebuffer; startup remains 20 ms.
   - File: `src/audio/AudioBufferController.cpp`.
4. Added confirmed-discontinuity recovery:
   - Short gap recovery requires both `arrival_gap_ms > 60` and a pending real WASAPI underrun.
   - Long resume gaps over 250 ms still recover without that condition.
   - Current experimental guard bounds stale backlog for 30 seconds.
   - Files: `src/audio/AudioEngine.h`, `src/audio/AudioEngine.cpp`.
5. Added deterministic tests for adaptive rebuffer and confirmed-discontinuity classification.
6. Added `tools/capture-media-metrics.ps1` for one-row-per-second CSV capture that survives log rotation.

## Physical evidence from the audio investigation

### Original 40 ms ceiling failure

During steady CurrentSafe playback, real underruns grew from 3 to 9 in roughly seven minutes while RTP remained active. Detailed instrumentation later showed:

- WASAPI requested 480 frames per event.
- At failures only 254–468 frames were available.
- RTP age at the render event was 37–95 ms.
- One measured packet-delivery gap was 86.01 ms.

The 40 ms emergency ceiling prevented `OnUnderrun()` from raising target further.

### 100 ms adaptive ceiling result

A continuous session ran about 30 minutes after the ceiling change:

- After initial reconnect recovery, real underruns remained 2→2.
- Overrun frames remained 1572→1572.
- No further recovery or backlog drops occurred.
- Manual 55-second samples showed ring oscillating rather than growing monotonically.

The built-in log rotated too quickly because UxPlay plist output is verbose. Only the final 508 one-second samples were retained:

- ring P50 92.42 ms, P95 106.46 ms, P99 114.23 ms, max 118.90 ms
- target P50 88.00 ms, P95/P99/max 100.00 ms
- padding fixed at 12.00 ms
- servo P50 -2.6 ppm, P95 149.0 ppm, max 164.4 ppm, min -225.3 ppm
- video T0–T7 P50 13.72 ms, P95 24.78 ms, max 38.84 ms
- video queue P50 0, P95 1, max 2

This was continuity-stable but audio latency was not ultra-low; typical total buffered audio was around 100 ms including padding.

### Formal CSV rerun failure

Artifact: `artifacts/phase13-current-safe-rerun-partial-failed.csv`

Despite the filename history, this is a stopped **partial failed run**, about 11 minutes / 657 data rows.

At 16:39:46:

- real underrun 2→3
- WASAPI requested 480, pulled 158, available_before 158
- RTP age 84.05 ms
- target 100 ms
- immediately after backlog returned, ring jumped to 150.67 ms
- overrun frames 1572→2133 within several seconds

This proved that raising the ceiling alone could trade repeated underruns for high latency and burst overrun.

### Rotation-stress behavior

While the user rotated the phone and inspected Preview, the source changed portrait/landscape and audio produced repeated 128–205 ms gaps. The 5-second confirmed-discontinuity guard correctly detected recoveries but expired before the extended stale burst ended:

- recoveries reached 16
- real underruns reached 16
- backlog drops exceeded 3.2 million frames
- overrun frames still reached 4227

The guard was then increased to 30 seconds and rebuilt. Latest physical snapshot on that binary:

- real underruns: 1
- recoveries: 1
- overrun frames: 306
- backlog drops: 1,441,388
- ring: roughly 120–139 ms
- target: 76–84 ms
- peak measured gap: 139.51 ms

Therefore the 30-second guard is **not yet accepted**. It still allowed an overrun and large ring fill. Do not present it as solved.

## Recommended next step

1. Preserve `CurrentSafe` UxPlay sink settings.
2. Separate tests clearly:
   - steady orientation / continuous audio drift test
   - rotation discontinuity stress test
3. Re-evaluate the current 30-second backlog guard. The evidence suggests that a fixed time window is insufficient or too coarse.
4. Prefer a recovery state that ends after measured stable arrival/ring conditions rather than elapsed time alone.
5. Enforce the emergency ring ceiling only after a confirmed discontinuity. Do not silently drop audio during normal playback.
6. Consider a bounded recovery-rate mode or larger physical ring capacity only if measurement shows it can absorb bursts without raising steady latency. Normal servo must remain smooth; do not jump ratios.
7. Run `tools/capture-media-metrics.ps1` for formal reruns so all one-second rows survive logger rotation.
8. Do not claim Phase 13 pass until a fresh 30-minute CSV completes with no continual underrun/overrun growth and ring fill shows no monotonic drift.

## Remaining original acceptance work

- Finish and pass Phase 13 30-minute drift/stability test; 60 minutes preferred.
- Validate 2K/60 video-only and video+audio acceptance on final audio logic.
- OBS Window Capture + Application Audio Capture test.
- TikTok Live Studio capture test.
- 20 portrait/landscape rotations on final recovery logic.
- Audio endpoint switch test.
- Produce final config table, latency table, fallback table, test matrix, and evidence-backed summary.
- Do not implement LiveSync without a validated common sender timeline and both low-latency pipelines stable.

## Important cautions

- `sync=false` is already disproven; do not retry it as a default.
- Do not lower resolution, FPS, sample rate, channel count, or bit depth.
- Do not count content-video cadence below 60 FPS as a renderer failure. Compare renderer cadence with actual source cadence.
- Do not count reconnect/rotation gaps as steady-state drift; report them separately.
- Do not call the audio path Speex-based; current converter is windowed-sinc.
- The app log is extremely verbose because UxPlay plist lines are logged every second, so use the CSV capture tool for long tests.
