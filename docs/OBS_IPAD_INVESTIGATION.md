# Duwn Mirror — OBS + iPad Optimization Report

Date: 2026-10-09 (Asia/Saigon). Investigation build; no release approval.

## Baseline and scope

- Baseline: released v1.1.5; tag points to `9a64b8fc8e1ba02321715616de3e490c956daf1e`.
- Current branch: `fix/obs-capture-ipad-frame-pacing` (created from the existing working tree).
- HEAD: `0ca204be556bc1c4ef4ddc519965e0b9d37d931e`, initially on `main`.
- Existing local fixes preserved: `src/ui/MainWindowView.cpp`, `src/audio/AudioDeviceManager.cpp`, `tests/unit/test_audio_device.cpp`. These remain uncommitted.
- No push, merge, commit, tag, release, version bump, history rewrite or destructive cleanup.

## Production flow audited before editing

Wireless iPad/iPhone → UxPlay sidecar → GStreamer `appsrc ! queue ! h264parse ! rtph264pay ! udpsink` → dynamic loopback UDP port → `RtpReceiver::RecvLoop` → `AirPlayEngine::OnVideoRtp` → `App::OnVideoData` under the decoder mutex → `VideoDecoder::FeedRtp` / `HevcRtpAssembler` → complete Annex-B AU → `MFVideoDecoder::Feed` (`ProcessInput`, `DrainOutput` / `ProcessOutput`) → frame-ready callback → `FrameScheduler::PushFrame` → render thread → `App::OnFramePresent` → `VideoRenderer` or WARP renderer → Output HWND.

The optional Direct IPC route consumes shared-memory AUs and enters `FeedAccessUnit`; it has no observed local RTP packet timestamp. Wired/direct routes also reuse the decoder/scheduler/output infrastructure. There is no need for another renderer.

The primary renderer targets `OutputWindow::Hwnd()` directly. `VideoSurfaceHwnd()` returns the same HWND. The visible placeholder is a child, hidden when streaming; the toolbar is a separate owned utility window. Output style is `WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN`, extended style `WS_EX_APPWINDOW`. No capture-exclusion affinity, layered output or no-redirection flag was found in this path. Output lifetime spans stream reconnect; it is recreated during full media-pipeline recovery.

Hardware swapchain: BGRA, two buffers, `CreateSwapChainForHwnd`, `DXGI_SWAP_EFFECT_FLIP_DISCARD`, frame-latency waitable flag, maximum frame latency already 1. Tearing support adds the swapchain capability flag, but frame Present does not request tearing. Primary calls `Present(0, 0)`; `SetNonBlocking(false)` is enforced on creation and recovery. Preview alone can use `DXGI_PRESENT_DO_NOT_WAIT`. These policies were preserved. WARP also calls `Present(0, 0)`.

Existing buffers/waits:

- UxPlay/GStreamer forwarding queue and sink clocking; exact upstream occupancy unavailable. Safe default retains sink synchronization; experimental latency profiles can change it, and were not enabled or modified.
- UDP receive buffer requests 8 MiB, with a 4 MiB fallback. Decode executes synchronously on the receive callback, so slow decode can delay further socket reads.
- Compressed AU assembly buffers and Media Foundation internal surfaces/reorder buffering. The hardware path retains the output sample and NV12/P010 GPU texture; system-memory output uses a six-texture upload pool.
- Default `GameLowLatency` scheduler is bounded to 1–3 decoded frames by the selected policy (Balanced default: 3). It waits for source availability and the DXGI waitable object, with 50/100 ms timeout limits. A lone frame remains eligible after a static period.
- Experimental `PresentationClock` has a separate bounded de-jitter queue; new enqueue/dequeue diagnostics target the production default mode.
- GPU video processor converts/scales into the back buffer. Shared-texture export is active only with consumers and includes GPU synchronization. WGC captures through Windows separately.

Potential local latency: synchronous receive/decode, decoder input allocation and buffering, software upload, D3D context mutex contention, decoded queue dwell, DXGI readiness, resize/processor rebuild, Present and export GPU synchronization. Upstream source encoding/network/UxPlay and DWM visibility latency lie outside the local receive-to-Present-return measurement.

RTP timestamps identify AU boundaries. In the wireless RTP path the MF sample PTS currently comes from local `arrival_ns`, not an independently synchronized iPad clock; the scheduler's PTS cadence is consequently not proof of source cadence. No clock-domain subtraction or pacing-policy change was made without measured evidence.

