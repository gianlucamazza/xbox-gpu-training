"""Negative probes for the release numerical comparator."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_e0_bit_identity import canonical, clean  # noqa: E402


class NumericalIdentity(unittest.TestCase):
    def test_negative_zero_is_not_positive_zero(self):
        self.assertNotEqual(
            canonical(clean({"value": -0.0})), canonical(clean({"value": 0.0}))
        )

    def test_numeric_types_are_preserved(self):
        self.assertNotEqual(
            canonical(clean({"value": 2})), canonical(clean({"value": 2.0}))
        )

    def test_schema_drift_is_not_execution_metadata(self):
        self.assertNotEqual(
            canonical(clean({"schema": "v1"})), canonical(clean({"schema": "v2"}))
        )

    def test_changed_tensor_is_not_execution_metadata(self):
        self.assertNotEqual(
            canonical(clean({"tensors": [1.0]})), canonical(clean({"tensors": [1.01]}))
        )

    def test_telemetry_and_key_order_do_not_change_identity(self):
        self.assertEqual(
            canonical(clean({"schema": "v1", "tensors": [1.0], "gpu_seconds": 3.0})),
            canonical(clean({"gpu_seconds": 9.0, "tensors": [1.0], "schema": "v1"})),
        )


if __name__ == "__main__":
    unittest.main()
