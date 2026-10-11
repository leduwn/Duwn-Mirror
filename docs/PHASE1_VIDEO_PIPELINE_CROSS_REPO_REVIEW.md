# Duwn Mirror Phase 1 — Cross-Repository Video Pipeline Review

Date: 2026-10-11 (Asia/Saigon)

- Duwn Mirror commit: `0ca204be556bc1c4ef4ddc519965e0b9d37d931e` plus the preserved uncommitted dropdown and OBS/iPad diagnostic work.
- iPhoneMirror commit: `829c5919f25718efc91f3fae4ee1e808f193964d`.
- Research checkout: `D:\Projects\Duwn Mirror Research\iPhoneMirror-phase1-20261011`.
- iPhoneMirror license observed in the checkout: GPL-3.0. No implementation text was copied into Duwn.
- Scope: ingest, Media Foundation decode, compressed/decoded handoff, and presentation limits. No Phase 2 scheduling redesign, UX, OBS, color, version, release, or packaging change.

## A. Cross-Repository Algorithm Review

The review used the requested documentation and the implementation at the pinned iPhoneMirror revision. Documentation was treated as context; decisions below are based on the C++ paths.

| Algorithm | iPhoneMirror implementation | Duwn implementation | Trade-off | Decision |
| --- | --- | --- | --- | --- |
| Receive-thread handoff | `CaptureSession.cpp:1642-1715, 2230-2299` copies complete compressed samples into a FIFO consumed by a `std::jthread`. `VideoQueueBudget` bounds it to 12 samples and 64 MiB (`CaptureSession.h:191-208`), clears on overflow, waits for a keyframe, then resets the decoder. | `RtpReceiver.cpp:212-229` invokes its callback inline. `AirPlayEngine.cpp:104-111` forwards inline. `App.cpp:3736-3848` assembles and decodes while holding `m_decoder_mutex`. The direct IPC path also feeds under that mutex (`App.cpp:3697-3719`). | A worker prevents socket-read stalls but adds a compressed backlog, cancellation/generation work, and reference-frame recovery requirements. | **Keep synchronous for Phase 1.** The available physical iPhone session had 60–61 AU/decode/present FPS, queue depth 0, decoder p50 about 0.5–0.6 ms, and no MFT failures. That is insufficient evidence for another queue/thread. |
| Compressed queue overflow | `VideoQueueBudget::admit`, `CaptureSession.cpp:703-769`, never drops arbitrary inter frames and continues decoding them. On capacity failure it clears, drops until a recoverable keyframe, and marks the decoder for reset. Tests are at `CoreTests.cpp:1803-1820`. | RTP assembly is bounded to the current AU; decoded output is bounded separately. There is no complete-AU backlog in the active AirPlay path. HEVC fragment loss aborts the incomplete AU in `HevcRtpAssembler.cpp`; H.264 does the equivalent in `VideoDecoder.cpp:200-299`. | A new FIFO needs both frame and byte bounds plus a protocol-valid recovery action. UxPlay/AirPlay currently exposes no proven on-demand keyframe request contract here. | **Do not adopt without measured receiver blockage.** If later evidence shows backlog, adapt the reference policy rather than dropping compressed P frames. |
| MFT `ProcessInput` backpressure | `MediaFoundationDecoder.cpp:1801-1814` retries `MF_E_NOTACCEPTING`, receiving one output each iteration, then drains all immediately available output. | `MFVideoDecoder.cpp:596-643` calls `DrainOutput()` on `MF_E_NOTACCEPTING`, retries input, records timing only after accepted input, then drains again. `DrainOutput()` already consumes until `MF_E_TRANSFORM_NEED_MORE_INPUT` (`MFVideoDecoder.cpp:736-1040`). | iPhoneMirror's explicit loop is robust if a transform alternates acceptance and one-output progress. Duwn's `DrainOutput()` empties the transform in one call, so a second retry loop does not add behavior for its synchronous MFT path. | **Keep.** No lost-output case was found in Duwn's synchronous path. |
| MFT output drain and stream changes | `receive_output`, `MediaFoundationDecoder.cpp:1632-1705`, loops through `MF_E_TRANSFORM_STREAM_CHANGE`, reselects the output type, and returns one owned frame. `decode_once`, lines 1792-1815, collects every available frame into a vector. | `MFVideoDecoder::DrainOutput`, `MFVideoDecoder.cpp:736-1040`, stays in a loop across stream change and only stops on `NEED_MORE_INPUT` or failure. Each successful output invokes the frame callback; no single "latest output" variable overwrites earlier frames. | Replacing a decoder drain with a one-frame return would lose burst output. Both current implementations avoid that bug. | **Keep.** |
| Output sample ownership | `MediaFoundationDecoder.cpp:1650-1659` immediately adopts transform-provided event/sample references with COM smart pointers on success, stream change, and failure paths. | `MFVideoDecoder.cpp:743-804, 859-920` releases events, adopts transform-provided samples, and stores `out_sample` in `VideoFrame::sample_retention` beside the DXGI texture. | Retaining only the texture can let the MFT recycle its surface while the renderer still uses it. | **Keep Duwn.** The frame explicitly retains the producing sample through presentation. |
| Delayed output correlation | iPhoneMirror uses the decoded sample timestamp and maps it back to the capture-side receive time in `CaptureSession.cpp:1909-1958`. | The current dirty tree uses bounded exact-sample-time correlation in `DecoderInputTiming.h` and `MFVideoDecoder.cpp:556-643, 868-870`; reset, fallback, and flush clear it (`MFVideoDecoder.cpp:85-86, 1040-1056`). Regression coverage is in `test_frame_timing.cpp`. | Guessing from the most recent input misattributes delayed or reordered output. Bounded exact lookup may report unavailable when ambiguous or evicted, which is safer than false latency. | **Keep Duwn.** This is already the correct Phase 1 mechanism. |
| Flush, reconfigure, and fallback | iPhoneMirror drains on explicit end-of-stream and resets decoder state through its candidate lifecycle (`MediaFoundationDecoder.cpp:1818-1842`). | Duwn clears timing before flush, drops the transform and upload surfaces on reset, increments format generation, and reinitializes (`MFVideoDecoder.cpp:1040-1056`). Scheduler generation checks reject old frames. | Publishing old-generation output after rotation/reconnect causes stale geometry or resource use. | **Keep.** No stale-output path was confirmed in the audited flow. |
| Asynchronous MFT execution | iPhoneMirror handles `METransformNeedInput`, `METransformHaveOutput`, and drain completion (`MediaFoundationDecoder.cpp:1708-1753, 1792-1799, 1828-1838`). | Duwn detects and unlocks an asynchronous MFT (`MFVideoDecoder.cpp:142-170`) but `Feed`/`DrainOutput` use the synchronous `ProcessInput`/`ProcessOutput` protocol. Current Intel H.264 and HEVC probes selected synchronous MFTs. | Full async support is substantial; rejecting async candidates may force software decode on hardware where async is the only accelerator. Neither behavior was tested on such a GPU. | **Defer, recorded risk.** Do not claim async-MFT compatibility and do not change candidate selection without NVIDIA/AMD/Qualcomm coverage. |
| `MF_MT_VIDEO_NO_FRAME_ORDERING` | Set to `TRUE` on the input media type unless `IPHONE_MIRROR_ALLOW_FRAME_REORDERING` is set (`MediaFoundationDecoder.cpp:1265-1280`). The code applies it to both supported codecs; there is no profile/B-frame guard. The document's safety claim is based on its ordered QuickTime stream and historical PTS observations. | Duwn does not set this attribute (`MFVideoDecoder::SetInputType`, `MFVideoDecoder.cpp:319-344`). Input samples have explicit PTS and delayed output is correlated by output sample time. A direct-encoder test guarantees no B frames only for that controlled direct source, not for all AirPlay H.264/HEVC sources. | It can reduce decoder reorder delay for streams that never require reordering; forcing it on B-frame/dependent streams can produce wrong order or corruption. | **Keep current policy.** No safe codec/profile/source gate and no controlled physical A/B were available. |
| Decoded mailbox | `CaptureSession::next_render_frame`, `CaptureSession.cpp:1084-1100`, returns the immutable latest `shared_ptr`; a new decode atomically replaces it at lines 1945-1958. Slow consumers skip only complete decoded frames. | `FrameScheduler.cpp:488-570` already bounds the production decoded queue to 1–3 frames according to policy and drops only complete decoded frames. It also rejects stale generations and accounts for every explicit drop. | A second mailbox would duplicate ownership, obscure drop accounting, and add another latency policy. | **Keep Duwn's single decoded queue.** |
| D3D manager and decoded texture path | iPhoneMirror creates a decoder device/manager and sends `MFT_MESSAGE_SET_D3D_MANAGER` (`MediaFoundationDecoder.cpp:1135-1178`). `IMFDXGIBuffer` is inspected at lines 1517-1529. Its production handoff then copies/maps to CPU NV12/P010 (`MediaFoundationDecoder.cpp:1545-1625`); an experimental shared-texture path exists but the implementation does not call it from `copy_dxgi_output`. | Duwn passes the renderer's MF device manager to the decoder (`MFVideoDecoder.cpp:176-184`), extracts the native texture/subresource (`MFVideoDecoder.cpp:877-920`), retains the sample, and sends that same GPU resource to the video processor. The CPU path uploads into a bounded texture pool only for system-memory decoder output. | Shared-device zero-copy avoids readback and cross-device keyed-mutex stalls. It requires correct sample lifetime and serialized immediate-context use. | **Keep Duwn.** This is a concrete architectural advantage; no CPU readback or I420 intermediary was added. |
| D3D synchronization | iPhoneMirror enables multithread protection on its decoder device (`MediaFoundationDecoder.cpp:1151-1158`) and documents/implements guarded shared resources; its source comments record a cross-device keyed-mutex rotation stall (`MediaFoundationDecoder.cpp:1545-1551`). | Duwn uses one D3D device, retains decoder surfaces, and locks `D3D11Device::ContextMutex()` around immediate-context rendering/upload work (`MFVideoDecoder.cpp:973`, `VideoRenderer.cpp:635-696`). It does not wait on DXGI while holding that mutex (`VideoRenderer.cpp:614-645`). | One device and one context lock avoid cross-device ownership transfer. Lock duration can still matter on CPU-upload/export paths. | **Keep.** No lock-wait regression was measured in the physical session. |
| Presentation limit | iPhoneMirror calls `IDXGIDevice1::SetMaximumFrameLatency(1)` (`D3D11PreviewRenderer.cpp:546-547`) and uses nonblocking or vsync presentation depending on its mode. | Duwn creates a two-buffer flip-discard swapchain, calls `IDXGISwapChain2::SetMaximumFrameLatency(1)`, uses the waitable object, and calls `Present(0, flags)` (`VideoRenderer.cpp:92-126, 614-723`). | Both bound compositor queuing. Changing pacing policy is Phase 2 and needs physical frame-time data. | **Keep.** |
| Timing evidence | iPhoneMirror records queue and decoder behavior in logs/tests but its historical figures are not measurements of this Duwn tree. | The preserved `DUWN_FRAME_TIMING=1` work records RTP/AU/decode/enqueue/dequeue/present intervals and correlated durations in `LatencyTelemetry.h/.cpp` and call sites across receiver, decoder, scheduler, and renderer. | Local receive-to-Present-return stops before DWM scanout and is not end-to-end latency. | **Keep and use for controlled follow-up.** |

