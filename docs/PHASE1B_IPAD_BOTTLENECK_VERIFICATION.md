# Duwn Mirror Phase 1B — iPad Latency Bottleneck Verification

Date: 2026-10-11 (Asia/Saigon)
Branch: `fix/obs-capture-ipad-frame-pacing`
HEAD: `0ca204be556bc1c4ef4ddc519965e0b9d37d931e`
Investigation executable: `D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe`

## I. Bottleneck Findings

No iPad, iPhone, Duwn Mirror, OBS, or TikTok Live Studio process was present when Phase 1B started. Therefore no 60–120 second physical session was captured and no device-specific FPS, p50/p95/p99, drop, resource, resolution, decoder, or zero-copy result is claimed.

The previous timing set could measure AU/decode/queue/Present cadence and correlated receive-to-Present-return latency, but it could not distinguish time spent inside the synchronous RTP callback from time between socket reads, decoder mutex contention, or the individual Media Foundation calls. The following opt-in measurements now close those local observability gaps:

| Evidence | Metric/log field | Meaning |
| --- | --- | --- |
| Socket/read spacing | `socket_read_interval` | Time between successful video `recv` returns observed in userspace. It is not a kernel packet-arrival timestamp and must not be labeled upstream jitter. |
| Receiver occupancy | `receiver_callback` | Time spent in the video RTP callback before the receive loop can call `recv` again. |
| Decoder lock | `decoder_mutex_wait`, `decoder_mutex_hold` | Wait and hold distributions in `App::OnVideoData`; `over_1ms` is the abnormal-wait count used for this investigation. |
| Decoder submission | `process_input`, `process_output` | Duration of each actual MFT call, including retry calls as separate samples. |
| Drain/backpressure | `[STATS] DECODER` | Per-window `outputs_per_input`, accepted-input sample count, `not_accepting`, stream-change (`sc`), failures, and software fallback. |
| Existing handoff/presentation | `queue_dwell`, `rtp_to_present_return`, `present_call`, interval metrics, frame-age and video/drop rows | Keeps the Phase 1 evidence needed to classify queue and Present behavior. |

All distributions use the existing fixed 256-sample storage per metric. The metrics thread emits one summary per active streaming cycle; media paths do not add per-frame INFO logs or allocate telemetry samples. The new clock reads and sample locks are skipped when `DUWN_FRAME_TIMING` is disabled. Telemetry collection occurs after releasing `m_decoder_mutex`, so diagnostics do not lengthen its measured hold interval.

## II. Root Cause

Classification: **Unconfirmed**.

Code evidence shows that the current AirPlay RTP callback synchronously reaches AU assembly and decoder submission while holding `m_decoder_mutex`. This makes receive/decode blocking a plausible cause, but it is not evidence that it is the active iPad bottleneck. The decoded queue remains bounded, MFT output is completely drained, and the GPU-native path remains available. No scheduling, queue, decoder choice, resolution, bitrate, source cadence, UxPlay forwarding, or Present policy was changed.

Use the physical timing evidence to classify a session:

- Receive/decode blocking: high callback or mutex p95/p99 aligns with `socket_read_interval` gaps; ProcessInput/Output identifies MFT time inside the hold.
- Decoder internals: ProcessInput/Output tails, `MF_E_NOTACCEPTING`, fallback, stream changes, or unusual outputs-per-input explain callback occupancy.
- Decoded handoff: decoder calls stay low while `queue_dwell`/frame age grows.
- Source/UxPlay: local callback, decoder, queue, and receive-to-Present-return stay low while AU/source cadence is late or irregular.
- Present/DWM: decode and queue remain regular while Present intervals/calls have tails. Present return still does not prove DWM scanout time.

## III. Optimization

No production optimization was implemented because there is no controlled physical iPad evidence selecting a bottleneck. Adding a decoder worker, changing queue policy, reducing quality, changing UxPlay buffering, or altering Present behavior would be speculative.

Phase 1B changed only diagnostic plumbing in:

