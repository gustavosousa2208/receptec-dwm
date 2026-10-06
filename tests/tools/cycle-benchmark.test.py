import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("cycle_benchmark", Path(__file__).parents[2] / "tools/cycle-benchmark.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class BenchmarkTests(unittest.TestCase):
    def capture(self):
        args = SimpleNamespace(period_ms=500, seconds=60, minimum_seconds=60,
            minimum_success=90, mode="measured", baseline_mm=None, address="test")
        capture = module.Capture(args, io.StringIO())
        capture.started = 0
        return capture

    def test_silent_tail_is_loss(self):
        c = self.capture()
        for round_id in range(1, 61):
            for pair in range(3):
                c.rows[pair][round_id] = dict(status=0, distance_mm=1000, source=0)
        with patch.object(module.time, "monotonic", return_value=60):
            result = c.summary(complete=True)
        self.assertEqual(result["expected_rounds"], 119)
        self.assertFalse(result["accepted"])
        self.assertGreater(result["pairs"]["A-T"]["missing"], 0)

    def test_three_pairs_threshold_includes_invalid_results(self):
        c = self.capture()
        for round_id in range(1, 121):
            for pair in range(3):
                c.rows[pair][round_id] = dict(status=1 if pair == 1 and round_id <= 13 else 0,
                    distance_mm=1000, source=0)
        with patch.object(module.time, "monotonic", return_value=60):
            result = c.summary(complete=True)
        self.assertFalse(result["accepted"])
        self.assertLess(result["pairs"]["A-T"]["success_percent"], 90)

    def test_accelerated_valid_capture_passes(self):
        c = self.capture()
        for round_id in range(1, 121):
            for pair in range(3):
                c.rows[pair][round_id] = dict(status=0, distance_mm=1000, source=0)
        with patch.object(module.time, "monotonic", return_value=60):
            self.assertTrue(c.summary(complete=True)["accepted"])

if __name__ == "__main__":
    unittest.main()