### Requested source path trace

The active Duwn route is:

`RtpReceiver::RecvLoop` (`RtpReceiver.cpp:212-229`) → `AirPlayEngine::OnVideoRtp` (`AirPlayEngine.cpp:104-111`) → `App::OnVideoData` (`App.cpp:3736-3848`) → `VideoDecoder::FeedRtp` / `HevcRtpAssembler::FeedRtp` → `VideoDecoder::FeedAccessUnit` (`VideoDecoder.cpp:105-160`) → `MFVideoDecoder::Feed` / `DrainOutput` (`MFVideoDecoder.cpp:516-1040`) → `FrameScheduler::PushFrame` (`FrameScheduler.cpp:480-640`) → `App::OnFramePresent` → `VideoRenderer::Present` (`VideoRenderer.cpp:457-786`). `LatencyTelemetry` observes this route without changing its ownership model.

## B. Performance Baseline

### Physical evidence available

This was not a controlled Phase 1 before/after run. It is an existing physical log from 2026-10-11 around 12:31 for `iPhone14,2` (iPhone 13 Pro), 1920×896 coded / 1920×888 visible, Microsoft H.264 Decoder MFT, NV12, hardware/D3D11-aware/zero-copy, and Intel Graphics (`0x8086`, device `0xa7ac`). The source reported 60 FPS.

