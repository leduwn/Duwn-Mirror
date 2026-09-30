import tempfile
import unittest
from pathlib import Path

from probe import analyze_capture, iter_rtp


def rtp(sequence, timestamp, payload, marker=True):
    header = bytes([0x80, 0xE0 if marker else 0x60])
    header += sequence.to_bytes(2, "big") + timestamp.to_bytes(4, "big")
    header += b"\x00\x00\x00\x01"
    packet = header + payload
    return len(packet).to_bytes(4, "big") + packet


class CaptureTests(unittest.TestCase):
    def test_continuous_hevc_rtp_and_annex_b(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            capture = root / "sample.rtp"
            annex_b = root / "sample.h265"
            # HEVC SPS followed by twenty coded slices at ten frames/sec.
            packets = [rtp(0, 0, bytes([33 << 1, 1, 0xAA]), False)]
            packets += [
                rtp(index + 1, index * 9000, bytes([1 << 1, 1, index]))
                for index in range(20)
            ]
            capture.write_bytes(b"".join(packets))
            result = analyze_capture(capture, annex_b, 2.0)
            self.assertEqual(result["coded_codec"], "HEVC")
            self.assertTrue(result["continuous_encoded_frames_observed"])
            self.assertAlmostEqual(result["source_fps_from_rtp_timestamps"], 10.0)
            self.assertEqual(result["sequence_discontinuities"], 0)
            self.assertTrue(annex_b.read_bytes().startswith(b"\x00\x00\x00\x01"))

    def test_truncated_capture_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "broken.rtp"
            path.write_bytes((100).to_bytes(4, "big") + b"short")
            with self.assertRaises(ValueError):
                list(iter_rtp(path))


if __name__ == "__main__":
    unittest.main()
