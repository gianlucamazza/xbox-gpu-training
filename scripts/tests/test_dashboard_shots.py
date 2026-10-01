"""Milestone selection and PNG checks of the dashboard screenshot recorder."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e0_dashboard_shots import PNG_SIGNATURE, app_running, milestone, png_size  # noqa: E402

SCHEDULE = {"ends": [879, 1758, 3516], "cooldown_starts": [792, 1583, 3165]}


def status(**fields):
    return {"state": "running", "phase": "trunk", "schedule": SCHEDULE, **fields}


class Milestones(unittest.TestCase):
    def test_first_status_with_schedule_is_running(self):
        self.assertEqual(milestone(status(), set()), "running")
        self.assertIsNone(milestone(status(), {"running"}))

    def test_running_waits_for_a_curve(self):
        self.assertIsNone(milestone(status(trunk_step=64), set(), running_step=1024))
        self.assertEqual(
            milestone(status(trunk_step=1024), set(), running_step=1024), "running"
        )

    def test_status_without_schedule_is_not_a_milestone(self):
        self.assertIsNone(milestone({"state": "running"}, set()))

    def test_cooldown_branch_comes_from_the_published_end(self):
        cooling = status(phase="cooldown", cooldown_end=1758)
        self.assertEqual(milestone(cooling, {"running"}), "cooldown-2")
        self.assertIsNone(milestone(status(phase="cooldown", cooldown_end=5), set()))

    def test_final_states(self):
        for state in ("completed", "interrupted", "failed"):
            self.assertEqual(milestone({"state": state}, {"running"}), state)


class App(unittest.TestCase):
    def test_running_app_by_image_or_package(self):
        package = "GianlucaMazza.XgpuE0_0.1.0.65_x64__g0p5dcfz4t9z4"
        self.assertTrue(
            app_running({"Processes": [{"ImageName": "XgpuE0.exe"}]}, package)
        )
        self.assertTrue(
            app_running({"Processes": [{"PackageFullName": package}]}, package)
        )
        self.assertFalse(
            app_running({"Processes": [{"ImageName": "System Idle Process"}]}, package)
        )


class Png(unittest.TestCase):
    def test_size_from_header(self):
        data = (
            PNG_SIGNATURE
            + struct.pack(">I", 13)
            + b"IHDR"
            + struct.pack(">II", 1920, 1080)
        )
        self.assertEqual(png_size(data + b"\0" * 5), (1920, 1080))

    def test_rejects_other_payloads(self):
        with self.assertRaises(ValueError):
            png_size(b'{"ErrorCode": 1}' + b"\0" * 16)


if __name__ == "__main__":
    unittest.main()
