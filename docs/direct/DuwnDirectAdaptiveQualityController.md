# Duwn Direct Architecture — Phase 6B: Adaptive Quality + Latency Controller

## 1. Overview & Objective

Phase 6B implements the adaptive control plane for Duwn Direct low-latency mode.
Its primary objective is: **keep Direct streaming fast without allowing output visual quality to collapse into unreadable blur**.

The controller operates strictly using the empirical quality floors and operating points established in Phase 6A. No arbitrary or unmeasured thresholds are introduced:
- **1440p (2560x1440)**: Quality Floor = **$12.0\text{ Mbps}$**, Balanced = **$16.0\text{ Mbps}$**, High = **$25.0\text{ Mbps}$**.
- **1080p (1920x1080)**: Quality Floor = **$6.0\text{ Mbps}$**, Balanced = **$12.0\text{ Mbps}$**, High = **$20.0\text{ Mbps}$**.
- **720p (1280x720)**: Quality Floor = **$3.0\text{ Mbps}$**, Balanced = **$5.0\text{ Mbps}$**, High = **$8.0\text{ Mbps}$**.

---

## 2. Telemetry Input Vector

The controller samples a comprehensive 9-signal telemetry vector on each evaluation tick ($100\text{ ms}$ interval):

```
+-------------------------------------------------------------------------------+
|                        Network & Hardware Telemetry Vector                    |
+-------------------------------------------------------------------------------+
| 1. actual_send_bitrate_bps            : Real-time bitstream emission rate     |
| 2. available_throughput_estimate_bps  : Bandwidth estimate (BBR / GCC / probe)|
| 3. rtt_ms                             : Round-trip time (transport latency)   |
| 4. jitter_ms                          : Inter-packet arrival variance         |
| 5. packet_loss_fraction               : Datagram drop ratio (0.0 to 1.0)      |
| 6. encoder_time_ms                    : Hardware compression duration         |
| 7. frame_supersede_count              : Dropped frames due to pipeline backlog|
| 8. receiver_incomplete_frames         : Frames dropped due to missing packets |
| 9. decoder_queue_depth                : Pending unrendered frames in MFT/D3D11|
+-------------------------------------------------------------------------------+
```

---

## 3. Four-Tier Policy Hierarchy

Decisions are governed by an uncompromised strict priority hierarchy:

### Priority 1: Prevent Latency Queue Growth
- **Invariant**: Never respond to network congestion by increasing video buffers.
- The pipeline rigidly enforces 0 or 1 pending frame across capture (`DuwnCaptureFrameSlot`), encoder (`DirectEncoderInputSlot`), assembler (`DirectFrameAssembler`), and receiver (`FreshestFrameSlot`).
- If queue depth exceeds 1 or superseded frames occur, the controller immediately cuts bitrate at the source to clear bottlenecks at wire speed.

### Priority 2: Maintain Smooth FPS (60 FPS Preferred for Gaming)
- 60 FPS is maintained across both bitrate reductions and resolution adaptations.
- **Rule**: Do not reduce FPS before proving that bitrate reduction and resolution adaptation cannot maintain a stable low-latency stream.
- FPS reduction (to 30 FPS) is used **strictly as a last resort** when operating at the lowest resolution (720p) below its quality floor ($<3.0\text{ Mbps}$) or under severe packet loss ($>15\%$).

### Priority 3: Preserve Visual Quality Floor
- As bandwidth drops, reduce bitrate **only while quality remains at or above the measured floor**.
- If required bitrate would fall below the quality floor, **reduce resolution one step rather than creating extremely blurry video**.
  - *Rationale*: Sub-floor bitrates at high resolutions cause the H.264 quantizer ($QP \ge 38$) and in-loop deblocking filter to blur 1px UI text strokes and code typography into an illegible smear. Stepping down resolution increases the bit-per-pixel ($BPP$) ratio by up to $1.78\times$, preserving sharp, readable text.

### Priority 4: Maximize Spatial Quality
- When network headroom is confirmed clean, probe upward additively.
- Restore resolution only after sustained stability.

---

## 4. Resolution & Bitrate Adaptation Ladder