Representative stable one-second windows in `%LOCALAPPDATA%\Duwn Mirror\Logs\duwn-mirror.log` showed:

- AU: 60–61 FPS; decoder output: 60–61 FPS; successful presentation: 60–62 FPS.
- Decoder call p50: about 0.51–0.60 ms; p95: about 0.66–1.34 ms.
- Decoded queue depth: 0; queue residence p95: about 1.13–1.59 ms.
- `ProcessInput`: 60–61 successes/s, 0 failures. `ProcessOutput`: one success per input plus expected `NEED_MORE_INPUT`, 0 failures and 0 stream changes in the sampled windows.
- Hardware zero-copy path active. No decoded backlog was observed.

Limitations: this log was not captured with the new `[FRAME TIMING]` distributions enabled for a controlled trial. Decoder p99, AU→output p99, RTP→Present p95/p99, process CPU/GPU load, and working set are unavailable. Later source-disconnected windows and cumulative old drop counters were excluded. The log does not prove the synchronous callback is harmless on every GPU or during rotation.

The older iPad Pro M4 evidence remains historical only: the 2026-10-04 log identified `iPad16,3`, a hardware Microsoft H.264 MFT, NV12 GPU textures, rotation, and encoder-reported 60 FPS. It lacks a reproducible controlled timing run and cannot be used as an A/B result.

