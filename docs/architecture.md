# Duwn Mirror — Architecture

## Goal

Receive iPhone/iPad screen + audio via AirPlay, render on Windows with stable
60 FPS frame pacing, correct stereo audio, sub-20ms A/V offset, and low CPU
overhead. Primary use case: PUBG Mobile → OBS Studio / TikTok Live Studio.

---

## Top-Level Pipeline

```
iPhone / iPad
      │
      │  AirPlay (Wi-Fi LAN)
      ▼
┌─────────────────────────────────┐
│  AirPlay Engine (UxPlay)        │
│  Discovery · Pairing · Crypto   │
│  RTP · NTP · Session            │
│  sidecar process: uxplay.exe    │
│  flags: -vrtp -artp             │
│  bound to 127.0.0.1 only        │
└──────────┬──────────────────────┘
           │ RTP/UDP (loopback, dynamic ports)
           │ Video: compressed H.264
           │ Audio: AAC / AAC-ELD / PCM (detected per session)
           ▼
┌─────────────────────────────────────────────────────────┐
│  Duwn Media Engine (native C++)                         │
│                                                         │
│  ┌──────────────┐   ┌──────────────┐                   │
│  │ RtpReceiver  │   │ RtpReceiver  │                   │
│  │ (video)      │   │ (audio)      │                   │
│  └──────┬───────┘   └──────┬───────┘                   │
│         │                   │                           │
│  ┌──────▼───────┐   ┌──────▼───────┐                   │
│  │ JitterBuffer │   │ JitterBuffer │                   │
│  │ (video)      │   │ (audio)      │                   │
│  └──────┬───────┘   └──────┬───────┘                   │
│         │                   │                           │
│  ┌──────▼───────┐   ┌──────▼───────┐                   │
│  │ VideoDecoder │   │ AudioEngine  │                   │
│  │ MF+D3D11 HW  │   │ decode once  │                   │
│  └──────┬───────┘   └──────┬───────┘                   │
│         │                   │                           │
│         │          ┌────────▼────────┐                  │
│         │          │ AudioRingBuffer │                  │
│         │          └────────┬────────┘                  │
│         │                   │                           │
│  ┌──────▼───────────────────▼───────┐                   │
│  │          MasterClock             │                   │
│  │      AvSynchronizer              │                   │
│  │      DriftController             │                   │
│  └──────┬───────────────────┬───────┘                   │
│         │                   │                           │
│  ┌──────▼───────┐   ┌───────▼──────┐                   │
│  │FrameScheduler│   │ WasapiOutput │                   │
│  └──────┬───────┘   └──────────────┘                   │
│         │                                               │
│  ┌──────▼───────┐                                       │
│  │ VideoRenderer│                                       │
│  │ D3D11/DXGI   │                                       │
│  └──────┬───────┘                                       │
└─────────┼───────────────────────────────────────────────┘
          │
    ┌─────▼──────────────────────┐
    │  Output                    │
    │  ┌──────────────────────┐  │
    │  │ Duwn Mirror Output   │  │  ← Window Capture (TikTok Live Studio)
    │  │ borderless window    │  │
    │  └──────────────────────┘  │
    │  ┌──────────────────────┐  │
    │  │ SharedTexture        │  │  ← OBS source plugin (Phase 2)
    │  └──────────────────────┘  │
    └────────────────────────────┘
```

---

## Thread Model

| Thread | Owns | Notes |
|--------|------|-------|
| UI Thread | WinUI 3 window, control messages | No decode, no network |
| AirPlay Supervisor | Child process lifetime, pipe I/O | Restarts UxPlay on crash |
| Video RTP Thread | UDP recv, sequence, JitterBuffer push | SPSC → decode thread |
| Video Decode Thread | MFTransform, D3D11 texture pool | Signals FrameScheduler |
| Video Render Thread | FrameScheduler, IDXGISwapChain1 | Waitable timer, QPC |
| Audio RTP Thread | UDP recv, JitterBuffer push | SPSC → audio processing |
| Audio Processing Thread | Decode AAC→PCM, resample if needed | → AudioRingBuffer |
| WASAPI Render Thread | IAudioClient3, ring buffer pull | Event-driven |
| Metrics Thread | Aggregate atomics → snapshot | 1 Hz poll, no lock on hot path |

All hot-path cross-thread transfers use **bounded SPSC lock-free queues** or
atomic ring buffers. No global mutex on the media path.

---

## Video Pipeline (zero-copy target)

