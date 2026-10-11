"""Off-console tests for portable teardown, CSRF, config, and idle-app checks."""

import argparse
import base64
import json
import sys
import tempfile
import unittest
from http.cookies import SimpleCookie
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e0_console_lifecycle import (  # noqa: E402
    LifecyclePortal,
    LifecycleSettings,
    PortalAuthError,
    adapter_engines,
    add_settings_flags,
    app_processes,
    average_adapter_engines,
    classify_app_state,
    csrf_cookie,
    csrf_headers,
    decode_terminate_package,
    encode_package_name,
    gpu_advisory,
    ingest_cookies,
    last_job_ended_age_seconds,
    lifecycle_report,
    load_settings,
    parse_int_setting,
    parse_int_tuple,
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


def files(*names, modified=None, folders=()):
    items = []
    for name in names:
        item = {"Name": name, "Type": 32, "FileSize": 8}
        if modified is not None:
            item["DateModified"] = modified
        items.append(item)
    for name in folders:
        items.append({"Name": name, "Type": 16})
    return {"Items": items}


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

    def test_any_engine_on_any_adapter_can_pin_advisory(self):
        adapters = [[1.0, 0.0], [0.0, 0.0, 97.0]]
        hits = pinned_engines(adapters, 90.0)
        self.assertEqual(hits, [{"adapter": 1, "engine": 2, "percent": 97.0}])
        filtered = pinned_engines(adapters, 90.0, engine_filter=(0,))
        self.assertEqual(filtered, [])
        excluded = pinned_engines(adapters, 90.0, engine_exclude=(2,))
        self.assertEqual(excluded, [])

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

    def test_idle_ready_app_is_not_an_orphan_even_if_gpu_is_pinned(self):
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=(),
            heartbeat_age_seconds=None,
            last_job_ended_age_seconds=None,
            idle_exit_seconds=120,
            heartbeat_stale_seconds=120,
        )
        advisory = gpu_advisory([[0.0, 0.0, 0.0, 0.0, 0.0, 99.8]], threshold=90.0)
        report = lifecycle_report({**state, "gpu_advisory": advisory}, when="before")
        self.assertTrue(state["ready"])
        self.assertFalse(state["orphan"])
        self.assertTrue(report["ok"])
        self.assertTrue(advisory["engine_pinned"])
        self.assertFalse(advisory["decisive"])

    def test_orphan_is_stale_idle_after_a_finished_job(self):
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=(),
            heartbeat_age_seconds=4000,
            last_job_ended_age_seconds=4000,
            idle_exit_seconds=1800,
            heartbeat_stale_seconds=1800,
        )
        report = lifecycle_report(state, when="before")
        self.assertTrue(state["orphan"])
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_stale_idle")

    def test_queued_or_running_job_is_busy_not_orphan(self):
        queued = classify_app_state(
            process_present=True,
            worker={"state": "ready"},
            inbox=("trial.ready",),
        )
        self.assertTrue(queued["job_active"])
        self.assertFalse(queued["orphan"])
        self.assertEqual(lifecycle_report(queued, when="before")["reason"], "job_active")
        running = classify_app_state(
            process_present=True,
            worker={"state": "running", "active_job": {"job_id": "trial"}},
        )
        self.assertTrue(running["job_active"])
        self.assertEqual(lifecycle_report(running, when="before")["reason"], "job_active")

    def test_after_ignores_advisory_gpu(self):
        gone = classify_app_state(process_present=False)
        gone["gpu_advisory"] = gpu_advisory([[0.0, 0.0, 0.0, 0.0, 0.0, 100.0]])
        self.assertTrue(lifecycle_report(gone, when="after")["ok"])
        still = classify_app_state(process_present=True, worker={"state": "ready"})
        self.assertEqual(
            lifecycle_report(still, when="after")["reason"], "process_still_running"
        )

    def test_last_job_age_from_status_mtime(self):
        now = 2_000_000.0
        age = last_job_ended_age_seconds(
            [files("status.json", modified=now - 125)], now=now
        )
        self.assertAlmostEqual(age, 125.0)


