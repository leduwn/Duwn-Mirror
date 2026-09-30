# Third-Party Notices

## UxPlay
- **Source**: https://github.com/FDH2/UxPlay
- **License**: GNU General Public License v3.0 (GPLv3)
- **Usage**: AirPlay protocol implementation (discovery, session, crypto, RTP, NTP).
  Used as a sidecar process in Milestone 0; portions may be integrated in later milestones.
- **Full license**: https://www.gnu.org/licenses/gpl-3.0.txt

## libplist
- **Source**: https://github.com/libimobiledevice/libplist
- **License**: GNU Lesser General Public License v2.1 (LGPLv2.1)
- **Usage**: Required by UxPlay for Apple property list parsing.

## openssl / libcrypto
- **Source**: https://www.openssl.org/
- **License**: Apache License 2.0 (OpenSSL 3.x)
- **Usage**: Required by UxPlay for AirPlay encryption (FairPlay, DTLS).

## avahi / mDNS
- **Source**: https://avahi.org/ or Apple mDNS (Bonjour SDK for Windows)
- **License**: LGPLv2.1 (avahi) / Apple Bonjour SDK EULA
- **Usage**: mDNS/DNS-SD service advertisement for AirPlay discovery.
- **Note**: On Windows, UxPlay can use dns_sd.h from Apple's Bonjour SDK.
  The Bonjour SDK for Windows has a separate EULA; review before distribution.

## Microsoft Windows App SDK / WinUI 3
- **Source**: https://github.com/microsoft/WindowsAppSDK
- **License**: MIT
- **Usage**: Native Windows UI framework.

## Media Foundation (Windows SDK)
- **License**: Microsoft Windows SDK License
- **Usage**: Hardware-accelerated H.264/H.265 video decode.

## Direct3D 11 / DXGI (Windows SDK)
- **License**: Microsoft Windows SDK License
- **Usage**: GPU rendering, zero-copy texture pipeline.

## WASAPI (Windows SDK)
- **License**: Microsoft Windows SDK License
- **Usage**: Low-latency audio output.

## {fmt} (optional, to be decided)
- **Source**: https://github.com/fmtlib/fmt
- **License**: MIT
- **Usage**: Structured logging formatting.

---

**Note on GPL compliance**: Because this project links against and ships
UxPlay (GPLv3), the entire combined work must be distributed under GPLv3.
Source code must be made available to recipients of binaries.