FPS: existing `[FPS DIAGNOSTIC]` separates RTP-marker, AU, decode and successful primary Present rates using elapsed sample time; source quality uses a three-second AU window. `video_rendered_frames` counts scheduler callback attempts and cannot replace successful `video_present_ok`. Existing decode-call duration, output gaps, queue residence, DXGI wait and render diagnostics generally lack complete p99 distributions. Average FPS, nominal MF media-type FPS, and one UI snapshot cannot establish a 40 FPS cap.

## OBS

- Actual capture method in the user's OBS session: unavailable; no local OBS process, standard installation directory, settings or logs were found.
- Root cause: unconfirmed for the user's session. Confirmed source-level behavior: OBS 32.0.4 Automatic chooses BitBlt for `DUWNMirrorOutputWindow`, because that class matches neither WGC allowlist.
- OBS configuration or Duwn code: explicit WGC is the supported configuration to test first. No confirmed Duwn capture defect was repaired; only capture guidance was clarified.
- Files changed for OBS: `README.md`, `README.en.md` (actual Output title, explicit WGC, log/reconnect checks).

| Method | Result |
| --- | --- |
| OBS Automatic | User reported black selecting Output; actual selected/chosen method was not supplied. Source predicts BitBlt when Automatic is selected. |
| OBS BitBlt | Not physically tested. |
| OBS WGC | Not physically tested in OBS. |
| OBS Display Capture | Not physically tested. |
| TikTok Live Studio WGC | User-reported working baseline; no new regression session. |

This remains a configuration-resolution candidate, not a verified configuration-only fix. The existing integration test checks WGC item binding when supported; it does not verify moving video in OBS.

