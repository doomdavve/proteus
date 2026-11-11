"""Check PCM preservation across WAV and mixed 8/16-bit XM sample export."""

import math
from pathlib import Path
import struct
import tempfile
import unittest
import wave

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))  # The tool under test.
import generate_audio as audio


def spectral_power(sample, start, hz):
    frequency = 130.81 * (8363 * 2 ** (11 / 12)) / 16000
    count = round(8 * sample.rate / frequency)
    offset = round(start * sample.rate)
    window = [sample.pcm[offset + i] * (0.5 - 0.5 * math.cos(math.tau * i
              / (count - 1))) for i in range(count)]
    real = sum(x * math.cos(math.tau * hz * i / sample.rate)
               for i, x in enumerate(window))
    imag = sum(x * math.sin(math.tau * hz * i / sample.rate)
               for i, x in enumerate(window))
    return real * real + imag * imag


class AudioExportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.samples = audio.make_samples()

    def test_deterministic_and_clean_boundaries(self):
        self.assertEqual(self.samples, audio.make_samples())
        for name in ("kick", "snare", "hat", "bass", "synth"):
            sample = self.samples[name]
            self.assertEqual((sample.rate, sample.bits), (44100, 16))
            self.assertEqual((sample.pcm[0], sample.pcm[-1]), (0, 0))
            self.assertGreater(max(map(abs, sample.pcm)), 1000)
            self.assertLess(max(map(abs, sample.pcm)), 32767)
            self.assertLess(abs(sum(sample.pcm) / len(sample.pcm)), 33)

    def test_melodic_pitch_and_closing_brightness(self):
        # C-4 must retain the old samples' audible fundamental after retuning.
        frequency = 130.81 * (8363 * 2 ** (11 / 12)) / 16000
        for name in ("bass", "synth"):
            with self.subTest(instrument=name):
                sample = self.samples[name]
                early = [spectral_power(sample, 0.015, frequency * h) for h in range(1, 13)]
                late = [spectral_power(sample, 0.28, frequency * h) for h in range(1, 13)]
                self.assertGreater(early[0], max(early[1:]))
                self.assertGreater(early[0], spectral_power(sample, 0.015, frequency * 0.9))
                self.assertGreater(early[0], spectral_power(sample, 0.015, frequency * 1.1))
                self.assertGreater(early[0], 10 * late[0])
                self.assertGreater(sum(early[2:]) / early[0], 2 * sum(late[2:]) / late[0])

    def test_wav_pcm(self):
        with tempfile.TemporaryDirectory() as directory:
            for name, sample in {**self.samples, "8bit": audio.Sample(
                    [-128, -1, 0, 1, 127], 16000, 8)}.items():
                path = Path(directory) / f"{name}.wav"
                audio.write_wav(path, sample)
                with wave.open(str(path), "rb") as wav:
                    self.assertEqual(wav.getnchannels(), 1)
                    self.assertEqual(wav.getsampwidth(), sample.bits // 8)
                    self.assertEqual(wav.getframerate(), sample.rate)
                    self.assertEqual(wav.getnframes(), len(sample.pcm))
                    data = wav.readframes(wav.getnframes())
                decoded = (list(struct.unpack(f"<{len(data) // 2}h", data))
                           if sample.bits == 16 else [x - 128 for x in data])
                self.assertEqual(decoded, sample.pcm)

    def test_xm_headers_and_delta_pcm(self):
        self.check_xm(self.samples)

    def test_mixed_bit_depth_xm(self):
        self.check_xm({**self.samples, "synth": audio.Sample(
            [-128, 127, 0, -1, 1], 16000, 8)})

    def check_xm(self, samples):
        # Walk the actual file using its on-disk sizes, including byte lengths
        # for 16-bit samples. Decoding must recover every signed PCM value.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.xm"
            audio.write_xm(path, samples)
            data = path.read_bytes()
        self.assertEqual(struct.unpack_from("<8H", data, 64), (8, 0, 8, 8, 5, 1, 6, 125))
        offset = 60 + struct.unpack_from("<I", data, 60)[0]
        for _ in range(8):
            size, _, rows, packed = struct.unpack_from("<IBHH", data, offset)
            self.assertEqual(rows, 64)
            offset += size + packed
        for sample in samples.values():
            offset += struct.unpack_from("<I", data, offset)[0]
            length, start, loop, volume, fine, flags, pan, relative, _ = struct.unpack_from(
                "<IIIBbBBbB", data, offset)
            bits = 16 if flags & 16 else 8
            self.assertEqual(bits, sample.bits)
            self.assertEqual((start, loop, volume, pan, flags & 3), (0, 0, 56, 128, 0))
            self.assertEqual(length, len(sample.pcm) * (bits // 8))
            rate = 8363 * 2 ** ((relative + fine / 128) / 12)
            self.assertLess(abs(1200 * math.log2(rate / sample.rate)), 0.4)
            offset += 40
            decoded = []
            accumulator = 0
            for pos in range(offset, offset + length, bits // 8):
                delta = int.from_bytes(data[pos:pos + bits // 8], "little")
                accumulator = (accumulator + delta) % (1 << bits)
                decoded.append(accumulator if accumulator < (1 << (bits - 1))
                               else accumulator - (1 << bits))
            self.assertEqual(decoded, sample.pcm)
            offset += length
        self.assertEqual(offset, len(data))


if __name__ == "__main__":
    unittest.main()
