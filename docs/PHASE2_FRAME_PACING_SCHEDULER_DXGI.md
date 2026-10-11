# Duwn Mirror Phase 2 — Frame Pacing, Scheduler, and DXGI Presentation

Date: 2026-10-11
Duwn branch: `fix/obs-capture-ipad-frame-pacing`
Duwn baseline HEAD: `0ca204be556bc1c4ef4ddc519965e0b9d37d931e`
iPhoneMirror reference: `829c5919f25718efc91f3fae4ee1e808f193964d`
Version remains `v1.1.5`; no commit, push, tag, release, or Phase 3 work was performed.

## A. Cross-Repository Algorithm Review

| Mechanism | iPhoneMirror | Duwn Mirror | Decision |
|---|---|---|---|
| Decoded-frame handoff | `CaptureSession::latest_frame_` and `WirelessClientStream::latest_frame_` retain an immutable latest-frame `shared_ptr` (`CaptureSession.cpp:1958`, `WirelessReceiverHub.cpp:504`). This prevents an unbounded decoded backlog. | `FrameScheduler::PushFrame` uses a policy-controlled decoded queue capped at 1–3 frames, drops the oldest on overflow, and tracks format generation (`FrameScheduler.cpp:372`). | Keep Duwn's existing queue. It already provides latest-frame behavior in Fastest mode and preserves short valid bursts in Smooth/Balanced modes. A second mailbox would add ownership and synchronization without removing a demonstrated bottleneck. |
| Stale-frame selection | The provider exposes the newest retained frame; the renderer rejects repeated timestamps and retains a dirty candidate when presentation must retry (`D3D11PreviewRenderer.cpp:1485–1535`). | `PopLatestValidFrame` presents the oldest valid frame while it is within the cadence-derived freshness budget, otherwise discards pending history and returns the newest (`FrameScheduler.cpp:164`). | Keep the policy. It avoids unnecessary drops while the display can still consume a short burst. Correct its cadence input rather than replacing it. |
| Presentation scheduling | A software deadline loop coalesces resize work and schedules from an ideal cadence. Preview presentation is nonblocking and retries `DXGI_ERROR_WAS_STILL_DRAWING`/occlusion (`D3D11PreviewRenderer.cpp:1485`, `:1550`). | The scheduler waits for a frame when empty, then uses the DXGI frame-latency object as the display opportunity. The renderer is told to skip its own wait so a frame is not waited twice (`FrameScheduler.cpp:1038–1114`, `App.cpp:4285`). | Keep Duwn's single DXGI-clocked path. It has lower wakeup overhead than a 1 ms software polling loop and is already integrated with the Output/OBS surface. |
| Swap chain latency | Device maximum frame latency is 1 (`D3D11PreviewRenderer.cpp:546`). | Both hardware and WARP renderers use flip-discard, two buffers, and maximum frame latency 1 (`VideoRenderer.cpp:99–119`, `WarpVideoRenderer.cpp:135–146`). | Equivalent low-latency constraint; no change. |
| Present policy | Preview uses `Present(0, DXGI_PRESENT_DO_NOT_WAIT)` and explicitly retains/retries rejected frames. | Primary output uses `Present(0, 0)` after the frame-latency wait. A nonblocking mode exists but is not the primary policy (`VideoRenderer.cpp:718`, `:916`). | Do not import the iPhoneMirror policy. Its retry state machine and WPF/HwndHost history differ, while Duwn previously regressed under an unsuitable nonblocking primary Present policy. |
| Frame lifetime | Immutable shared ownership lets capture and preview independently retain the latest GPU frame. | `VideoFrame` moves through a bounded queue and carries COM texture ownership; output/capture export remains GPU-side. | Both are safe for their architectures. No CPU copy or readback was introduced. |
| Resize/rotation | Renderer coalesces interactive resize and retains the latest candidate for immediate redraw. | Format-generation transitions purge older geometry, reject late old-generation frames, and signal swap-chain resize from the accepted frame. | Duwn's generation boundary is stronger for decoder format changes. Keep it. |
| GPU synchronization | Explicit resource unbinding and retry behavior avoid implicit hazards in the preview copy path. | The renderer/context mutex and shared-texture query/lease protocol protect Output and capture consumers. | No redundant synchronization was proven in the primary path. Preserve zero-copy NV12/P010 and current capture safety. |

The iPhoneMirror code was studied only as an algorithm reference. No GPL source was copied.

## B. Scheduler Findings

