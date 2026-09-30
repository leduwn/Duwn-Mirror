# Media Core Architecture Pass — Phase 1–2 Audit

Date: 2026-09-21

## Scope and version

- Packaged executable: `runtime/duwn-airplay/uxplay.exe`
- Runtime version output: `UxPlay 1.74`
- The exact source commit used to build the packaged executable is not stored in this workspace, so a binary-to-commit match cannot be proved.
- Source behavior was audited against upstream FDH2/UxPlay commit `57ea83411d5f7e0b38c5841987439340543f025c` (2026-09-16), whose README identifies version 1.74.

## UxPlay / GStreamer forwarding audit

### Video

Source: `renderers/video_renderer.c:382-435`, `renderers/video_renderer.c:634-672`.

For the current H.264 `-vrtp` configuration, UxPlay constructs:

```text
appsrc name=video_source ! queue ! h264parse ! rtph264pay config-interval=1 ! udpsink host=127.0.0.1 port=<dynamic>
```

- The forwarding branch does not append UxPlay's `sync=true/false` video-sink setting. That setting is only appended in the local display branch (`!rtp`).
- The `sync` variable remains false for RTP forwarding, so UxPlay does not assign sender PTS to forwarded video buffers.
- Therefore `-vsync no` does not change the `-vrtp` forwarding path in audited 1.74 source.
- One default `queue` exists before `h264parse`. Packaged GStreamer defaults are 200 buffers, 10 MiB, or 1 second, whichever limit is reached first; `leaky=no`.
- `udpsink` uses packaged defaults because the current command supplies no sink properties: `sync=true`, `async=true`.

### Audio

Source: `renderers/audio_renderer.c:128-233`, `renderers/audio_renderer.c:248-278`, `renderers/audio_renderer.c:306-373`.

For `-artp`, UxPlay constructs one pipeline per supported input codec. The relevant shape is:

```text
appsrc name=audio_source ! queue ! <decoder if compressed> !
audioconvert ! audioresample quality=10 ! volume name=volume !
audioconvert ! audio/x-raw,format=S16BE,rate=44100,channels=2 !
rtpL16pay pt=96 ! udpsink host=127.0.0.1 port=<dynamic>
```

- The RTP branch does not append UxPlay's audio/video synchronization setting.
- Its internal `vsync` and `async` flags stay false in RTP mode; forwarded compressed audio receives no sender PTS before push.
- Therefore `-vsync no` does not change `-artp` forwarding in audited 1.74 source.
- One default `queue` exists directly after `appsrc`, with the same large defaults listed above.
- The forwarding `udpsink` defaults to `sync=true`, `async=true`.

### Current conclusion

`-vsync no` is not a meaningful A/B variable for Duwn's `-vrtp/-artp` paths in the audited source. The first physical forwarding A/B should test explicit `udpsink sync=false`, separately from `async=false`, after collecting the unchanged baseline. Queue occupancy also needs measurement because both forwarding paths contain an unbounded-for-live default queue.

## Phase 1 audio instrumentation

The Release build now records and logs:

- RTP packet duration
- A0→A1 receive-to-accept time
- A1→A2 S16BE-to-Float32 conversion time
- A2→A3 resampler time
- A3→A4 ring-write time
- A5→A6 WASAPI GetBuffer-to-ReleaseBuffer time
- current ring fill and total ring capacity
- actual `GetCurrentPadding` duration
- IAudioClient3 default, fundamental, minimum, maximum, and chosen periods
- endpoint stream latency reported by WASAPI
- underrun frames and rejected ring-write frames

The prior latency display used total WASAPI buffer capacity as if it were current padding. It now reports the actual current padding. The prior `estimated_av_skew_ms` subtracted unrelated software durations without a common sender timeline; that false-precision value was removed and is logged as unavailable.

## Baseline required before behavior changes

Run a real AirPlay session with the current forwarding configuration and retain the diagnostics log. Capture at least steady state, interaction bursts, audio startup, and 30 minutes of continuous playback. Physical video, physical audio, and A/V flash/click measurements remain required. No low-latency forwarding flag has been enabled in this phase.