Evidence: [OBS 32.0.4 method-selection and selected/chosen log code](https://github.com/obsproject/obs-studio/blob/32.0.4/plugins/win-capture/window-capture.c). Explicit **Windows 10 (1903 and up)** must be tested on a restored, visible Output window. If it remains black, collect the selected/chosen log entry, current HWND, minimized/cloaked state and both adapter LUIDs before considering an HWND/GPU fix.

## iPad measurements and attribution

- Actual FPS: user confirms approximately 60 FPS is possible; no new physical iPad session was available.
- RTP frame timing p50/p95/p99: unavailable for current iPad.
- AU timing p50/p95/p99: unavailable for current iPad.
- Decoder timing p50/p95/p99: unavailable for current iPad.
- Present call interval p50/p95/p99: unavailable for current iPad.
- Stutter events: not reproduced in a controlled physical session.
- Stage responsible: unconfirmed; cases source/forwarding, decode, queue, Present/composition cannot yet be separated.
- Receive→Present-return latency p50/p95/p99: unavailable for current iPad. No end-to-end latency reduction claimed.
- Queue age: no current physical distribution; source audit confirms the decoded queue remains bounded.
- Decoder path / GPU zero-copy: unverified for a current iPad session.

Historical evidence only: `%LOCALAPPDATA%\Duwn Mirror\Logs\duwn-mirror.1.log`, 2026-10-04 17:47 session, model `iPad16,3` (11-inch M4), AirPlay user agent `960.13.1`. It records hardware Microsoft H.264 MFT / NV12 GPU textures, coded 1328×1920 and visible 1324×1920, rotating to 1920×1328 / 1920×1324; requested Auto 1920×1920. Client `encoderCurrentFPS=60` appears at 17:47:06.921. This is encoder-reported FPS, not a controlled proof of successful 60 FPS presentation. The same old log contains iPhone 13 Pro output 1920×896 / visible 1920×888 in other sessions. OS version, codec profile/level, identical content and capture conditions are unavailable. These sessions are not combined into a before/after comparison; old zero/misattributed latency fields are not trustworthy evidence. These details were read before normal test-run logging rotated that file; the full original historical log was not retained separately.

It would also be premature to conclude “Local Duwn pipeline is not the confirmed latency bottleneck”: current iPad measurements are absent. The new diagnostics are intended to test that conclusion.

## Changes implemented

1. Carry the local receive-thread QPC tick through RTP callbacks into AU assembly. It is a local observation, not an iPad timestamp or kernel/network timestamp.
2. Correct first-packet QPC assignment after an AU timestamp boundary, and clear H.264 assembly/decode timing on flush.
3. Replace the decoder's current-input timestamp reuse with a bounded 64-entry correlation table keyed by the exact MF sample time of accepted input. Delayed/reordered output obtains its own timestamps. Failed submissions are not registered; reset/fallback/flush clear pending entries. Missing, evicted or ambiguous keys produce unavailable timing, never guessed data. Both GPU and software-upload output use the same correlation. Sample time remains a key, never a clock subtracted from QPC.
4. Add opt-in fixed-capacity stage interval and duration samples to existing `LatencyTelemetry`. Each metric retains at most 256 values, with count/mean/max/gap totals for the collection window. Percentiles use retained samples, explicitly reported as `retained`. Collection/sorting/log formatting happens on the metrics thread; media calls add no strings or dynamic samples. Session reset rejects timestamps predating the reset cutoff.
5. Measure intervals at RTP/AU/decode/enqueue/dequeue/actual primary Present begin, and durations RTP→AU, AU→output, output→enqueue, enqueue→dequeue, dequeue→Present return, RTP→Present return and the Present call. Duration distributions include successfully presented, correlated frames; discarded/unmatched frames do not become zero-latency samples. Present intervals include actual primary calls, while success/skip counters remain separate. Direct IPC/synthetic input does not invent an RTP timestamp.

Scope: `src/network/RtpPacket.h`, `RtpReceiver.cpp`; `src/airplay/AirPlayEngine.h/.cpp`; `src/app/App.h/.cpp`; `src/video/DecoderInputTiming.h`, `MFVideoDecoder.h/.cpp`, `VideoDecoder.h/.cpp`, `HevcRtpAssembler.h/.cpp`, `FrameScheduler.cpp`, `VideoRenderer.cpp`, `WarpVideoRenderer.cpp`; `src/common/telemetry/LatencyTelemetry.h/.cpp`; `tests/unit/test_frame_timing.cpp`, `test_main.cpp`; both READMEs and this report. No config, external ABI/export or fixture changes required.

The five regressions cover delayed/reordered output, missing/ambiguous correlation, bounded storage and reset, gap/percentile calculations, invalid/absent clock stages, disabled measurement, stale session samples, and first-packet timing across HEVC boundaries/flush.

## Drop accounting

The UI receives `state.dropped_frames = video_dropped_frames`, a session cumulative total reset on disconnect/idle, not drops in the current second. It mixes H.264 incomplete-FU events with scheduler discards and is not a DWM frame-loss count. Existing `[STATS] VIDEO drop=…/s` reports a delta; several reason fields in the same line remain cumulative.

| Reason | Existing evidence / semantics |
| --- | --- |
| RTP lost/reordered packets | `network_lost_packets` / `network_reordered_packets` are not populated by the active video callback; do not infer zero loss. Audio sequence-gap counters concern audio only. |
| AU incomplete | H.264 interrupted/sequence-broken FU increments the mixed drop counter; HEVC has `hevc_fu_aborted` / `hevc_fu_seq_gaps`. NAL events are not necessarily whole lost frames. |
| Decoder failure | `decoder_process_input_failed`, `decoder_output_failed`; API failures are not interchangeable with dropped-frame counts. |
| Queue overflow | `video_queue_overflow_drops`, with session/lifetime queue-full diagnostics. |
| Catchup / superseded | `video_latency_catchup_drops` overlaps `video_stale_age_drops` or `video_superseded_before_present`; do not sum overlapping totals. |
| Format / old generation | `video_format_transition_drops`, `video_stale_generation_drops`. |
| Order / late rejection | `video_presentation_late_drops`. |
| Present skipped/error | `video_present_skipped`, `video_present_errors`, separate from successful `video_present_ok`. |
| DWM missed/visible frames | Unavailable; no DWM loss telemetry was fabricated. |

Physical frame drops by reason: unavailable in this investigation. Gap thresholds count intervals, not losses. The dashboard and existing counter behavior were not redesigned.

## Measurement procedure and limitations

Start the investigation executable with `DUWN_FRAME_TIMING=1`. For example, in PowerShell:

```powershell
$env:DUWN_FRAME_TIMING = '1'
& 'D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe'
Remove-Item Env:DUWN_FRAME_TIMING
```

Read `[FRAME TIMING]` from `%LOCALAPPDATA%\Duwn Mirror\Logs\duwn-mirror.log`. Every metrics cycle includes `session`, metric, `count`, `retained`, mean/p50/p95/p99/max, >25/>33/>50 ms counts and availability. Do not average window percentiles and call that a session percentile. Compare the 16.67 ms reference only during continuous moving 60 FPS content; static/VFR gaps are valid observations.

Repeat the same moving content, quality preset and monitor setting for Duwn alone, Duwn+OBS explicit WGC, and Duwn+Live Studio WGC. Record device/OS, actual UxPlay command, coded/visible geometry, client FPSdata, decoder type and zero-copy, capture method and CPU/GPU load. Compare iPad/iPhone in separate clearly identified sessions. Default diagnostics are disabled; remove the environment variable to return to production operation.

Present return only establishes completion of the API call. Neither the existing waitable object nor these timestamps prove scanout/visible-frame time. `GetFrameStatistics`/DWM timing was not added without validating support and reliable per-frame attribution in this presentation model. Upstream AirPlay and display latency require separate measurement. New raw frame logs/export and per-frame INFO output were deliberately omitted.

## Automated acceptance

- Release build of app, unit and capture/persistence targets: PASS.
- Unit: **444/444 PASS** (five new tests; existing audio/dropdown test retained).
- Integration: **5/5 PASS**, including actual WGC `GraphicsCaptureItemInterop` binding to the borderless HWND, synthetic GPU presentation, geometry persistence and the existing ten-frame performance check. This is not an OBS moving-video test or a long-run 60 FPS acceptance test; occasional individual Present calls exceeded 16.67 ms while the test's mean budget passed.
- `python tools/verify_hit_test_and_live.py`: PASS against the installed executable. The same script was also run against the investigation build via a temporary in-memory path substitution, without replacing the installed app. Center drag, placeholder forwarding, resize edge codes and clean shutdown passed. Toolbar movement was not confirmed in the first build run, then passed on an idle repeat; the script itself does not fail on every missing assertion, so its exit code alone is insufficient.
- Diagnostic toggle: PASS on short synthetic sessions; disabled produced no new timing rows, enabled produced 234 rows with real Present-call intervals. Missing RTP stages stayed unavailable. An iPhone connected near the end of the disabled synthetic run, so that pair is **not a valid performance A/B comparison** and cannot establish overhead equivalence. The enabled synthetic run presented about 60 frames per second in steady windows; this does not establish iPad FPS.
- Standalone physical observation: 30 seconds, **no device connected**, no physical FPS/timing windows. The temporary harness initially assumed every enabled run emits timing and missed a log rotation; the rotated log was recovered and verified. An idle run correctly yields no frame measurements. No physical PASS or zero-latency sample inferred.
- Git diff --check: PASS.
- Git status: uncommitted investigation source/tests/docs plus the three preserved local fixes. Generated `src/common/Version.h` metadata restored to its pre-task content after building; product version remains 1.1.5. User settings restored from the pre-test backup. No running test app left behind.

Local evidence (ignored artifacts): `artifacts/obs-ipad-build.log`, `obs-ipad-unit.log`, `obs-ipad-integration.log`, `obs-ipad-hit-test-installed.log`, `obs-ipad-hit-test-build.log`, `obs-ipad-hit-test-build-repeat.log`, `obs-ipad-motion-0.log`, `obs-ipad-motion-1.log`, `obs-ipad-toggle-validation.json`, `obs-ipad-physical.log`, `obs-ipad-physical-summary.json`, `obs-ipad-git-status.txt`. These do not replace the missing physical OBS/iPad/iPhone/audio acceptance.

## Release decision

Changes deliberately not made: resolution/bitrate/codec reduction, UxPlay flag or topology changes, new queue/drop policy, decoder thread changes, artificial audio delay, vsync/latency policy changes, CPU readback, GDI mirror, duplicate frames, second renderer, firewall/mDNS/BLE edits, release preparation.

iPhone regression: automated pipeline tests only; current physical session not tested.
Audio regression: automated audio tests only; current routing/listening not tested. Pre-existing audio fixes preserved.
Live Studio regression: not tested in a new physical session.
Known limitations: physical timing and capture matrix incomplete; no confirmed stutter stage or end-to-end improvement; a small bounded diagnostic lock remains when opted in; correlation beyond 64 pending inputs or duplicate sample times is unavailable.
Ready for v1.1.6: **NO**. Review diagnostics and obtain controlled physical acceptance before any release preparation.