class Config(unittest.TestCase):
    def test_defaults(self):
        settings = load_settings(environ={})
        self.assertEqual(settings.idle_exit_seconds, 1800)
        self.assertEqual(settings.gpu_threshold_percent, 90.0)
        self.assertEqual(settings.process_images, ("xgpue0.exe",))
        self.assertEqual(settings.gpu_engine_filter, ())
        self.assertEqual(settings.gpu_engine_exclude, ())
        self.assertEqual(settings.terminate_by, ("package", "pid"))

    def test_float_env_integers(self):
        self.assertEqual(parse_int_setting("20.0"), 20)
        self.assertEqual(parse_int_tuple("5.0, 2"), (5, 2))
        settings = load_settings(
            environ={
                "XGPU_IDLE_EXIT_SECONDS": "20.0",
                "XGPU_HEARTBEAT_STALE_SECONDS": "30.0",
                "XGPU_GPU_SAMPLE_COUNT": "4.0",
                "XGPU_PORTAL_PORT": "11443.0",
            }
        )
        self.assertEqual(settings.idle_exit_seconds, 20)
        self.assertEqual(settings.heartbeat_stale_seconds, 30)
        self.assertEqual(settings.gpu_sample_count, 4)
        self.assertEqual(settings.portal_port, 11443)

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
                    "15.0",
                    "--gpu-engine",
                    "2",
                    "--gpu-engine-exclude",
                    "5.0",
                    "--process-image",
                    "Custom.bin",
                ]
            )
            settings = load_settings(
                args,
                environ={
                    "XGPU_IDLE_EXIT_SECONDS": "60",
                    "XGPU_GPU_THRESHOLD_PERCENT": "70",
                    "XGPU_GPU_ENGINE_EXCLUDE": "1",
                },
                credentials=path,
            )
            self.assertEqual(settings.idle_exit_seconds, 15)
            self.assertEqual(settings.gpu_threshold_percent, 70.0)
            self.assertEqual(settings.process_images, ("custom.bin",))
            self.assertEqual(settings.gpu_engine_filter, (2,))
            self.assertEqual(settings.gpu_engine_exclude, (5,))

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

    def test_terminate_package_is_base64(self):
        path = terminate_app_path(PACKAGE)
        self.assertEqual(terminate_process_path(4242), "/api/taskmanager/process?pid=4242")
        self.assertTrue(path.startswith("/api/taskmanager/app?"))
        self.assertNotIn(PACKAGE, path)
        self.assertEqual(decode_terminate_package(path), PACKAGE)
        encoded = encode_package_name(PACKAGE)
        self.assertEqual(
            parse_qs(urlsplit(path).query)["package"][0], encoded
        )


