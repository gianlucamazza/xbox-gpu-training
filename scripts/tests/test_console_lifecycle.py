"""Off-console tests for portable teardown, CSRF, config, and idle-GPU checks."""

import argparse
import json
import sys
import tempfile
import unittest
from http.cookies import SimpleCookie
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e0_console_lifecycle import (  # noqa: E402
    LifecyclePortal,
    LifecycleSettings,
    PortalAuthError,
    adapter_engines,
    add_settings_flags,
    app_processes,
    average_adapter_engines,
    csrf_cookie,
    csrf_headers,
    idle_gpu_report,
    ingest_cookies,
    load_settings,
    orphan_gpu_signature,
    pinned_engines,
    terminate_app_path,
    terminate_process_path,
)


PACKAGE = "Publisher.App_1.2.3.4_x64__family"
OTHER = "Publisher.Other_9.0.0.0_x64__family"


def systemperf(*engines, extra_adapters=None):
    adapters = [{"EnginesUtilization": list(engines)}]
    if extra_adapters:
        adapters.extend({"EnginesUtilization": list(values)} for values in extra_adapters)
    return {"GPUData": {"AvailableAdapters": adapters}}


def listing(*procs):
    return {"Processes": list(procs)}


def app_proc(**fields):
    return {
        "ImageName": "XgpuE0.exe",
        "ProcessId": 4242,
        "CPUUsage": 0.0,
        "PackageFullName": PACKAGE,
        **fields,
    }


class Detection(unittest.TestCase):
    def test_app_processes_by_image_package_or_family(self):
        self.assertEqual(
            len(
                app_processes(
                    listing({"ImageName": "Trainer.EXE"}),
                    images=("trainer.exe",),
                )
            ),
            1,
        )
        self.assertEqual(
            len(app_processes(listing({"PackageFullName": PACKAGE}), package=PACKAGE)),
            1,
        )
        self.assertEqual(
            len(
                app_processes(
                    listing({"PackageFullName": PACKAGE}),
                    package_family="Publisher.App",
                    images=(),
                )
            ),
            1,
        )
        self.assertEqual(
            app_processes(
                listing({"ImageName": "System Idle Process", "PackageFullName": OTHER}),
                package=PACKAGE,
                package_family="Publisher.App",
                images=("trainer.exe",),
            ),
            [],
        )

    def test_multi_adapter_engines(self):
        payload = systemperf(1, 0, 0, extra_adapters=[[0, 0, 99.0]])
        adapters = adapter_engines(payload)
        self.assertEqual(len(adapters), 2)
        self.assertEqual(adapters[1][2], 99.0)
        nested = {"system": payload}
        self.assertEqual(adapter_engines(nested)[1][2], 99.0)

    def test_any_engine_on_any_adapter_can_pin(self):
        adapters = [[1.0, 0.0], [0.0, 0.0, 97.0]]
        hits = pinned_engines(adapters, 90.0)
        self.assertEqual(hits, [{"adapter": 1, "engine": 2, "percent": 97.0}])
        filtered = pinned_engines(adapters, 90.0, engine_filter=(0,))
        self.assertEqual(filtered, [])

    def test_average_sample_window(self):
        mean = average_adapter_engines(
            [
                [[100.0, 0.0], [0.0]],
                [[0.0, 0.0], [0.0]],
                [[20.0, 0.0], [0.0]],
            ]
        )
        self.assertAlmostEqual(mean[0][0], 40.0)
        self.assertFalse(pinned_engines(mean, 90.0))

    def test_orphan_signature_is_not_tied_to_one_engine(self):
        observed = orphan_gpu_signature(
            [[0.0, 100.0]],
            listing(app_proc()),
            package=PACKAGE,
            threshold=90.0,
        )
        self.assertTrue(observed["orphan"])
        other_engine = orphan_gpu_signature(
            [[100.0, 0.0]],
            listing(app_proc()),
            package=PACKAGE,
            threshold=90.0,
        )
        self.assertTrue(other_engine["orphan"])
        busy_cpu = orphan_gpu_signature(
            [[0.0, 100.0]],
            listing(app_proc(CPUUsage=12.0)),
            package=PACKAGE,
        )
        self.assertFalse(busy_cpu["orphan"])

    def test_idle_gpu_before_and_after(self):
        orphan = orphan_gpu_signature(
            [[0.0, 0.0, 97.0]], listing(app_proc()), package=PACKAGE
        )
        self.assertEqual(
            idle_gpu_report(orphan, when="before")["reason"], "orphan_gpu_signature"
        )
        gone = orphan_gpu_signature([[0.0]], listing(), package=PACKAGE)
        self.assertTrue(idle_gpu_report(gone, when="after")["ok"])
        still = orphan_gpu_signature([[0.0]], listing(app_proc()), package=PACKAGE)
        self.assertEqual(
            idle_gpu_report(still, when="after")["reason"], "process_still_running"
        )
        pinned = orphan_gpu_signature([[0.0, 100.0]], listing(), package=PACKAGE)
        self.assertEqual(
            idle_gpu_report(pinned, when="after")["reason"], "engine_still_pinned"
        )