## CurrentSafe live baseline

Captured 2026-09-21 from 232 one-second samples at 2560×1184, 60 FPS, with audio:

| Metric | P50 | P95 | P99 | Maximum |
| :--- | ---: | ---: | ---: | ---: |
| Duwn video T0→T7 | 2.45 ms | 32.80 ms | 47.73 ms | 51.70 ms |
| Video queue depth | 0 | 1 | 1 | 1 |
| Audio ring fill | 24.44 ms | 96.54 ms | 110.65 ms | 120.94 ms |
| WASAPI padding | 12.00 ms | 12.00 ms | 12.00 ms | 12.00 ms |

- Source and rendered cadence remained approximately 60 FPS.
- Decoder: Microsoft H264 Video Decoder MFT, hardware, D3D11-aware, NV12 zero-copy.
- `CODECAPI_AVLowLatencyMode` enable returned `S_OK`.
- IAudioClient3: available; default/fundamental/minimum/maximum/chosen period were all 480 frames (10 ms at 48 kHz). Buffer size was 1056 frames.
- Audio packet duration was 3.02 ms.
- Audio ring overrun frames: 0.
- Audio underrun-frame increase during the sample: 25,678.
- This short early sample captured startup instability and was superseded by the longer Phase 3 baseline below.
- Physical camera-based video/audio/A/V measurements are still outstanding.

## Forwarding A/B result

`LiveSinkAsync` (both `udpsink sync=false`, UxPlay timestamp policy unchanged) was run against the same live source. It failed quickly:

- ring overrun frames reached 34,003
- underrun frames exceeded 10,000
- rendered video briefly fell below the 60 FPS source cadence
- source resolution remained 2560×1184 and the video queue remained bounded

The profile was aborted and `CurrentSafe` restored. Explicit `sync=false` is not suitable for the current audio forwarding pipeline because it allows decoded audio to arrive in destructive bursts. `LiveFullLowLatency` contains the same sink setting and was therefore not promoted or tested further. `LiveNoVsync` is behaviorally redundant for RTP forwarding according to the audited UxPlay source.

Final forwarding choice for this pass: `CurrentSafe`. The developer-only profiles remain available through `DUWN_MEDIA_LATENCY_PROFILE` for investigation and are not exposed in release UI.

The later controlled repetitions are recorded in [Phase 4 `-vsync no` A/B](media-core-phase4-vsync-ab.md) and [Phase 5 sink `sync=false` A/B](media-core-phase5-sink-sync-ab.md). They confirmed the same decision: `-vsync no` is ineffective for RTP forwarding, while `sync=false` causes destructive video/audio bursts. Phase 6 therefore freezes `CurrentSafe` (`sync=true`, `async=true`).

## Phase 3 unchanged physical baseline

The completed 7-minute physical baseline is preserved in [media-core-phase3-physical-baseline.md](media-core-phase3-physical-baseline.md). With `CurrentSafe` (`sync=true`, `async=true`), local video T0→T7 measured 5.01 ms P50 and 7.16 ms P95, with queue depth normally 0–1 frame. Audio ultimately converged to 37.77 ms in the ring plus 12.00 ms WASAPI padding.

The 130–158 ms audio state was classified as burst accumulation after a 165.6 ms RTP silence gap, followed by a slow drain at the servo's -300 ppm limit. It was not the steady equilibrium, but the resulting multi-minute high-latency tail and 219 overrun frames still require correction. Phase 4 starts from this measured baseline rather than treating the burst as acceptable final behavior.

## Audio engine implementation after forwarding freeze