class FakePortal(unittest.TestCase):
    def setUp(self):
        self.calls = []
        self.processes = listing(app_proc())
        self.perf = systemperf(0, 0, 0, 0, 0, 100.0, 0)
        self.delete_status = 200
        self.package_delete_status = 200
        self.csrf_cookie = "CSRF-Token=token-1"
        self.files = {
            "": files("worker.json", "device.json", "host.heartbeat", modified=1_900_000),
            "inbox": files(),
            "inbox/results": files(),
        }
        self.contents = {
            ("", "worker.json"): {"state": "ready", "active_job": None},
            ("", "device.json"): {"state": "ready"},
            ("", "host.heartbeat"): {"schema": "floppylm.host.heartbeat.v1"},
        }
        self.settings = LifecycleSettings(
            package=PACKAGE,
            process_images=("xgpue0.exe",),
            gpu_sample_count=2,
            gpu_sample_interval_seconds=0,
            poll_interval_seconds=0,
            teardown_wait_seconds=0,
            idle_exit_seconds=1800,
            heartbeat_stale_seconds=1800,
        )
        self.now = 2_000_000.0

    def request(self, method, path, headers, body):
        self.calls.append((method, path, dict(headers), body))
        if method == "GET" and path == "/":
            return 200, [("Set-Cookie", self.csrf_cookie)], b"ok"
        if path.endswith("/api/resourcemanager/processes"):
            return 200, [], json.dumps(self.processes).encode()
        if path.endswith("/api/resourcemanager/systemperf"):
            return 200, [], json.dumps(self.perf).encode()
        if "/api/filesystem/apps/files?" in path:
            directory = parse_qs(urlsplit(path).query).get("path", [""])[0]
            directory = directory.replace("\\LocalState\\", "").replace("\\LocalState", "")
            directory = directory.replace("\\", "/").strip("/")
            return 200, [], json.dumps(self.files.get(directory, files())).encode()
        if "/api/filesystem/apps/file?" in path:
            query = parse_qs(urlsplit(path).query)
            filename = (query.get("filename") or [""])[0]
            directory = (query.get("path") or [""])[0]
            directory = directory.replace("\\LocalState\\", "").replace("\\LocalState", "")
            directory = directory.replace("\\", "/").strip("/")
            if method == "GET":
                payload = self.contents.get((directory, filename))
                if payload is None:
                    return 404, [], b""
                data = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
                return 200, [], data
            if method == "POST":
                if filename == "shutdown":
                    self.contents[(directory, filename)] = body or b""
                    folder = self.files.setdefault("inbox", files())
                    names = [item["Name"] for item in folder.get("Items", [])]
                    if "shutdown" not in names:
                        folder.setdefault("Items", []).append(
                            {"Name": "shutdown", "Type": 32, "FileSize": 4}
                        )
                return 200, [], b""
            if method == "DELETE":
                folder = self.files.get(directory, files())
                folder["Items"] = [
                    item
                    for item in folder.get("Items", [])
                    if item.get("Name") != filename
                ]
                self.contents.pop((directory, filename), None)
                return 200, [], b""
        if method == "DELETE" and path.startswith("/api/taskmanager/app"):
            status = self.package_delete_status
            if status == 200:
                self.processes = listing()
                self.perf = systemperf(0, 0, 0, 0, 0, 0.0, 0)
            return status, [], b""
        if method == "DELETE" and path.startswith("/api/taskmanager/process"):
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
        self.package_delete_status = 200
        self.settings = replace_terminate(self.settings, ("pid",))
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

        portal = LifecyclePortal(
            always_forbidden, replace_terminate(self.settings, ("pid",))
        )
        with self.assertRaises(PortalAuthError):
            portal.terminate()

    def test_terminate_by_package_and_pid(self):
        portal = self.portal()
        result = portal.terminate()
        paths = [c[1] for c in self.calls if c[0] == "DELETE"]
        app_paths = [p for p in paths if p.startswith("/api/taskmanager/app?")]
        self.assertTrue(app_paths)
        self.assertEqual(decode_terminate_package(app_paths[0]), PACKAGE)
        self.assertTrue(any(p.startswith("/api/taskmanager/process?") for p in paths))
        self.assertEqual(result["packages"], [PACKAGE])
        self.assertEqual(result["pids"], [4242])

    def test_package_failure_still_kills_pid(self):
        self.package_delete_status = 400
        portal = self.portal()
        result = portal.terminate()
        self.assertTrue(result["package_errors"])
        self.assertEqual(result["pids"], [4242])
        self.assertFalse(self.processes["Processes"])

    def test_teardown_exit_on_shutdown_is_not_terminated(self):
        def request(method, path, headers, body):
            status, hdrs, payload = self.request(method, path, headers, body)
            if method == "POST" and "filename=shutdown" in path.replace("%5C", "\\"):
                self.processes = listing()
            if method == "POST" and "shutdown" in path:
                self.processes = listing()
            return status, hdrs, payload

        portal = LifecyclePortal(request, self.settings)
        report = portal.teardown(sleep=lambda _: None)
        self.assertTrue(report["ok"])
        self.assertFalse(report["terminated"])
        self.assertFalse(report["process_present"])

    def test_teardown_kill_sets_terminated(self):
        portal = self.portal()
        report = portal.teardown(sleep=lambda _: None)
        self.assertTrue(report["terminated"])
        self.assertTrue(report["ok"])

    def test_prepare_clears_stale_shutdown(self):
        self.files["inbox"] = files("shutdown")
        self.contents[("inbox", "shutdown")] = b"campaign-complete"
        portal = self.portal()
        cleared = portal.clear_stale_markers()
        self.assertEqual(cleared["cleared"], ["inbox/shutdown"])
        self.assertTrue(
            any(
                c[0] == "DELETE" and "shutdown" in c[1]
                for c in self.calls
            )
        )

    def test_before_check_reuses_idle_ready_app(self):
        portal = self.portal()
        report = portal.check_idle("before", sleep=lambda _: None)
        self.assertTrue(report["ok"])
        self.assertFalse(report["orphan"])
        self.assertTrue(report["gpu_advisory"]["engine_pinned"])
        self.assertFalse(report["gpu_advisory"]["decisive"])


def replace_terminate(settings, by):
    return LifecycleSettings(
        **{**settings.__dict__, "terminate_by": by}
    )


if __name__ == "__main__":
    unittest.main()
