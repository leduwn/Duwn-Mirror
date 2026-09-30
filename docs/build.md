# Build Guide

## Prerequisites

### Required
- **Visual Studio 2022+** with:
  - "Desktop development with C++" workload
  - Windows 11 SDK (10.0.22621.0 or later)
  - MSVC v143 toolchain
- **CMake 3.28+**
- **Git**

### Windows App SDK (WinUI 3)
```powershell
winget install Microsoft.WindowsAppRuntimeRedist
```
Or install the NuGet package via CMake fetch.

### Bonjour SDK for Windows (mDNS)
Required for UxPlay mDNS advertisement.
Download from Apple Developer: https://developer.apple.com/bonjour/
Install to default path (`C:\Program Files\Bonjour SDK`).
Review Apple's Bonjour SDK EULA before distribution.

### UxPlay (sidecar binary)
See `third_party/uxplay/README.md` for Windows build instructions.
UxPlay must be built with:
- MinGW-w64 or WSL2 Ubuntu cross-compile (MSVC port not needed for sidecar)
- Flags `-vrtp` and `-artp` support verified

### vcpkg (optional, recommended)
```powershell
git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\vcpkg"
```

---

## Build

### Configure (Debug)
```powershell
cmake --preset debug
```

### Configure (Release)
```powershell
cmake --preset release
```

### Configure (RelWithDebInfo — recommended for profiling)
```powershell
cmake --preset relwithdebinfo
```

### Build
```powershell
cmake --build build/debug
```

### Run unit tests
```powershell
cd build/debug
ctest --output-on-failure
```

---

## Output Layout

```
build/<preset>/
  duwn-mirror.exe          Main application
  duwn-airplay/
    uxplay.exe             AirPlay sidecar (built separately, copied here)
  obs-plugin/
    duwn-mirror-source.dll OBS source plugin
  tests/
    duwn-unit-tests.exe
```

---

## Environment Notes

- All paths in CMake are relative. Do not hard-code `C:\Users\...`.
- GPU: any DirectX 11-capable GPU with DXVA2/D3D11VA hardware decode.
- Tested on: NVIDIA GeForce (NVDEC), AMD Radeon (AMF/UVD), Intel Quick Sync.
- Software decode fallback available but not recommended for 1080p60.

---

## Known Issues / First-Time Setup

1. **Firewall**: Windows Firewall will prompt on first run. Allow on Private networks.
2. **Bonjour service**: Must be running (`mDNSResponder` service). Installed with Bonjour SDK.
3. **UxPlay sidecar**: Must be present at `duwn-airplay/uxplay.exe`. See third_party setup.
