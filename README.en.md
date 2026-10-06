# Duwn Mirror

[Tiếng Việt](README.md) | [English](README.en.md)

[![GitHub Release](https://img.shields.io/github/v/release/leduwn/Duwn-Mirror?include_prereleases&style=flat-square&color=blue)](https://github.com/leduwn/Duwn-Mirror/releases)
[![Build & Test](https://img.shields.io/github/actions/workflow/status/leduwn/Duwn-Mirror/build.yml?branch=main&style=flat-square)](https://github.com/leduwn/Duwn-Mirror/actions)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%20%2F%2011%20(x64)-blue?style=flat-square)](https://github.com/leduwn/Duwn-Mirror)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPLv3-green.svg?style=flat-square)](LICENSE)

Duwn Mirror is a high-performance Windows application designed to **receive, mirror, and stream iPhone and iPad screens** wirelessly using standard Apple AirPlay protocol. Built for professionals, educators, developers, streamers, and general users:

- **Presentations, Teaching & Education:** Project slide decks, documents, and interactive iPad/iPhone workflows directly onto PC monitors and projectors.
- **App Demos & QA Testing:** Showcase iOS/iPadOS applications live during technical reviews and demonstrations.
- **Screen Recording & Video Production:** Capture clean 1080p60 footage for software tutorials and product reviews.
- **Livestreaming & Broadcasting:** Stream directly into OBS Studio, TikTok Live Studio, Discord, or Microsoft Teams via the dedicated Output Window or DirectShow Virtual Camera.
- **Casual Entertainment:** Enjoy mobile photos, media playback, and mobile gaming on a large desktop display with rich stereo audio.

![Duwn Mirror Control Panel and Output Window](assets/image_0.jpg)

---

## 🎯 Technical Scope & Privacy Commitments

- **Receiver Only:** The application operates strictly as an AirPlay receiver for video and audio streams from Apple devices to Windows; **it DOES NOT inject inputs and CANNOT remotely control the iOS/iPadOS device**.
- **100% Local-First:** Operates entirely over your local Wi-Fi or LAN network. No user accounts required, no cloud dependencies, no personal telemetry collection.
- **Native FPS:** Renders at native source frame rates (up to 60 FPS), with no artificial frame interpolation.
- **Pristine Audio:** Preserves stereo 48 kHz uncompressed PCM audio streams without dynamic range compression.

---

## ✨ Key Features

- **Direct3D 11 Hardware Decoding:** Utilizes Windows Media Foundation and GPU hardware acceleration (NVIDIA NVDEC, Intel QuickSync, AMD AMF) for low-overhead H.264 decoding with sub-millisecond local GPU pipeline latency.
- **Dedicated Output Window:**
  - Automatic aspect ratio locking tailored to each connected device (iPhone 19.5:9, iPad 4:3, video 16:9).
  - Flexible screen rotation (0°, 90°, 180°, 270°).
  - Fullscreen mode and Always on Top window pinning.
- **DirectShow Virtual Camera (`duwn-virtualcam.dll`):** Built-in 64-bit DirectShow filter sharing triple-buffered GPU textures directly, enabling OBS Studio, TikTok Live Studio, and conferencing apps to recognize Duwn Mirror as a physical webcam without desktop capture overhead.
- **Low-Latency WASAPI Audio:** Low-latency WASAPI output engine coupled with an adaptive ring buffer to prevent audio stutter and buffer underruns.
- **Real-Time Telemetry HUD:** Transparent overlay and metrics panel providing real-time diagnostics:
  - Render FPS and Source FPS.
  - Local GPU pipeline latency.
  - Dropped and catch-up frame counters.
  - Decoder engine identification (D3D11 / MF Video Decoder).

---

## 📥 Download & Installation

Visit the [Official GitHub Releases](https://github.com/leduwn/Duwn-Mirror/releases/latest) page to download the latest release:

| Package | Size | Intended Audience & Details |
| :--- | :--- | :--- |
| **`Duwn-Mirror-Setup-1.1.2-x64.exe`** (Recommended) | ~79 MB | **Complete Bootstrapper:** Automatically detects and installs Microsoft Visual C++ 2015-2026 Redistributable (x64) if missing; configures Windows Firewall rules automatically. |
| **`Duwn-Mirror-1.1.2-x64.msi`** | ~61 MB | **Standard Windows Installer:** Ideal for enterprise environments, automated deployments (GPO/SCCM), or systems with VC++ runtimes already present. Automatically opens firewall ports. |
| **`SHA256SUMS.txt`** | < 1 KB | Cryptographic SHA-256 hashes for verifying package integrity. |

### 🛡️ Windows SmartScreen Notice

As Duwn Mirror is a non-commercial open-source community project, installer binaries are not signed with expensive enterprise EV code-signing certificates. Windows Defender SmartScreen may display an alert:

> *"Windows protected your PC"*

**How to safely proceed:**

1. Click **"More info"** on the SmartScreen dialog.
2. Click **"Run anyway"** to continue installation.
3. You can verify the integrity of downloaded files by comparing their SHA-256 hash with `SHA256SUMS.txt`.

---

## 💻 System Requirements

- **Operating System:** Windows 10 (version 22H2 or newer) or Windows 11 (64-bit).
- **GPU Hardware:** DirectX 11 compatible graphics card with H.264 hardware decoding support (NVIDIA GeForce GTX 600 series+, Intel HD Graphics 4000 series+, AMD Radeon HD 7000 series+).
- **Network:** Windows PC and iOS/iPadOS device must be on the same local Wi-Fi / LAN subnet. **5 GHz Wi-Fi** or a wired Ethernet connection for the PC is strongly recommended for low latency and zero packet drop.
- **Source Device:** iPhone or iPad running iOS 12.0 or newer supporting **Screen Mirroring** in Control Center.

---

## 🚀 Quick Start Guide

1. **Launch:** Open `Duwn Mirror` from the Start Menu or Desktop.
2. **Verify Server Status:** The control panel will indicate that the AirPlay receiver is listening and ready for incoming connections.
3. **Connect from iPhone / iPad:**
   - Swipe to open **Control Center** on your iOS device.
   - Tap **Screen Mirroring**.
   - Select **`Duwn Mirror [Your-PC-Name]`**.
4. **Enjoy:** Your device screen and audio will instantly mirror onto the Windows display window.

---

## 🎥 Livestream Setup (OBS Studio & TikTok Live Studio)

Duwn Mirror supports two integration workflows for streaming software:

### Method 1: DirectShow Virtual Camera (Recommended)

1. In the Duwn Mirror control panel, toggle **Virtual Camera** on.
2. Open OBS Studio or TikTok Live Studio.
3. Add a source: select **Video Capture Device**.
4. Choose device: **`Duwn Mirror Video`**.
5. Video frames are delivered directly through Direct3D 11 hardware memory without interruption even if the desktop window is minimized or obscured.

### Method 2: Window Capture

1. In OBS Studio, click `+` under Sources -> select **Window Capture**.
2. Select Window: `[duwn-mirror.exe]: Output Window` (or `Cửa sổ phát`).
3. Capture Method: Select **Windows 10 (1903 and up)** for smooth hardware-accelerated capture.

### Audio Routing

- Duwn Mirror outputs audio directly to the default Windows playback device via WASAPI.
- In OBS Studio, audio is automatically captured by **Desktop Audio**, or via **Application Audio Capture** if you prefer isolating application audio tracks.

---

## 🔧 Troubleshooting

### 1. iPhone/iPad does not discover the PC in Screen Mirroring

- **Windows Firewall:** Ensure Windows Firewall allows traffic for Duwn Mirror. Required ports:
  - TCP: `5000–5010`, `7000–7010`, `7100` (AirPlay control & RTSP)
  - UDP: `6000–6010`, `7011` (RTP media streaming & mDNS)
- **Router AP/Client Isolation:** Some Wi-Fi routers enable "AP Isolation" or "Client Isolation", preventing wireless clients from communicating with each other. Disable this setting in your router's administration page.
- **Subnet Mismatch:** Ensure PC and iOS device are not on segregated subnets (such as a Guest Wi-Fi network).

### 2. Black screen or frame stuttering

- **Wi-Fi Band:** 2.4 GHz Wi-Fi is susceptible to radio interference and packet loss with high-bitrate 1080p60 video. Switch your Wi-Fi network to the 5 GHz band.
- **GPU Drivers:** Ensure graphics drivers (NVIDIA, Intel, AMD) are up to date with full D3D11 Video Decoder support.

---

## 🛠️ Building from Source

### Prerequisites

- **OS:** Windows 10/11 x64.
- **Compiler:** Visual Studio 2022 (with *Desktop development with C++* workload).
- **Build System:** CMake 3.25 or newer.
- **Installer Tooling (Optional):** WiX Toolset v5 and .NET SDK.

### Build Commands

```powershell
# 1. Clone repository
git clone https://github.com/leduwn/Duwn-Mirror.git
cd Duwn-Mirror

# 2. Configure project via CMake
cmake --preset release

# 3. Build release binaries
cmake --build build/release --config Release

# 4. Run automated unit tests
ctest --test-dir build/release --output-on-failure -C Release

# 5. Build WiX MSI and Setup EXE installers (Optional)
powershell -ExecutionPolicy Bypass -File installer/build-installer.ps1 -Configuration Release
```

---

## 📄 License & Legal Notice

- **Software License:** Duwn Mirror is derived from open-source projects including UxPlay (GPLv3) and is licensed under the [GNU General Public License v3.0](LICENSE).
- **Trademark Notice:** Apple, iPhone, iPad, iOS, iPadOS, AirPlay, and Bonjour are trademarks of Apple Inc., registered in the U.S. and other countries. Duwn Mirror is an independent open-source project and is not affiliated with, sponsored, or endorsed by Apple Inc.