```text
FrameScheduler architecture: One scheduler thread; source event when empty; DXGI frame-latency waitable object when available; one dequeue/present decision per display opportunity.
Queue strategy: Policy-controlled deque, 1–3 decoded frames, move ownership, bounded under every tested workload.
PTS/clock semantics before: Wireless MF sample PTS was local packet arrival time. Bursty delivery therefore looked like sub-8 ms noise and could leave cadence classified as 29.97/30 FPS.
PTS/clock semantics after: The first RTP timestamp is anchored to local monotonic arrival time; subsequent PTS values follow signed 90 kHz RTP deltas with fractional carry, wrap support, and discontinuity re-anchoring.
Wait strategy: 50 ms source-event timeout only when empty; up to 100 ms on the DXGI latency object when a frame is pending. OnFramePresent skips the renderer's duplicate wait when that object exists.
Drop policy: Reject older generations; drop oldest on queue overflow; select newest only when policy says always-latest or the oldest exceeds its cadence-aware freshness budget.
Presentation policy: Primary output remains flip-discard, two buffers, maximum latency 1, Present(0, 0).
Confirmed correctness issue: With bursty 60 FPS RTP delivery, a 25 ms-old frame A could still be considered fresh under the incorrectly retained 30 FPS cadence and be presented while frame B was ready.
Performance bottlenecks: No scheduler lock, allocation, redundant texture copy, or wakeup change met the evidence threshold. Capture-consumer GPU synchronization can add work when consumers are active, but Phase 2 did not prove that it should be weakened.
```

The 90 kHz rate is the existing video RTP contract in `StreamMetadata`/codec classification and UxPlay's H.264/H.265 RTP path. The mapping is deliberately relative: it does not equate RTP timestamps to Windows QPC and does not claim iPad/Windows clock synchronization. A delta over ten seconds is treated as a source discontinuity and re-anchored to the observed local arrival time. Flush/reset clears the mapper. Decoder output continues to carry MF's corresponding input PTS, so reorder does not replace the source timeline with completion order.

## C. Deterministic Benchmark

The harness uses the production `FrameScheduler`, `StreamingPolicy`, `VideoFrame`, and new RTP-to-relative-PTS mapper. It does not duplicate the scheduling algorithm.

| Workload | Result |
|---|---|
| Stable 60 FPS | 60 source frames at 1,500 RTP ticks per frame were presented in order. No synthetic frames, valid-frame drops, or queue accumulation. The cadence estimator converged to 60 FPS despite 1 ms local arrival spacing. |
| Variable FPS/static interval | A two-second source interval remained one real frame; no duplication or false loss/drop was generated. The existing cadence outlier policy remained stable. Existing scheduler tests also cover 29.97, 30/60 transitions, and cadence hysteresis. |
| Burst arrivals | A three-frame fresh burst stayed FIFO and bounded at three. Frames were not discarded while still inside the display budget. |
| Delayed decoder | With frame A aged 25 ms and frame B aged 1 ms, the corrected 60 FPS cadence selected B and accounted for one superseded frame. Generation and sequence order remained valid. |
| GPU stall | With dequeue withheld for 600 simulated arrivals, queue depth never exceeded three; the next opportunity selected the newest frame and drained stale history. The test performs no busy wait. Existing concurrent scheduler and hidden-swap-chain tests cover deadlock/progress behavior. |
| Rotation | A generation transition from 1920×896 to 1920×1328 purged the late old-generation frame and retained the new dimensions. |
| Session reset | `Flush`, sequence restart, and a late prior-generation frame were exercised; only the post-reset generation was returned. |
| Long run | 216,000 frames, equivalent to one hour at 60 FPS, completed in 359.06 ms. Queue depth was exactly one before each pop and zero at completion; no new scheduler drops occurred. |

```text
Queue age p50/p95/p99: 1.60 / 1.70 / 1.90 microseconds (direct CPU harness residence)
Frame intervals p50/p95/p99: 16.6667 / 16.6667 / 16.6667 ms (generated RTP source timeline)
Drop accounting: 0 in steady one-hour workload; targeted stale/overflow/generation cases matched exact expected counts
Frame gaps >25/>33/>50 ms: 0 in the stable 60 FPS workload
Latency bounds: queue <= 3 under burst/stall; <= 1 in steady workload
CPU cost: 1,662.3 ns/frame wall-clock for mapper + push + pop + assertions in the one-hour-equivalent harness
Memory: decoded queue remained within the production 1–3 frame policy; test-only percentile storage was bounded to 216,000 int64 samples
```

## D. Production Changes