- The shared-mode renderer now uses `IAudioClient3::GetSharedModeEnginePeriod`, selects the endpoint minimum valid period, initializes with `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`, and renders from a dedicated event-waiting worker. The current endpoint exposes 480 frames (10 ms) for default, fundamental, minimum, and maximum periods, so 10 ms is the lowest supported choice on this device.
- `AudioBufferController` derives its minimum from two RTP packets or two endpoint periods, whichever is larger. It raises the target after jitter or underruns, lowers it after eight stable seconds, and enforces a measured live-mode target ceiling of 40 ms.
- WASAPI now primes playback to the adaptive target before consuming a newly active stream. Intentional silence while the source is inactive or the startup gate is filling is excluded from underrun feedback; a genuine active-stream underrun closes the gate and refills before playback resumes.
- `AudioClockServo` applies smoothed PI correction through the resampler, clamps normal correction to ±300 ppm, prevents integrator windup, and resets on stream flush or discontinuity.
- The former packet-local linear interpolation was replaced with a continuous 33-tap windowed-sinc streaming resampler. At 44.1 kHz its 16-frame lookahead is 0.363 ms. Streaming state is retained across RTP packets.
- A resume gap over 250 ms starts an exceptional, logged `AudioDiscontinuityRecovery`. It resets stale state, applies a short ramp, and bounds incoming resume backlog to 40 ms. Recovery remains active until two seconds after the final bounded re-anchor because the observed sender released its backlog over more than two seconds.

The first physical recovery run recorded one 3.156-second RTP gap. The initial fixed two-second guard prevented ring overruns but expired while the sender was still releasing backlog; ring fill then rose to 98.56 ms. The guard has since been changed to extend from every re-anchor. A new physical connection is required to validate that revision. This exceptional recovery is not used for normal drift correction.

The clean post-gate physical session confirmed that an active 2560×1184 video stream with no incoming audio RTP remains at zero ring target, zero real underruns, zero overrun frames, and zero recovery events while WASAPI continues its device clock with silence.

## Active audio physical validation

Captured 2026-09-21 with continuous live audio playback (RTP 184–192 pkts/s, ~175 KB/s):

| Metric | P50 | P95 | P99 | Maximum |
| :--- | ---: | ---: | ---: | ---: |
| Video T0→T7 total | 14.99 ms | 26.81 ms | 29.75 ms | 31.16 ms |
| Video queue depth | 0 | 1 | 1 | 1 |
| Rendered FPS | 61.00 | 62.00 | 63.00 | 63.00 |
| Audio ring fill | 21.42 ms | 32.29 ms | 32.29 ms | 32.29 ms |
| Audio ring target | 40.00 ms | 40.00 ms | 40.00 ms | 40.00 ms |
| WASAPI padding | 12.00 ms | 12.00 ms | 12.00 ms | 12.00 ms |
| Clock servo correction | +4.20 ppm | +5.20 ppm | +5.20 ppm | +5.20 ppm |

Key validation results:

- Audio underrun-frame delta: 0 frames across continuous playback.
- Audio overrun-frame delta: 0 frames.
- Discontinuity recoveries: 0 triggered during continuous stream.
- Audio ring remains bounded at 21–32 ms, never exceeding the 40 ms target ceiling.
- Clock servo operates smoothly within +1.90 to +5.20 ppm (far below ±300 ppm bound), preventing drift without audible pitch modulation.
- Video queue stays at 0–1 frame at 2K/60 (2560×1184).

## Remaining acceptance work

- Repeat the resume-backlog run and verify zero overruns, bounded ring fill, and clean audible recovery.
- Run the 30/60-minute drift test and collect ring, target, padding, servo, underrun, and overrun distributions.
- Complete physical video, audio, and flash/click A/V measurements.
- Validate OBS, TikTok Live Studio, endpoint switching, mute, rotation, disconnect/reconnect, and Wireless/Wired switching.
- A common sender epoch is not available in the forwarded RTP data audited so far. Exact A/V skew and `LiveSyncController` remain intentionally unimplemented until both streams independently pass low-latency physical validation.

## Verification

- Release targets `duwn-mirror` and `duwn-unit-tests` built successfully.
- Unit tests: 207 passed, 0 failed.
- Installer was not rebuilt.