```
H.264 NAL units (from RTP)
        ↓
IMFSourceReader / MFCreateSampleFromMediaBuffer
        ↓  (MF pipeline, D3D11-backed buffers)
IMFTransform (H.264 decoder, D3D_HARDWARE)
        ↓
IMFSample → NV12 ID3D11Texture2D (decoder output)
        ↓
ID3D11VideoProcessor (NV12 → BGRA, in GPU)
   OR  HLSL shader (NV12 → BGRA, in GPU)
        ↓
ID3D11Texture2D (BGRA, final)
        ↓
IDXGISwapChain1::Present
```

No `Map()`/`memcpy()`/`Unmap()` per frame for render path.
CPU readback only for diagnostics export (on demand).

---

## Audio Pipeline

```
AirPlay AAC / AAC-ELD / ALAC (detect per session)
        ↓
AudioEngine: decode ONCE → PCM float32
        ↓
AudioConverter: resample to 48 kHz stereo IF needed (one pass)
        ↓
AudioRingBuffer (bounded, float32, 48 kHz, 2ch)
        ↓
WasapiOutput: IAudioClient3 shared mode, event-driven
        ↓
Windows audio graph / speakers / headphones (monitor branch)
```

Internal format: `float32 / 48 000 Hz / 2 ch`.
No re-encoding inside Duwn Mirror.

---

## A/V Synchronization

```
Video PTS (from RTP timestamp + NTP anchor) ──┐
                                               ├─→ MasterClock (QPC-based)
Audio PTS (from RTP timestamp + NTP anchor) ──┘         │
                                                         ▼
                                               AvSynchronizer
                                               - tracks av_offset
                                               - feeds DriftController
                                                         │
                                          ┌──────────────┘
                                          │
                               ┌──────────▼──────────┐
                               │  DriftController     │
                               │  soft correction:    │
                               │  - resample ratio    │
                               │  - frame schedule    │
                               └─────────────────────┘
```

Target: `av_offset_avg < 20 ms`. No aggressive pitch-shifting.

---

## Streaming Modes

| Mode | Jitter Buffer | Late Frame Policy | Use |
|------|--------------|-------------------|-----|
| Smooth Live (default) | 2–3 frames adaptive | Drop if > 2 frames late | Livestream PUBG |
| Low Latency | ~1 frame | Drop immediately | Latency-sensitive |
| Compatibility | Larger | Keep | Problematic GPU/driver |

---

## Session State Machine

```
Idle
  │ app start
  ▼
Advertising  (mDNS broadcast, UxPlay running)
  │ iPhone connects
  ▼
Connecting
  │ session established, formats negotiated
  ▼
Streaming ◄──────────────────────────┐
  │ disconnect / error / rotate      │
  ▼                                  │
Reconnecting ──── retry ────────────┘
  │ timeout or user stop
  ▼
Idle
```

---

## D3D11 Device Sharing

One `ID3D11Device` (with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`) shared across:
- Video decoder (via `IMFDXGIDeviceManager`)
- Video processor
- Renderer / SwapChain
- SharedTexture (for OBS plugin, Phase 2)

Device-removed / driver-reset → full resource recreation without app restart.

---

## OBS Integration (Phase 2)

```
DUWN Mirror app
      │
      │ Named Pipe (control + metadata)
      │ Shared D3D11 texture handle (video)
      │ Named Pipe PCM blocks (audio)
      ▼
obs-plugin/DuwnMirrorSource
      │
      ├─ obs_source_frame (video)
      └─ obs_source_audio (audio)
```

---

## Capture Window (TikTok Live Studio / OBS Window Capture)

`DUWN Mirror Output` window:
- Borderless, black background
- No toolbar, no cursor overlay, no stats
- Correct aspect ratio (Fit default, Fill/Crop optional)
- Separate from `DUWN Mirror Control` window

---

## Milestone Plan

| # | Name | Deliverable |
|---|------|-------------|
| 0 | Proof of Concept | AirPlay→video visible + audio audible on Windows |
| 1 | Native Video Pipeline | MF+D3D11 HW decode, FrameScheduler, correct AR |
| 2 | Professional Audio | WASAPI, ring buffer, 48k stereo, timestamps |
| 3 | A/V Sync | MasterClock, AvSynchronizer, DriftController, stress test |
| 4 | Production UI | WinUI 3, Settings, Diagnostics, Reconnect |
| 5 | Capture Optimization | Clean output window, OBS source plugin |
| 6 | Virtual Audio | Kernel driver, DUWN Mirror Audio device |

No milestone may start until the previous one passes acceptance criteria.