- `src/common/telemetry/LatencyTelemetry.h/.cpp`
- `src/common/metrics/Metrics.h`
- `src/network/RtpReceiver.cpp`
- `src/app/App.cpp`
- `src/video/MFVideoDecoder.h/.cpp`
- `tests/unit/test_frame_timing.cpp`

`MFVideoDecoder::DrainOutput` now returns the number of successful outputs it already drained. Its loop, ordering, error handling, backpressure retry, frame ownership, and scheduling behavior are unchanged. The return value is aggregated per accepted input for diagnostics. This is smaller and lower risk than importing iPhoneMirror's worker or compressed FIFO without evidence that synchronous callback occupancy is the problem.

Before/after iPad benefit: **unavailable**. The change creates the measurement needed for a valid comparison; it does not claim lower latency.

## IV. Verification

- Release build: **PASS**. App, libraries, unit tests, and acceptance executables built from the current dirty tree.
- Unit: **445/445 PASS**. The new test covers all Phase 1B metrics, bounded collection, opt-in behavior, and the >1 ms count.
- Registered CTest targets: **3/3 PASS** (`duwn-unit-tests`, `duwn-portable-tests`, `duwn-capture-persistence-test`).
- Integration acceptance: **5/5 PASS**. WGC binding, synthetic GPU presentation, persistence, and the presentation budget passed. Ten-frame Present average was 7.46 ms; maximum was 41.36 ms. This is not an iPad result.
- Hit-test/live script: **PASS** against the installed executable. Placeholder forwarding, center drag, right/bottom resize codes, toolbar presence, persistence, and clean shutdown were observed. The saved settings were restored afterward.
- Git diff check: **PASS**.
- Physical iPad: **NOT RUN — no device connected**.
- Physical iPhone: **NOT RUN — no device connected**.
- Duwn+WGC capture comparison: **NOT RUN — no source device/session**.
- Weak-iGPU resource impact: **NOT MEASURED**. Diagnostics are disabled by default; the normal path only performs an atomic enabled check.
- Version/release state: unchanged at v1.1.5; no commit, push, tag, release, or version bump.
- Git status: existing dropdown, audio, Phase 1 telemetry/research changes are preserved, with the Phase 1B diagnostic/report additions still uncommitted.

### Physical capture procedure

Connect the iPad, show continuous motion, and keep the same quality, orientation, display, and capture settings for each run. Record 60–120 seconds with Duwn alone, then repeat with the same WGC capture application if available. Close the app normally after each run.

```powershell
$env:DUWN_FRAME_TIMING = '1'
& 'D:\Projects\Duwn Mirror\build-msvc\bin\Release\duwn-mirror.exe'
Remove-Item Env:DUWN_FRAME_TIMING
```

The logs are under `%LOCALAPPDATA%\Duwn Mirror\Logs`. Preserve the whole matching log and compare `[METADATA]`, `[STATS] VIDEO`, `[STATS] DECODER`, `[FRAME TIMING]`, `[FRAME AGE OUTPUT]`, `[LATENCY]`, capability, drop, and resource rows. Do not average one-second percentile rows into a session percentile. Record the device model/OS, exact content, Duwn-only versus WGC condition, monitor refresh, and capture method alongside each log.

For the lock diagnosis, compare `decoder_mutex_wait` p95/p99 and `over_1ms` with `receiver_callback` and `socket_read_interval` in the same windows. A userspace read gap can be caused by callback occupancy or upstream delivery; without kernel receive timestamps it does not identify either one by itself.

## V. Decision

```text
Phase 1B status: PARTIAL
Confirmed bottleneck: Unconfirmed; physical iPad timing is unavailable
Optimization implemented: None; targeted opt-in instrumentation only
Measured benefit: Not available
Unresolved latency: Physical iPad/iPhone, WGC comparison, upstream versus DWM visibility
Ready for Phase 2: NO
```

Stop at Phase 1B. Review a controlled physical capture before selecting any receive/decode, UxPlay, queue, or presentation change.