### Synthetic evidence from this verification

The unit benchmark produced 60.00 AU/decode/present FPS for five measured seconds. Presented-frame intervals were p50 16.54 ms, p95 17.95 ms, p99 18.09 ms, maximum 18.29 ms, with zero gaps above 20/25/33/50/100 ms. Queue depth stayed 0. Reported process CPU was 0.02% across 12 cores and GPU use was below 5% on the synthetic D3D11 path. These are synthetic process-local results, not AirPlay or physical-device latency.

The capture/persistence integration benchmark averaged 5.95 ms per 1080p presentation call over ten frames, with a 31.23 ms maximum. Ten frames are a functional acceptance check, not a frame-pacing distribution.

## C. Changes Implemented

Root cause / performance bottleneck: **none confirmed by the source comparison and available measurements.** Duwn already contains the complete MFT drain, bounded decoded handoff, output lifetime retention, generation handling, and GPU-native path that Phase 1 requires. The physical iPhone sample did not show decoder backlog or MFT failures.

Algorithm learned from iPhoneMirror: a bounded complete-compressed-sample FIFO with a dedicated decoder worker is a valid protection for a blocking transport thread when overload recovery can drop through a keyframe and reset decoder state. Its async-MFT event pump also demonstrates the work needed for true asynchronous-transform support.

Why applicable to Duwn: only conditionally. The active AirPlay receiver currently decodes on the socket callback, so measured long decoder calls or socket backlog could justify such a handoff.

Duwn-specific implementation: **no production algorithm change in this phase.** Adding a worker without a measured bottleneck would add latency and recovery complexity. Enabling no-frame-ordering without a verified source profile would risk corrupted/reordered output. The existing zero-copy path was preserved.

Files changed by this Phase 1 task:

- `docs/PHASE1_VIDEO_PIPELINE_CROSS_REPO_REVIEW.md` only.

Tests added: none. Existing targeted timing, generation, queue-bound, fallback, HEVC assembly, and GPU-tier tests already cover the retained mechanisms. No behavior was added that warranted a duplicate test.

## D. Results

There is no valid before/after performance delta because production behavior was intentionally unchanged.

| Metric | Before | After | Interpretation |
| --- | --- | --- | --- |
| Decode p95/p99 | Physical iPhone p95 roughly 0.66–1.34 ms; p99 unavailable | Same implementation | No claimed improvement. |
| RTP→Present p95/p99 | Unavailable in controlled physical run | Unavailable | Local timing instrumentation exists; no end-to-end claim. |
| Queue age | Physical iPhone queue depth 0, residence p95 roughly 1.13–1.59 ms | Same implementation | No evidence of compressed backlog. |
| FPS | Physical iPhone 60–61 AU/decode, 60–62 present in representative windows | Same implementation | No regression introduced. |
| Stutter gaps | Physical p99 distribution unavailable; synthetic >20 ms = 0 | Same implementation | Synthetic only. |
| CPU/GPU | Physical unavailable; synthetic CPU 0.02%, GPU <5% | Same implementation | Not a physical A/B. |
| Memory | Unavailable | Unavailable | No new queue or persistent allocation added. |

