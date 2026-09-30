# Wired USB video probe (prototype)

This is a separate research tool. It is not part of DUWN Mirror, its installer,
or the Wireless AirPlay path. Milestone W1 remains incomplete until a physical
USB device yields encoded, decoded, and presented frames in the DUWN OutputWindow.

## Supported probe path

The first cable-only probe targets Windows 10/11 x64 and iOS/iPadOS 17.4 or
later. On Windows, iOS 17.0–17.3.1 needs a privileged tunnel and additional
drivers; the no-root userspace route cannot prove cable-only transport there.
The probe refuses that range. See the [upstream tunnel support matrix](https://github.com/doronz88/pymobiledevice3/blob/master/docs/guides/ios17-tunnels.md).

The research sequence is:

1. usbmux list enumerates one USB device and reports its UDID.
2. lockdown info verifies a working paired/trusted lockdown session.
3. mounter auto-mount mounts the Developer Disk Image if needed.
4. The CoreDevice display CLI uses a forced userspace RSD tunnel and queries
   media support, then records 10–30 seconds of continuous display RTP.
5. The independent analyzer unwraps length-prefixed RTP and HEVC NAL units,
   writes an Annex-B .h265 file, and reports RTP timing and bitrate. ffprobe
   verifies coded resolution; ffmpeg must decode a frame for external proof.

The display service is com.apple.coredevice.displayservice over RSD. Its
getmediasupportinfo feature queries capabilities, and its startmediastream
feature negotiates the device-initiated RTP video path. These names and the
session offer/answer flow are documented in the [upstream DisplayService](https://github.com/doronz88/pymobiledevice3/blob/master/pymobiledevice3/remote/core_device/display_service.py).
The prototype does not itself negotiate that protocol; the isolated CLI does.

The [upstream CLI recipe](https://github.com/doronz88/pymobiledevice3/blob/master/docs/guides/cli-recipes.md#screen-streaming-hevc-video)
documents HEVC/RTP capture and its length-prefixed format. The parser here was
written for this project from packet framing facts; no pymobiledevice3 source
was copied.

## Run

Use an isolated Python environment. This dependency is for research only and
is never required by the released DUWN application. Put ffprobe and ffmpeg on
PATH for coded-resolution and external-decoder verification:

    py -m venv $env:TEMP\duwn-wired-probe-venv
    & "$env:TEMP\duwn-wired-probe-venv\Scripts\python.exe" -m pip install "pymobiledevice3==11.15.5"
    & "$env:TEMP\duwn-wired-probe-venv\Scripts\python.exe" tools\wired-probe\probe.py --duration 30 --out wired-probe-output

Unlock the device and approve its Trust prompt first. If lockdown fails, the
probe stops and records the error; it never treats USB detection as trust.
Mounting the Developer Disk Image changes device state, so the script does
that only when explicitly run.

For the physical-cable proof, disable Wi-Fi on the device before capturing.
The userspace tunnel normally uses the USB CoreDeviceProxy on 17.4+, but its
reference implementation can fall back to network RemotePairing if that
service is absent. The report therefore keeps cable_only_verified false;
confirm the transport separately rather than treating a USB listing alone
as proof of where the video travelled.

The output directory contains command logs, the raw display.rtp capture,
the independently depacketized display.h265, and report.json. A failed stage
remains recorded in report.json. The capture contains screen content and should
be handled as private device data.

For an existing capture, run:

    py tools\wired-probe\probe.py --analyze path\to\display.rtp --duration 15 --out analysis-output

The analyzer assumes the RTP video clock is 90 kHz and labels that assumption.
Delivery FPS uses the capture command's wall time; source FPS uses distinct
RTP timestamps. Neither number is a decoded or presented FPS. An existing
capture needs its actual capture duration supplied by --duration.

## Legal and product boundary

| Dependency | Version | License | Boundary | Distribution |
| --- | --- | --- | --- | --- |
| pymobiledevice3 | 11.15.5 in this prototype | GPL-3.0 | Separate CLI process in an isolated Python environment | Not bundled or shipped |
| Apple Mobile Device Service | Host-installed version | Apple proprietary | Windows service and USB driver | Not bundled |
| Python | Host-selected 3.9+ | PSF | Prototype interpreter | Not bundled |
| ffprobe | Optional host version | Build-dependent | Metadata subprocess | Not bundled |
| ffmpeg | Optional host version | Build-dependent | External decode validation subprocess | Not bundled |

No libimobiledevice, libusbmuxd, or libplist binaries are linked or
distributed by this prototype. Production use of any GPL/LGPL component
requires a separate license and redistribution review.

## Current integration blocker

DUWN now has a codec-selecting Media Foundation ingest path for Annex-B HEVC
and NV12/P010 renderer support where the driver allows it. A validated capture
must still establish the actual parameter sets, RTP framing, timestamp basis,
and coded resolution before a wired receiver is integrated. The probe does not
claim a DUWN FirstDecodedFrame, FirstPresentedFrame, or functioning wired mode.

On a physical iPhone XS with iOS 18.5, USB discovery, lockdown trust, the RSD
tunnel, and the display-service capability query succeeded. The service reported
`supportedFeatures: 0`; `start-video-stream` returned error 9021, saying remote
control requires iOS 27.0 or later on that device. No encoded video arrived.
HID control on the same phone is independent evidence for the control path and
does not establish display-stream availability. The 17.4+ requirement above is
for the Windows userspace tunnel, not a claim that this display service works on
every 17.4+ device.
