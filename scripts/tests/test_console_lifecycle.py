"""Off-console tests for XgpuE0 teardown, CSRF echo, and idle-GPU checks."""

import json
import sys
import unittest
from http.cookies import SimpleCookie
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e0_console_lifecycle import (  # noqa: E402
    LifecyclePortal,
    app_processes,
    csrf_headers,
    engines_utilization,
    idle_gpu_report,
    ingest_cookies,
    orphan_gpu_signature,
    terminate_path,
)

PACKAGE = "GianlucaMazza.XgpuE0_0.1.0.109_x64__g0p5dcfz4t9z4"


def systemperf(*engines):
    return {
        "GPUData": {
            "AvailableAdapters": [{"EnginesUtilization": list(engines)}]
        }
    }


def listing(*procs):
    return {"Processes": list(procs)}


def e0_proc(**fields):
    return {
        "ImageName": "XgpuE0.exe",
        "ProcessId": 4242,
        "CPUUsage": 0.0,
        "PackageFullName": PACKAGE,
        **fields,
    }


class Detection(unittest.TestCase):
    def test_app_processes_by_image_or_package(self):
        self.assertEqual(
            len(app_processes(listing({"ImageName": "XgpuE0.exe"}), PACKAGE)), 1
        )
        self.assertEqual(
            len(app_processes(listing({"PackageFullName": PACKAGE}), PACKAGE)), 1
        )
        self.assertEqual(
            app_processes(listing({"ImageName": "System Idle Process"}), PACKAGE),
            [],
        )

    def test_engines_from_system_or_root(self):
        nested = {"system": systemperf(1, 0, 0, 0, 0, 99.0, 0)}
        self.assertEqual(engines_utilization(nested)[5], 99.0)
        self.assertEqual(engines_utilization(systemperf(0, 0, 0, 0, 0, 0, 0))[5], 0.0)

    def test_orphan_signature_matches_incident(self):
        observed = orphan_gpu_signature(
            systemperf(0, 0, 0, 0, 0, 100.0, 0), listing(e0_proc()), PACKAGE
        )
        self.assertTrue(observed["orphan"])
        self.assertTrue(observed["engine_pinned"])

    def test_running_job_cpu_is_not_the_orphan_signature(self):
        observed = orphan_gpu_signature(
            systemperf(0, 0, 0, 0, 0, 100.0, 0),
            listing(e0_proc(CPUUsage=12.0)),
            PACKAGE,
        )
        self.assertFalse(observed["orphan"])

    def test_idle_gpu_before_refuses_orphan(self):
        report = idle_gpu_report(
            systemperf(0, 0, 0, 0, 0, 97.0, 0), listing(e0_proc()), PACKAGE, when="before"
        )
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_gpu_signature")

    def test_idle_gpu_after_requires_process_gone_and_engine_down(self):
        gone = idle_gpu_report(
            systemperf(0, 0, 0, 0, 0, 0.0, 0), listing(), PACKAGE, when="after"
        )
        self.assertTrue(gone["ok"])
        still_running = idle_gpu_report(
            systemperf(0, 0, 0, 0, 0, 0.0, 0), listing(e0_proc()), PACKAGE, when="after"
        )
        self.assertEqual(still_running["reason"], "process_still_running")
        pinned = idle_gpu_report(
            systemperf(0, 0, 0, 0, 0, 100.0, 0), listing(), PACKAGE, when="after"
        )
        self.assertEqual(pinned["reason"], "engine_still_pinned")


class Csrf(unittest.TestCase):
    def test_csrf_cookie_is_echoed_as_header(self):
        cookies = SimpleCookie()
        cookies.load("CSRF-Token=abc123")
        headers = csrf_headers(cookies)
        self.assertEqual(headers["X-CSRF-Token"], "abc123")
        self.assertIn("CSRF-Token=abc123", headers["Cookie"])

    def test_ingest_set_cookie(self):
        cookies = SimpleCookie()
        token = ingest_cookies(cookies, [("Set-Cookie", "CSRF-Token=from-portal")])
        self.assertEqual(token, "from-portal")

    def test_terminate_path_uses_pid_query(self):
        self.assertEqual(terminate_path(4242), "/api/taskmanager/process?pid=4242")


class FakePortal(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.processes = listing(e0_proc())
        self.perf = systemperf(0, 0, 0, 0, 0, 100.0, 0)
        self.files = []

    def request(self, method, path, headers, body):
        self.calls.append((method, path, dict(headers), body))
        if method == "GET" and path == "/":
            return 200, [("Set-Cookie", "CSRF-Token=token-1")], b"ok"
        if path.endswith("/api/resourcemanager/processes"):
            return 200, [], json.dumps(self.processes).encode()
        if path.endswith("/api/resourcemanager/systemperf"):
            return 200, [], json.dumps(self.perf).encode()
        if method == "POST" and "filesystem/apps/file" in path:
            self.files.append(path)
            return 200, [], b""
        if method == "DELETE" and path.startswith("/api/taskmanager/process"):
            self.processes = listing()
            self.perf = systemperf(0, 0, 0, 0, 0, 0.0, 0)
            return 200, [], b""
        return 404, [], b""

    def portal(self):
        return LifecyclePortal(self.request, PACKAGE)

    def test_mutating_call_fetches_csrf_then_echoes_it(self):
        portal = self.portal()
        portal.terminate()
        methods = [c[0] for c in self.calls]
        self.assertEqual(methods[:3], ["GET", "GET", "DELETE"])
        self.assertEqual(self.calls[0][1], "/")
        delete = self.calls[2]
        self.assertEqual(delete[0], "DELETE")
        self.assertEqual(parse_qs(urlparse(delete[1]).query)["pid"], ["4242"])
        self.assertEqual(delete[2]["X-CSRF-Token"], "token-1")

    def test_teardown_writes_shutdown_then_terminates(self):
        portal = self.portal()
        report = portal.teardown(wait_seconds=0)
        self.assertTrue(any("shutdown" in path for method, path, *_ in self.calls if method == "POST"))
        self.assertTrue(any(c[0] == "DELETE" for c in self.calls))
        self.assertTrue(report["ok"])
        self.assertEqual(report["when"], "after")
        self.assertFalse(report["process_present"])

    def test_heartbeat_uploads_root_file(self):
        portal = self.portal()
        portal.write_heartbeat()
        posted = [path for method, path, *_ in self.calls if method == "POST"]
        self.assertTrue(posted)
        self.assertIn("host.heartbeat", posted[-1])
        self.assertNotIn("inbox", posted[-1].lower())

    def test_idle_gpu_before_uses_live_sample(self):
        portal = self.portal()
        report = portal.check_idle_gpu("before")
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_gpu_signature")


if __name__ == "__main__":
    unittest.main()