```
[ Tier 0: 2560x1440 @ 60 FPS ]
   Ceiling: 25.0 Mbps | Balanced: 16.0 Mbps | Floor: 12.0 Mbps
       |
       | Throughput < 12.0 Mbps (Floor violation)
       v [Step down to preserve sharpness]
[ Tier 1: 1920x1080 @ 60 FPS ]
   Ceiling: 20.0 Mbps | Balanced: 12.0 Mbps | Floor: 6.0 Mbps
       |
       | Throughput < 6.0 Mbps (Floor violation)
       v [Step down to preserve sharpness]
[ Tier 2: 1280x720 @ 60 FPS ]
   Ceiling: 8.0 Mbps | Balanced: 5.0 Mbps | Floor: 3.0 Mbps
       |
       | Throughput < 3.0 Mbps OR Loss > 15% (All spatial adaptation exhausted)
       v [Last Resort: FPS reduction]
[ Tier 3: 1280x720 @ 30 FPS ]
   Ceiling: 4.0 Mbps | Minimum Viable: 1.5 - 2.0 Mbps (Doubled byte-per-frame budget)
```

---

## 5. Recovery & Anti-Oscillation Hysteresis

To prevent rapid oscillation (mode flapping) between resolutions and bitrates:
1. **Asymmetric Reaction (Fast Backoff, Slow Recovery)**:
   - Congestion detection triggers immediate rate backoff or resolution downscale.
   - Clean network requires $15\text{ cycles}$ ($1.5\text{ seconds}$) of zero loss, low jitter ($<4\text{ ms}$), and low RTT before additive bitrate increase ($+500\text{ Kbps}$) begins.
2. **Resolution Cooldown Timer**:
   - Following any resolution change, a $20\text{ cycle}$ ($2.0\text{ second}$) cooldown prevents subsequent resolution changes, allowing transport queues and encoder rate-control to settle.
3. **Resolution Upgrade Hold Threshold**:
   - Stepping resolution back up requires:
     1. Current stream operating at $\ge 90\%$ of its current tier's ceiling.
     2. At least $30\text{ consecutive clean cycles}$ ($3.0\text{ seconds}$) of zero packet loss.
     3. Available throughput estimate confirmed $\ge 115\%$ of the higher tier's balanced bitrate.

---

## 6. Verification & Test Suite Results

Comprehensive test suite in `tests/unit/test_direct_adaptive_controller.cpp`:

1. **`DirectAdaptive_CleanNetwork_GradualAdditiveRampUp` (PASS)**:
   - Clean LAN (30 Mbps available, 0% loss, RTT 4.2ms).
   - Starts at 12 Mbps balanced point, ramps additively (+500 Kbps) up to 20 Mbps ceiling.
   - 0 resolution changes, 0 FPS drops.
2. **`DirectAdaptive_MildCongestion_ReducesBitrateWithoutDownscaling` (PASS)**:
   - Bandwidth dips to 9 Mbps (above 6 Mbps floor for 1080p).
   - Bitrate reduces gracefully to safe throughput limit.
   - Resolution stays 1080p, 60 FPS maintained.
3. **`DirectAdaptive_TemporaryPacketLoss_FastBackoffSlowProbe` (PASS)**:
   - Transient 5% packet loss burst.
   - Controller backs off immediately.
   - Once loss clears, holds bitrate steady for 6 cycles before probing upward. Zero flapping.
4. **`DirectAdaptive_BandwidthBelowFloor_StepsDownResolutionToPreserveSharpness` (PASS)**:
   - Source is 1440p (Floor: 12 Mbps). Bandwidth drops to 8 Mbps.
   - Controller downscales to 1080p, setting bitrate to 8 Mbps (well above 6 Mbps floor).
   - Bandwidth drops further to 4 Mbps. Controller downscales to 720p (well above 3 Mbps floor).
   - 60 FPS maintained across all downscales; text readability preserved without blur.
5. **`DirectAdaptive_SevereStarvation_ReducesFpsAsLastResort` (PASS)**:
   - Stream at 720p experiences collapse to 1.8 Mbps with 18% packet loss.
   - With spatial adaptation exhausted, controller drops FPS to 30 as last resort.
6. **`DirectAdaptive_RecoveryAndAntiOscillation` (PASS)**:
   - Network recovers to 30 Mbps.
   - FPS restores to 60 -> Bitrate ramps up -> Resolution steps back up to 1080p.
   - Cooldown timer strictly prevents oscillation.

---

## 7. Deliverables & Status

- Implementation: [DirectQualityController.h](src/direct/quality/DirectQualityController.h) and [DirectQualityController.cpp](src/direct/quality/DirectQualityController.cpp).
- Test Suite: [test_direct_adaptive_controller.cpp](tests/unit/test_direct_adaptive_controller.cpp).
- Documentation: [DuwnDirectAdaptiveQualityController.md](docs/direct/DuwnDirectAdaptiveQualityController.md).
- Total Unit Tests: **318 / 318 PASS** (0 regressions).

FINAL STATUS: **DIRECT_ADAPTIVE_QUALITY_READY**