## E. Rejected Ideas

1. **Dedicated compressed decoder worker:** rejected for Phase 1 because the available physical sample shows no decoder throughput deficit or queue growth. Duwn also lacks a proven AirPlay keyframe-request/recovery contract for a new compressed overflow policy.
2. **Unconditional `MF_MT_VIDEO_NO_FRAME_ORDERING`:** rejected because iPhoneMirror's current code has no codec/profile guard, while Duwn accepts AirPlay H.264 and HEVC whose reorder requirements were not captured. The direct encoder's no-B-frame guarantee cannot be generalized to those sources.
3. **Second decoded mailbox:** rejected because `FrameScheduler` already owns a bounded 1–3-frame decoded queue with generation and drop accounting.
4. **iPhoneMirror-style CPU materialization:** rejected because Duwn already retains and renders the native `IMFDXGIBuffer` texture on the shared D3D device. A readback would increase CPU traffic and latency.
5. **Cross-device keyed-mutex handoff:** rejected because Duwn's shared-device route is simpler and avoids the rotation stall documented in iPhoneMirror's source comments.
6. **Async-MFT implementation or rejection policy:** deferred. Duwn's current synchronous path is verified on the available Intel MFTs, but a correct general implementation needs event-pump lifecycle tests and non-Intel hardware. Silently selecting an async-only MFT remains a compatibility risk; changing the fallback policy without hardware coverage could reduce FPS.
7. **Presentation policy redesign:** deferred to Phase 2 as requested. Maximum frame latency remains 1 and the current scheduler/present behavior is unchanged.

## F. Verification

- Build: **PASS** — Release `duwn-mirror`, `duwn-unit-tests`, and `duwn-capture-persistence-test` targets.
- Unit: **PASS — 444/444**, including delayed/reordered timing correlation, bounded reset behavior, HEVC boundary timing, decoded queue bounds, format generations, GPU tiers, and synthetic 60 FPS pacing. Run with `DUWN_FRAME_TIMING=1`.
- Integration: **PASS — 5/5**, including WGC binding, synthetic motion presentation, persistence, and the ten-frame 1080p acceptance check.
- Hit-test: **PASS** — installed executable; placeholder forwarding, draggable center, resize edges, toolbar presence, and clean shutdown. The script's temporary 50-pixel window move was restored to the observed pre-run `(400, 200)` settings.
- Diff check: **PASS**.
- Git status: expected uncommitted dropdown fixes, OBS/iPad diagnostics, and this report; no reset, clean, stash, commit, push, tag, release, or version change.
- Physical iPad: **NOT RUN** in this phase; only historical evidence available.
- Physical iPhone: **historical/current-day log reviewed, not a controlled Phase 1 A/B**.
- Low-end GPU: Intel integrated GPU covered by build/unit GPU probes and the reviewed physical log; no controlled long-run physical A/B.

## G. Decision

Phase 1 completed: **PARTIAL**.

Confirmed improvements: no new performance improvement is claimed. The review confirms that Duwn's complete output drain, decoded-frame ownership, exact timestamp correlation, bounded decoded queue, and shared-device GPU-native handoff should remain.

Remaining problems:

- No controlled physical iPad/iPhone sessions with `DUWN_FRAME_TIMING=1`, matched content, and before/after builds.
- Callback duration, decoder-mutex wait, separate `ProcessInput`/`ProcessOutput` durations, kernel/socket ingress interval, and compressed backlog are not all independently measured.
- Async Media Foundation transforms are detected but do not have a dedicated event-driven execution path.
- Physical CPU/GPU/working-set and p99 distributions are missing.

Regression risks: no production code changed in this phase. Existing risk remains on untested async-only MFTs and during unmeasured rotation/reconnect load. The preserved dirty tree still requires physical iPad, iPhone, audio, and capture acceptance before release.

Ready for Phase 2: **NO**. First collect controlled physical timing that either demonstrates receive-thread decoder blockage or clears it; only then choose synchronous optimization or a bounded complete-AU worker.
