# Receiver latency controls

Scope: built-in iPhone Screen Mirroring, existing AirPlay receiver, native
Media Foundation/D3D11 playback. No companion iOS application is required.

## Why this change

At the baseline, `Settings::streaming_mode` was neither loaded/saved nor applied
by `App`. The decoded queue always held up to three frames. The app's
`SchedulerConfig::max_queue_depth = 2` only applied to the experimental
PresentationClock path, which production does not use.

The software/no-DXGI-waitable path also waited on the frame event a second time
after consuming it, adding up to a 16 ms timeout to a frame already available.
This change removes that redundant wait; hardware DXGI pacing remains active.

The prior receiver audit is decisive for the transport configuration:

- [Phase 4](media-core-phase4-vsync-ab.md): `-vsync no` did not improve RTP
  forwarding; that flag affects UxPlay's local presentation branch.
- [Phase 5](media-core-phase5-sink-sync-ab.md): disabling synchronization on both
  forwarding sinks caused video overflows, an audio gap of 3.356 seconds,
  real underruns, and backlog recovery drops.
- [Phase 13](media-core-phase13-acceptance.md): the safe profile had an average
  T0–T7 video processing time of 2.83 ms in the documented hardware run.

The production `CurrentSafe` forwarding settings are preserved. Existing
development environment overrides are unchanged and should be absent during
production A/B testing (`DUWN_MEDIA_LATENCY_PROFILE`, in particular).

The 2.83 ms measurement starts at Duwn's local RTP receiver, **after UxPlay**.
It excludes iPhone capture/encode, the physical Wi-Fi path, UxPlay processing,
and scanout on the monitor. It does not prove low phone-to-display latency or
performance comparable to ApowerMirror/DouWan.

## Behavior

Video → Display Latency (Vietnamese: Video → Độ trễ hiển thị):

| Mode | Pending decoded frames | Selection at each display opportunity |
|---|---:|---|
| Balanced / Cân bằng | Up to 3 | Preserve fresh FIFO; select newest when oldest age exceeds 1.25× source frame interval |
| Fastest / Nhanh nhất | 1 | Replace the pending decoded image with the newest available image |
| Prefer smoothness / Ưu tiên mượt | Up to 3 | Preserve fresh FIFO until oldest age exceeds 2× source frame interval |
| Custom / Tùy chỉnh | 1–3 | User-selected age threshold: 5, 10, 16, 25, 40, 60, or 100 ms |

These are maximum pending counts, not a requirement to prefill the queue.
Frames are consumed as soon as the display is ready. The Custom threshold
chooses when to discard pending history; it is neither an added delay nor an
end-to-end latency guarantee. A single frame is always eligible for display,
including after a static/quiet period.

All dropping occurs after decode. Compressed H.264/HEVC reference frames still
reach the decoder in order. Selected resolution, requested FPS, crop, color,
scaling, and the audio path remain independent. Fastest trades retention of
every decoded image for immediacy during bursts; spatial image quality is not
reduced, but temporal smoothness can change.

A four-byte atomic snapshot transfers live settings to the decode/render
threads. Changing the mode does not disconnect Screen Mirroring or mutate the
scheduler's non-atomic configuration from the UI thread.

The default remains Balanced, preserving the previously tested delivery policy.
The optional JSON keys are backward compatible with schema v2:

```json
{
  "streaming_mode": 1,
  "custom_video_freshness_ms": 25,
  "custom_video_queue_frames": 2
}
```

Mode values are 0=Balanced, 1=Fastest, 2=Prefer smoothness, 3=Custom. Custom
fields apply only in mode 3. Existing files without the keys use Balanced,
25 ms, and 2 frames. Loaded values are clamped to supported bounds. New
`ReceiverDelivery` log lines show applied live changes; freshest-image drops
are counted separately from stale-age drops.

## Verification performed for this patch

- Six portable C++20 policy cases pass with `-Wall -Wextra -Werror`: source
  cadence, burst selection, custom boundary, invalid inputs, single-frame
  retention, and concurrent atomic updates.
- The actual scheduler source plus those cases and the existing scheduler suite
  were compiled/run on Linux with temporary Win32 event/clock and empty GPU
  interface stubs: **36/36 passed**. This checks queue logic, including a
  1,000-frame burst and live queue shrink. It does not validate Windows events,
  DXGI, Media Foundation, or hardware performance.
- Actual Settings and localization sources were compiled/run with isolated
  platform stubs: all four modes survived Save/Load, corrupt fields clamped,
  a v2 file without new fields retained defaults, and the English/Vietnamese
  tables contained all strings with the correct new labels.
- Full Windows application build, UI visual verification, real iPhone tests,
  audio/video continuity, and phone-to-monitor measurements are pending.

Portable policy command from the repository root:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pthread -Isrc tests/portable/streaming_policy_main.cpp -o streaming-policy-tests
./streaming-policy-tests
```

## Windows and device validation

```powershell
cmake --preset debug
cmake --build build/debug --config Debug
ctest --test-dir build/debug -C Debug --output-on-failure
```

Use the same phone, Windows PC, display refresh rate, content, and requested
resolution/FPS for comparisons. Record actual delivered resolution/FPS as well:
AirPlay resolution and frame-rate requests are advisory to the sender, as
documented by [UxPlay](https://github.com/FDH2/UxPlay#usage).

1. Verify both languages, dropdowns, scrolling at the minimum window size,
   runtime changes while connected, and persistence after restarting.
2. Start with Balanced and the original receiver quality. Compare Fastest
   while preserving actual source quality. Also try Custom at 1 frame/10 ms
   and 2 frames/25 ms. Do not raise the output resolution to infer source
   detail: output upscaling does not improve the received encoding.
3. Film the phone and PC together with another camera, preferably 120/240 FPS,
   during a repeatable visible event. Collect at least 30 paired events per
   case and report median/P95 phone-to-visible-display latency. Camera frame
   quantization and display refresh intervals limit measurement precision.
4. Record queue residence, decoded/rendered FPS, explicit drops, real audio
   underruns, and A/V offset. Compare Preview and Output separately. Repeat
   over Wi-Fi and wired USB networking, if available. Run the chosen policy
   for 30 minutes before promoting it to the default.

| Connection | Surface | Mode | Actual source W×H/FPS | Physical median | Physical P95 | Queue P95 | Audio gaps/underruns | Notes |
|---|---|---|---|---|---|---|---|---|
| Wi-Fi | Preview | Balanced / Fastest / Custom | | | | | | |
| Wi-Fi | Output | Balanced / Fastest / Custom | | | | | | |
| USB network | Preview | Balanced / Fastest / Custom | | | | | | |
| USB network | Output | Balanced / Fastest / Custom | | | | | | |

Each mode needs its own measurement row. If physical latency remains high
while queue residence is near zero, replacing this decoded-frame policy will
not remove the upstream delay. Measure UxPlay packet input → decrypted access
unit → RTP output next, and then assess a native decrypted-AU IPC bridge.
The repository currently contains native IPC foundations but no vendored
UxPlay source to build and verify such a producer in this patch.

Wi-Fi 5 GHz signal strength or a high bandwidth test alone cannot guarantee
minimal delay; capture, encoding, packet bursts, queues, composition, and scanout
all remain part of the path. Built-in Screen Mirroring does not give this
receiver the direct encoder-bitrate control available to a custom sender.