class Config(unittest.TestCase):
    def test_defaults(self):
        settings = load_settings(environ={})
        self.assertEqual(settings.idle_exit_seconds, 1800)
        self.assertEqual(settings.gpu_threshold_percent, 90.0)
        self.assertEqual(settings.process_images, ("xgpue0.exe",))
        self.assertEqual(settings.gpu_engine_filter, ())
        self.assertEqual(settings.terminate_by, ("package", "pid"))

    def test_env_and_file_and_cli_override(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "portal.env"
            path.write_text(
                "XGPU_IDLE_EXIT_SECONDS=120\n"
                "XGPU_GPU_THRESHOLD_PERCENT=80\n"
                "XGPU_PROCESS_IMAGES=Trainer.exe,Other.EXE\n"
            )
            parser = argparse.ArgumentParser()
            add_settings_flags(parser)
            args = parser.parse_args(
                [
                    "--idle-exit-seconds",
                    "15",
                    "--gpu-engine",
                    "2",
                    "--gpu-engine",
                    "4",
                    "--process-image",
                    "Custom.bin",
                ]
            )
            settings = load_settings(
                args,
                environ={
                    "XGPU_IDLE_EXIT_SECONDS": "60",
                    "XGPU_GPU_THRESHOLD_PERCENT": "70",
                },
                credentials=path,
            )
            self.assertEqual(settings.idle_exit_seconds, 15)
            self.assertEqual(settings.gpu_threshold_percent, 70.0)
            self.assertEqual(settings.process_images, ("custom.bin",))
            self.assertEqual(settings.gpu_engine_filter, (2, 4))

    def test_alias_env_names(self):
        settings = load_settings(
            environ={"UWP_DEVICE_URL": "https://example.test:11443", "XBOX_USER": "op"}
        )
        self.assertEqual(settings.portal_url, "https://example.test:11443")
        self.assertEqual(settings.user, "op")

    def test_credentials_file_via_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "portal.env"
            path.write_text(
                "XGPU_PORTAL_URL=https://example.test:11443\n"
                "XGPU_PORTAL_TLS_VERIFY=none\n"
                "XGPU_TERMINATE_BY=package\n"
            )
            settings = load_settings(
                environ={"XGPU_PORTAL_CREDENTIALS": str(path)},
            )
            self.assertEqual(settings.portal_url, "https://example.test:11443")
            self.assertEqual(settings.tls_verify, "none")
            self.assertEqual(settings.terminate_by, ("package",))


class Csrf(unittest.TestCase):
    def test_generic_csrf_cookie_names(self):
        cookies = SimpleCookie()
        cookies.load("XSRF-TOKEN=from-xsrf")
        self.assertEqual(csrf_cookie(cookies), ("XSRF-TOKEN", "from-xsrf"))
        headers = csrf_headers(cookies)
        self.assertEqual(headers["X-CSRF-Token"], "from-xsrf")

    def test_ingest_set_cookie(self):
        cookies = SimpleCookie()
        token = ingest_cookies(cookies, [("Set-Cookie", "CSRF-Token=from-portal")])
        self.assertEqual(token, "from-portal")

    def test_terminate_paths(self):
        self.assertEqual(terminate_process_path(4242), "/api/taskmanager/process?pid=4242")
        self.assertIn("/api/taskmanager/app?", terminate_app_path(PACKAGE))
        self.assertIn("package=", terminate_app_path(PACKAGE))
        self.assertIn("Publisher.App", terminate_app_path(PACKAGE))


class FakePortal(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.processes = listing(app_proc())
        self.perf = systemperf(0, 0, 0, 0, 0, 100.0, 0)
        self.delete_status = 200
        self.csrf_cookie = "CSRF-Token=token-1"
        self.settings = LifecycleSettings(
            package=PACKAGE,
            process_images=("xgpue0.exe",),
            gpu_sample_count=2,
            gpu_sample_interval_seconds=0,
            poll_interval_seconds=0,
            teardown_wait_seconds=0,
        )

    def request(self, method, path, headers, body):
        self.calls.append((method, path, dict(headers), body))
        if method == "GET" and path == "/":
            return 200, [("Set-Cookie", self.csrf_cookie)], b"ok"
        if path.endswith("/api/resourcemanager/processes"):
            return 200, [], json.dumps(self.processes).encode()
        if path.endswith("/api/resourcemanager/systemperf"):
            return 200, [], json.dumps(self.perf).encode()
        if method == "POST" and "filesystem/apps/file" in path:
            return 200, [], b""
        if method == "DELETE" and path.startswith("/api/taskmanager/"):
            status = self.delete_status
            if status == 200:
                self.processes = listing()
                self.perf = systemperf(0, 0, 0, 0, 0, 0.0, 0)
            self.delete_status = 200
            return status, [], b""
        return 404, [], b""

    def portal(self):
        return LifecyclePortal(self.request, self.settings)

    def test_mutating_call_fetches_csrf_then_echoes_it(self):
        portal = self.portal()
        portal.terminate()
        deletes = [c for c in self.calls if c[0] == "DELETE"]
        self.assertTrue(deletes)
        self.assertEqual(deletes[0][2]["X-CSRF-Token"], "token-1")
        self.assertTrue(any(c[1] == "/" for c in self.calls if c[0] == "GET"))

    def test_csrf_400_retries_once(self):
        self.delete_status = 403
        self.csrf_cookie = "XSRF-TOKEN=token-2"
        portal = self.portal()
        portal.terminate()
        deletes = [c for c in self.calls if c[0] == "DELETE"]
        self.assertGreaterEqual(len(deletes), 2)
        self.assertEqual(deletes[-1][2]["X-CSRF-Token"], "token-2")

    def test_csrf_400_twice_is_auth_error(self):
        def always_forbidden(method, path, headers, body):
            self.calls.append((method, path, dict(headers), body))
            if method == "GET" and path == "/":
                return 200, [("Set-Cookie", "CSRF-Token=tok")], b"ok"
            if path.endswith("/api/resourcemanager/processes"):
                return 200, [], json.dumps(self.processes).encode()
            return 403, [], b"no"

        portal = LifecyclePortal(always_forbidden, self.settings)
        with self.assertRaises(PortalAuthError):
            portal.terminate()

    def test_terminate_by_package_and_pid(self):
        portal = self.portal()
        result = portal.terminate()
        paths = [c[1] for c in self.calls if c[0] == "DELETE"]
        self.assertTrue(any(p.startswith("/api/taskmanager/app?") for p in paths))
        self.assertTrue(any(p.startswith("/api/taskmanager/process?") for p in paths))
        self.assertEqual(result["packages"], [PACKAGE])
        self.assertEqual(result["pids"], [4242])

    def test_teardown_and_idle_gpu_sample_window(self):
        portal = self.portal()
        report = portal.teardown(sleep=lambda _: None)
        self.assertTrue(any("shutdown" in c[1] for c in self.calls if c[0] == "POST"))
        self.assertTrue(report["ok"])
        self.assertFalse(report["process_present"])
        perfs = [c for c in self.calls if c[1].endswith("/systemperf")]
        self.assertGreaterEqual(len(perfs), 2)

    def test_idle_gpu_before_uses_live_sample(self):
        portal = self.portal()
        report = portal.check_idle_gpu("before", sleep=lambda _: None)
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_gpu_signature")


if __name__ == "__main__":
    unittest.main()
