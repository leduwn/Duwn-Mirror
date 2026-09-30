"""Isolated research probe for an iOS 17.4+ USB CoreDevice display stream.

This script uses the separately installed GPL-3.0 pymobiledevice3 CLI. It is
not imported, packaged, or launched by DUWN Mirror.
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from collections import Counter
from pathlib import Path


START_CODE = b"\x00\x00\x00\x01"
MAX_PACKET = 1024 * 1024


def run_cli(arguments, log_path):
    command = [sys.executable, "-m", "pymobiledevice3", *arguments]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace")
    log_path.write_text(
        "$ " + " ".join(command) + "\n" + result.stdout + "\n" + result.stderr,
        encoding="utf-8",
    )
    if result.returncode:
        detail = result.stderr.strip().splitlines()[-1] if result.stderr.strip() else "no stderr"
        raise RuntimeError(f"{' '.join(arguments)} failed ({result.returncode}): {detail}; see {log_path}")
    return result.stdout


def iter_rtp(path):
    with path.open("rb") as source:
        while prefix := source.read(4):
            if len(prefix) != 4:
                raise ValueError("truncated packet length")
            length = int.from_bytes(prefix, "big")
            if length < 12 or length > MAX_PACKET:
                raise ValueError(f"invalid packet length: {length}")
            packet = source.read(length)
            if len(packet) != length:
                raise ValueError("truncated RTP packet")
            if packet[0] >> 6 != 2:
                raise ValueError("invalid RTP version")
            payload_type = packet[1] & 0x7F
            if 64 <= payload_type <= 95:
                yield None
                continue
            header_length = 12 + (packet[0] & 0x0F) * 4
            if packet[0] & 0x10:
                if len(packet) < header_length + 4:
                    raise ValueError("truncated RTP extension")
                words = int.from_bytes(packet[header_length + 2:header_length + 4], "big")
                header_length += 4 + words * 4
            if header_length > len(packet):
                raise ValueError("RTP header exceeds packet")
            padding = packet[-1] if packet[0] & 0x20 else 0
            if padding > len(packet) - header_length:
                raise ValueError("invalid RTP padding")
            payload = packet[header_length:len(packet) - padding]
            yield {
                "sequence": int.from_bytes(packet[2:4], "big"),
                "timestamp": int.from_bytes(packet[4:8], "big"),
                "marker": bool(packet[1] & 0x80),
                "payload": payload,
                "packet_bytes": length,
            }


def analyze_capture(capture, annex_b, wall_seconds):
    """Analyze RTP timing and convert common RFC 7798 HEVC packet forms."""
    counts = Counter()
    timestamps = []
    nal_types = Counter()
    expected_sequence = None
    fragment = None
    frame_markers = 0
    packet_bytes = 0
    with annex_b.open("wb") as output:
        for packet in iter_rtp(capture):
            if packet is None:
                counts["rtcp"] += 1
                continue
            counts["rtp"] += 1
            packet_bytes += packet["packet_bytes"]
            timestamp = packet["timestamp"]
            if not timestamps or timestamps[-1] != timestamp:
                timestamps.append(timestamp)
            if packet["marker"]:
                frame_markers += 1
            sequence = packet["sequence"]
            if expected_sequence is not None and sequence != expected_sequence:
                counts["sequence_discontinuities"] += 1
                fragment = None
            expected_sequence = (sequence + 1) & 0xFFFF
            payload = packet["payload"]
            if len(payload) < 2:
                continue
            nal_type = (payload[0] >> 1) & 0x3F

            def emit(nal):
                if len(nal) >= 2:
                    output.write(START_CODE)
                    output.write(nal)
                    nal_types[(nal[0] >> 1) & 0x3F] += 1

            if nal_type < 48:
                emit(payload)
            elif nal_type == 48:
                cursor = 2
                while cursor + 2 <= len(payload):
                    size = int.from_bytes(payload[cursor:cursor + 2], "big")
                    cursor += 2
                    if size < 2 or cursor + size > len(payload):
                        counts["malformed_aggregation"] += 1
                        break
                    emit(payload[cursor:cursor + size])
                    cursor += size
            elif nal_type == 49 and len(payload) >= 3:
                fu = payload[2]
                if fu & 0x80:
                    original_type = fu & 0x3F
                    fragment = bytearray(
                        [(payload[0] & 0x81) | (original_type << 1), payload[1]]
                    )
                if fragment is None:
                    counts["orphan_fragments"] += 1
                    continue
                fragment.extend(payload[3:])
                if fu & 0x40:
                    emit(fragment)
                    fragment = None
            else:
                counts["unsupported_hevc_packets"] += 1

    elapsed_ticks = ((timestamps[-1] - timestamps[0]) & 0xFFFFFFFF) if len(timestamps) > 1 else 0
    source_seconds = elapsed_ticks / 90000.0 if elapsed_ticks else None
    intervals = [
        ((later - earlier) & 0xFFFFFFFF) / 90000.0
        for earlier, later in zip(timestamps, timestamps[1:])
    ]
    hevc_parameter_sets = any(nal_types[nal_type] for nal_type in (32, 33, 34))
    video_nals = sum(nal_types[nal_type] for nal_type in range(32))
    continuity = (
        len(timestamps) >= max(10, int(wall_seconds * 5))
        and source_seconds is not None
        and source_seconds >= wall_seconds * 0.5
        and hevc_parameter_sets
        and video_nals > 0
        and not counts["malformed_aggregation"]
        and not counts["unsupported_hevc_packets"]
    )
    return {
        "capture_bytes": capture.stat().st_size,
        "annex_b_bytes": annex_b.stat().st_size,
        "rtp_packets": counts["rtp"],
        "rtcp_packets": counts["rtcp"],
        "sequence_discontinuities": counts["sequence_discontinuities"],
        "malformed_aggregation": counts["malformed_aggregation"],
        "orphan_fragments": counts["orphan_fragments"],
        "unsupported_hevc_packets": counts["unsupported_hevc_packets"],
        "hevc_nal_types": dict(nal_types),
        "coded_codec": "HEVC" if hevc_parameter_sets else "unverified",
        "hevc_video_nals": video_nals,
        "frames_by_unique_rtp_timestamp": len(timestamps),
        "frame_markers": frame_markers,
        "rtp_clock_hz_assumed": 90000,
        "source_timeline_seconds": source_seconds,
        "rtp_timestamps": timestamps,
        "median_source_interval_ms": statistics.median(intervals) * 1000 if intervals else None,
        "source_fps_from_rtp_timestamps": (len(timestamps) - 1) / source_seconds if source_seconds else None,
        "delivery_fps_over_capture_wall_time": len(timestamps) / wall_seconds if wall_seconds else None,
        "encoded_mbps_over_capture_wall_time": packet_bytes * 8 / wall_seconds / 1e6 if wall_seconds else None,
        "continuous_encoded_frames_observed": continuity,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=int, default=30, choices=range(10, 301),
                        metavar="10..300")
    parser.add_argument("--out", type=Path, default=Path("wired-probe-output"))
    parser.add_argument("--analyze", type=Path, help="analyze an existing .rtp capture only")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    phases = ("UsbDetected", "AppleMobileDeviceServiceAvailable", "UsbmuxDeviceVisible",
              "LockdownConnected", "PairingRecordFound", "Trusted", "RsdTunnelEstablished",
              "DisplayServiceOpened", "StreamNegotiated", "FirstRtpPacket",
              "FirstEncodedFrame", "CodecDetected", "FirstKeyframe",
              "ExternalDecodeVerified")
    report = {"prototype": True, "product_wired_streaming": False,
              "trust_state": "unverified", "cable_only_verified": False,
              "stages": [], "phases": {name: "NOT_RUN" for name in phases}}
    report["first_frame_latency_ms"] = None
    report_path = args.out / "report.json"
    current_phase = None

    def stage(name, detail=None):
        report["stages"].append({"name": name, "detail": detail})
        if name in report["phases"]:
            report["phases"][name] = "PASS"
        report["last_successful_phase"] = name
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")

    try:
        if args.analyze:
            capture = args.analyze
            wall_seconds = float(args.duration)
        else:
            try:
                import pymobiledevice3  # noqa: F401 - dependency check only
            except ImportError as exc:
                raise RuntimeError("Install pymobiledevice3 in an isolated Python environment") from exc
            if os.name == "nt":
                pnp = subprocess.run(
                    ["powershell.exe", "-NoProfile", "-Command",
                     "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_05AC|APPLE' -or $_.FriendlyName -match 'iPhone|iPad|Apple Mobile' } | Select-Object Status,Class,FriendlyName,InstanceId | ConvertTo-Json -Compress"],
                    capture_output=True, text=True, errors="replace",
                )
                if pnp.returncode == 0:
                    result = json.loads(pnp.stdout) if pnp.stdout.strip() else []
                    report["pnp_devices"] = result if isinstance(result, list) else [result]
                current_phase = "AppleMobileDeviceServiceAvailable"
                service = subprocess.run(
                    ["sc.exe", "query", "Apple Mobile Device Service"],
                    capture_output=True, text=True, errors="replace",
                )
                (args.out / "apple-service.log").write_text(
                    service.stdout + "\n" + service.stderr, encoding="utf-8")
                report["apple_service_running"] = service.returncode == 0 and "RUNNING" in service.stdout
                if not report["apple_service_running"]:
                    raise RuntimeError("Apple Mobile Device Service is missing or stopped; see apple-service.log")
                stage("AppleMobileDeviceServiceAvailable")
            current_phase = "UsbDetected"
            listed = json.loads(run_cli(["usbmux", "list"], args.out / "usbmux.log"))
            devices = [item for item in listed if item.get("ConnectionType") == "USB"]
            report["usb_devices"] = devices
            if len(devices) != 1:
                report["phases"]["UsbmuxDeviceVisible"] = "FAIL"
                raise RuntimeError(f"Expected exactly one USB iPhone/iPad; found {len(devices)}")
            device = devices[0]
            report["udid"] = device.get("UniqueDeviceID") or device.get("Identifier")
            report["device_name"] = device.get("DeviceName")
            report["product_type"] = device.get("ProductType")
            report["ios_version"] = device.get("ProductVersion")
            stage("UsbDetected", report["udid"])
            stage("UsbmuxDeviceVisible", report["udid"])
            if not report["udid"]:
                raise RuntimeError("usbmux did not provide a device identifier")
            os.environ["PYMOBILEDEVICE3_UDID"] = report["udid"]
            current_phase = "LockdownConnected"
            info = json.loads(run_cli(
                ["lockdown", "info", "--udid", report["udid"]],
                args.out / "lockdown.log",
            ))
            report["pairing_result"] = "lockdown info succeeded"
            report["trust_state"] = "trusted for current lockdown session"
            report["ios_version"] = info.get("ProductVersion", report["ios_version"])
            stage("LockdownConnected")
            stage("PairingRecordFound", "lockdown query succeeded with paired device")
            version = tuple(int(part) for part in report["ios_version"].split(".")[:2])
            if version < (17, 4):
                raise RuntimeError("Windows cable-only userspace RSD probe requires iOS 17.4+")
            stage("Trusted", "lockdown query succeeded; device trust confirmed for this session")
            run_cli(["mounter", "auto-mount", "--udid", report["udid"]],
                    args.out / "mounter.log")
            stage("DeveloperImageMounted")
            base = ["developer", "core-device", "display"]
            current_phase = "RsdTunnelEstablished"
            report["media_support"] = json.loads(run_cli(
                [*base, "get-media-support-info", "--userspace"],
                args.out / "media-support.log"))
            stage("RsdTunnelEstablished", "userspace tunnel command succeeded")
            stage("DisplayServiceOpened", "media support query succeeded")
            capture = args.out / "display.rtp"
            current_phase = "StreamNegotiated"
            start = time.monotonic()
            run_cli([*base, "start-video-stream", str(capture), "--duration",
                     str(args.duration), "--userspace"],
                    args.out / "capture.log")
            wall_seconds = time.monotonic() - start
            stage("StreamNegotiated", "capture command completed")
            stage("CaptureFinished", f"{wall_seconds:.2f} seconds")
        report["capture"] = str(capture.resolve())
        report["capture_wall_seconds"] = wall_seconds
        annex_b = args.out / "display.h265"
        current_phase = "FirstRtpPacket"
        report["video"] = analyze_capture(capture, annex_b, wall_seconds)
        report["annex_b"] = str(annex_b.resolve())
        if report["video"]["rtp_packets"]:
            stage("FirstRtpPacket")
        current_phase = "FirstEncodedFrame"
        if report["video"]["continuous_encoded_frames_observed"]:
            stage("FirstEncodedFrame", "continuous HEVC/RTP capture observed")
        else:
            raise RuntimeError("Capture did not prove a continuous encoded video stream")
        stage("CodecDetected", report["video"]["coded_codec"])
        if any(report["video"]["hevc_nal_types"].get(nal_type, 0)
               for nal_type in (19, 20, 21)):
            stage("FirstKeyframe", "HEVC IDR/CRA NAL observed")
        else:
            current_phase = "FirstKeyframe"
            raise RuntimeError("No HEVC IDR/CRA NAL observed")
        current_phase = "ExternalDecodeVerified"
        ffprobe = shutil.which("ffprobe")
        ffmpeg = shutil.which("ffmpeg")
        if not ffprobe or not ffmpeg:
            raise RuntimeError("Encoded capture obtained; ffprobe and ffmpeg are required to verify coded dimensions and external decoding")
        probe = subprocess.run(
            [ffprobe, "-v", "error", "-select_streams", "v:0",
             "-show_entries", "stream=codec_name,width,height", "-of", "json", str(annex_b)],
            capture_output=True, text=True,
        )
        if probe.returncode:
            raise RuntimeError(f"ffprobe could not identify the captured stream: {probe.stderr.strip()}")
        streams = json.loads(probe.stdout).get("streams", [])
        if not streams or not streams[0].get("width") or not streams[0].get("height"):
            raise RuntimeError("ffprobe did not report coded video dimensions")
        report["decoded_stream_metadata"] = streams[0]
        if streams[0].get("codec_name") != "hevc":
            raise RuntimeError(f"Expected HEVC from this analyzer; ffprobe reported {streams[0].get('codec_name')}")
        stage("VideoMetadataVerified", streams[0])
        decode = subprocess.run(
            [ffmpeg, "-v", "error", "-i", str(annex_b), "-f", "null", "-",
             "-progress", "pipe:1", "-nostats"],
            capture_output=True, text=True,
        )
        (args.out / "decode.log").write_text(decode.stdout + "\n" + decode.stderr, encoding="utf-8")
        frames = [int(line.split("=", 1)[1]) for line in decode.stdout.splitlines()
                  if line.startswith("frame=") and line.split("=", 1)[1].strip().isdigit()]
        report["external_decoded_frames"] = max(frames, default=0)
        if decode.returncode or not report["external_decoded_frames"]:
            raise RuntimeError("ffmpeg did not decode any complete frames; see decode.log")
        stage("ExternalDecodeVerified", f"{report['external_decoded_frames']} frames via ffmpeg")
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Continuous encoded stream captured: {capture}")
        print(f"Analysis: {report_path}")
        return 0
    except (OSError, ValueError, RuntimeError) as exc:
        report["error"] = str(exc)
        if report["phases"].get(current_phase) == "NOT_RUN":
            report["phases"][current_phase] = "FAIL"
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"Probe stopped: {exc}\nReport: {report_path}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
