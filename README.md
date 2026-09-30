# Duwn Mirror

AirPlay receiver for Windows. Designed for PUBG Mobile → iPhone → OBS Studio / TikTok Live Studio.

## What it does

1. Starts an AirPlay receiver on your Windows PC (LAN only)
2. iPhone: Control Center → Screen Mirroring → **Duwn Mirror**
3. Phone screen + stereo game audio appear on Windows
4. Capture with OBS or TikTok Live Studio to livestream

## What it does NOT do

- No remote control of the iPhone
- No cloud, no account, no telemetry
- No fake 60fps (renders actual source FPS)
- No audio processing (stereo preserved as-is)

## Requirements

- Windows 10 22H2+ or Windows 11
- DirectX 11-capable GPU with hardware H.264 decode (NVIDIA / AMD / Intel)
- UxPlay sidecar (see `docs/build.md`)
- Bonjour SDK for Windows (mDNS)
- iPhone/iPad on the same Wi-Fi network

## Building

See [`docs/build.md`](docs/build.md).

```powershell
cmake --preset debug
cmake --build build/debug
```

## Architecture

See [`docs/architecture.md`](docs/architecture.md).

## Milestones

| # | Name | Status |
|---|------|--------|
| 0 | Proof of Concept — AirPlay → video + audio on Windows | 🚧 In progress |
| 1 | Native Video Pipeline — MF + D3D11 HW decode, FrameScheduler | ⬜ |
| 2 | Professional Audio — WASAPI, ring buffer, 48k stereo | ⬜ |
| 3 | A/V Sync — MasterClock, AvSynchronizer, DriftController | ⬜ |
| 4 | Production UI — WinUI 3, settings, diagnostics, reconnect | ⬜ |
| 5 | Capture Optimization — OBS source plugin, shared texture | ⬜ |
| 6 | Virtual Audio — kernel driver, Duwn Mirror Audio device | ⬜ |

## License

This project is derived from [UxPlay](https://github.com/FDH2/UxPlay) (GPLv3) and is
distributed under the GNU General Public License v3.0.

See [`LICENSE`](LICENSE) and [`THIRD_PARTY_NOTICES.txt`](THIRD_PARTY_NOTICES.txt).