```text
Root cause: Local RTP packet arrival spacing was being used as source presentation cadence. Network/process bursts can compress arrival deltas, preventing 60 FPS classification and doubling the Smooth-mode stale budget.
Algorithm considered: iPhoneMirror latest-frame mailbox, always-newest queueing, nonblocking Present retry, and relative RTP timestamp mapping.
Why this improves Duwn: It fixes the clock semantic at decoder ingress while preserving Duwn's tested queue, DXGI clock, output capture, and zero-copy renderer paths.
Files changed for Phase 2:
  src/video/RtpTimestampMapper.h
  src/video/VideoDecoder.h
  src/video/VideoDecoder.cpp
  tests/unit/test_phase2_scheduler.cpp
  tests/unit/test_main.cpp
Behavior before: A burst-delivered 60 FPS stream could remain classified as 29.97/30 FPS; at a 25 ms queue age the scheduler could present A even though B was ready.
Behavior after: RTP deltas classify the same stream as 60 FPS; the 25 ms-old A is stale and B is presented. Wrap, reset, discontinuity, fractional conversion, H.264, and H.265 share the mapping.
Tests added: RTP mapping semantics; direct before/after regression; stable/VFR/burst/delayed/stall/rotation/reset matrix; one-hour-equivalent production scheduler run.
```

No renderer, queue-capacity, streaming-mode default, audio, AirPlay port, firewall, OBS, resolution, bitrate, or version setting was changed in Phase 2.

## E. Performance

```text
Synthetic before/after:
  Before: burst arrival PTS retained 29.97 FPS classification; A(age=25 ms) presented, B remained queued.
  After: RTP-derived PTS converged to 60 FPS; B presented and A counted once as superseded.
Scheduler source interval p50/p95/p99: 16.6667 / 16.6667 / 16.6667 ms
Queue age p50/p95/p99: 1.60 / 1.70 / 1.90 microseconds
Frame gap counts >25/>33/>50 ms: 0 / 0 / 0 in deterministic stable-60 input
CPU overhead: 1,662.3 ns/frame wall-clock in the one-hour-equivalent CPU harness
Memory: production decoded queue remained bounded; no production allocation or buffer was added per frame
GPU presentation behavior: unchanged; synthetic integration Present average 3.87 ms, maximum 26.75 ms over ten 1080p frames

Physical iPad: NOT TESTED (device unavailable)
Low-end Intel/AMD iGPU comparison: NOT VERIFIED
```

The current host did validate Intel hardware D3D11 decode/zero-copy and WARP presentation through the existing unit suite. Those checks establish compatibility on this host, not performance across all integrated GPUs.

## F. Verification

```text
Build: PASS — Release application and dependencies linked to artifacts/phase2-build/duwn-mirror.exe. The normal output path was locked by the already-running development executable, so validation used an alternate output directory without stopping it.
Unit: PASS — 449/449
Portable tests: PASS — 6/6
CTest: PASS — 3/3
Integration: PASS — 5/5; WGC bind, synthetic GPU motion, persistence, and 1080p presentation budget
Hit-test: PASS — installed executable; placeholder forwarding, draggable center, right/bottom sizing, toolbar presence, and clean shutdown. User settings restored to output position (400, 200), size 600×1300.
Diff check: PASS — only existing line-ending conversion warnings
Git status: Expected pre-existing Phase 1/1B working changes preserved; Phase 2 adds the mapper, scheduler tests, and this report. No reset/clean/discard was used.
```

## G. Decision

```text
Phase 2 status: COMPLETE

Confirmed improvements:
  - Wireless decoder PTS now preserves the source RTP cadence under bursty arrival.
  - The demonstrated stale-A/new-B decision now selects B at the correct 60 FPS freshness budget.
  - Deterministic production-scheduler coverage now includes all Phase 2 workload classes and a one-hour-equivalent run.

Unchanged mechanisms:
  - Bounded 1–3 frame decoded queue and policy-specific drop behavior.
  - DXGI frame-latency waitable object, flip-discard, two buffers, maximum latency 1, and Present(0, 0).
  - GPU zero-copy, capture export protocol, image quality, resolution, and bitrate.

Rejected algorithms:
  - A second latest-frame mailbox: duplicates the existing bounded/policy queue.
  - Always-newest behavior for Smooth/Balanced modes: would discard valid short bursts and can create visible microstutter.
  - Primary DXGI_PRESENT_DO_NOT_WAIT: no Duwn-specific failure justified reintroducing its retry complexity/regression risk.
  - Software 1 ms presentation polling or another thread: higher wakeup/synchronization cost than the existing DXGI clock.

Remaining risks:
  - Physical iPad latency and frame pacing remain unverified.
  - The relative mapper assumes the established 90 kHz UxPlay video RTP contract; a future transport with another rate must supply its rate rather than reuse this instance.
  - Actual low-end Intel/AMD iGPU performance, monitor refresh changes, and long-run occlusion/device-removal behavior still require physical coverage.

Ready for Phase 3: YES, after review of this report
```

Phase 2 stops here.
