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
    START_TIME_UNKNOWN_WARNING,
    LifecyclePortal,
    LifecycleSettings,
    PortalAuthError,
    adapter_engines,
    add_settings_flags,
    app_processes,
    average_adapter_engines,
    classify_app_state,
    completion_job_ids,
    csrf_cookie,
    csrf_headers,
    decode_terminate_package,
    encode_package_name,
    gpu_advisory,
    heartbeat_for_instance,
    ingest_cookies,
    last_job_ended_age_seconds,
    last_job_ended_at,
    lifecycle_report,
    live_claimed_without_result,
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
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=(),
            now=now,
            app_start_seconds=now - 30,
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
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            app_start_seconds=now - 4000,
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
            worker={"state": "ready", "active_job": None},
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

    def test_finished_claimed_history_is_not_job_active(self):
        now = 2_000_000.0
        claimed = [f"job-{i:04d}.claimed" for i in range(32)]
        results = {name[: -len(".claimed")] for name in claimed}
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=[{"name": name, "modified": now - 10_000} for name in claimed],
            result_job_ids=results,
            now=now,
            app_start_seconds=now - 30,
            last_job_ended_age_seconds=8_000,
            idle_exit_seconds=1800,
        )
        self.assertFalse(state["job_active"])
        self.assertFalse(state["claimed"])
        self.assertTrue(state["ready"])
        self.assertFalse(state["orphan"])

    def test_fresh_app_with_old_history_is_not_orphan(self):
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            app_start_seconds=now - 12,
            heartbeat_age_seconds=None,
            last_job_ended_age_seconds=10_000,
            idle_exit_seconds=1800,
            heartbeat_stale_seconds=1800,
        )
        report = lifecycle_report(state, when="before")
        self.assertTrue(state["ready"])
        self.assertLess(state["app_age_seconds"], 1800)
        self.assertFalse(state["orphan"])
        self.assertTrue(report["ok"])

    def test_no_process_with_stale_history_is_idle(self):
        now = 2_000_000.0
        claimed = [f"job-{i:04d}.claimed" for i in range(64)]
        state = classify_app_state(
            process_present=False,
            worker={"state": "ready", "active_job": None},
            inbox=tuple(claimed) + ("leftover.ready",),
            result_job_ids={name[: -len(".claimed")] for name in claimed},
            now=now,
            last_job_ended_age_seconds=50_000,
            heartbeat_age_seconds=50_000,
            idle_exit_seconds=1800,
        )
        self.assertFalse(state["process_present"])
        self.assertFalse(state["job_active"])
        self.assertFalse(state["orphan"])
        self.assertIsNone(state["worker_state"])
        self.assertIsNone(state["app_age_seconds"])
        self.assertTrue(lifecycle_report(state, when="before")["ok"])

    def test_unknown_start_is_not_orphan(self):
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            heartbeat_age_seconds=917,
            last_job_ended_age_seconds=917,
            idle_exit_seconds=300,
            heartbeat_stale_seconds=300,
        )
        report = lifecycle_report(state, when="before")
        self.assertIsNone(state["orphan"])
        self.assertFalse(state["start_time_known"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "start_time_unknown")
        self.assertEqual(report["warning"], START_TIME_UNKNOWN_WARNING)

    def test_unknown_start_strict_refuses(self):
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            last_job_ended_age_seconds=917,
            idle_exit_seconds=300,
        )
        report = lifecycle_report(state, when="before", orphan_strict=True)
        self.assertIsNone(state["orphan"])
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "start_time_unknown")
        self.assertNotIn("warning", report)

    def test_stale_heartbeat_from_other_instance_is_ignored(self):
        now = 2_000_000.0
        started = now - 18
        written = heartbeat_for_instance(
            {
                "schema": "floppylm.host.heartbeat.v1",
                "instance_id": "previous-instance",
                "written_at": now - 917,
            },
            instance_id="current-instance",
            started_at=started,
            now=now,
        )
        self.assertIsNone(written)
        predates = heartbeat_for_instance(
            {
                "schema": "floppylm.host.heartbeat.v1",
                "instance_id": "current-instance",
                "written_at": started - 5,
            },
            instance_id="current-instance",
            started_at=started,
            now=now,
        )
        self.assertIsNone(predates)
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            app_start_seconds=started,
            instance_id="current-instance",
            heartbeat_written_at=written,
            last_job_ended_age_seconds=917,
            idle_exit_seconds=300,
            heartbeat_stale_seconds=300,
        )
        report = lifecycle_report(state, when="before")
        self.assertLess(state["app_age_seconds"], 300)
        self.assertIsNone(state["heartbeat_age_seconds"])
        self.assertIsNone(state["last_job_ended_age_seconds"])
        self.assertFalse(state["orphan"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "ok")

    def test_known_start_stale_idle_is_orphan(self):
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            now=now,
            app_start_seconds=now - 4000,
            instance_id="same-instance",
            heartbeat_written_at=now - 4000,
            last_job_ended_at=now - 4000,
            idle_exit_seconds=1800,
            heartbeat_stale_seconds=1800,
        )
        report = lifecycle_report(state, when="before")
        self.assertTrue(state["start_time_known"])
        self.assertTrue(state["orphan"])
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_stale_idle")

    def test_claimed_without_result_only_if_newer_than_app_start(self):
        now = 2_000_000.0
        app_start = now - 20
        live = live_claimed_without_result(
            [
                {"name": "old.claimed", "modified": app_start - 5},
                {"name": "fresh.claimed", "modified": app_start + 1},
                {"name": "done.claimed", "modified": app_start + 2},
            ],
            {"done"},
            app_start,
        )
        self.assertEqual(live, ("fresh.claimed",))
        fallback = classify_app_state(
            process_present=True,
            worker={},
            inbox=[{"name": "fresh.claimed", "modified": app_start + 1}],
            result_job_ids=(),
            now=now,
            app_start_seconds=app_start,
            worker_fresh=False,
        )
        self.assertTrue(fallback["job_active"])
        self.assertEqual(fallback["claimed_live"], ("fresh.claimed",))
        trusted = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=[{"name": "fresh.claimed", "modified": app_start + 1}],
            result_job_ids=(),
            now=now,
            app_start_seconds=app_start,
        )
        self.assertFalse(trusted["job_active"])
        self.assertTrue(trusted["worker_fresh"])
        self.assertEqual(trusted["claimed_live"], ())

    def test_fixture_actual_json_is_completion_not_live_claimed(self):
        now = 2_000_000.0
        app_start = now - 60
        inbox = [
            {"name": "reuse.claimed", "modified": now - 9},
            {"name": "reuse.actual.json", "modified": now - 9},
            {"name": "reuse.job.json", "modified": now - 9},
            {"name": "reuse.owner.json", "modified": now - 9},
        ]
        self.assertIn("reuse", completion_job_ids(inbox, set()))
        self.assertEqual(live_claimed_without_result(inbox, set(), app_start), ())
        state = classify_app_state(
            process_present=True,
            worker={"state": "ready", "active_job": None},
            inbox=inbox,
            result_job_ids=(),
            now=now,
            app_start_seconds=app_start,
            last_job_ended_at=now - 9,
            idle_exit_seconds=300,
        )
        report = lifecycle_report(state, when="before")
        self.assertFalse(state["job_active"])
        self.assertFalse(state["claimed"])
        self.assertTrue(state["ready"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "ok")

    def test_processed_cancel_is_completion(self):
        now = 2_000_000.0
        app_start = now - 40
        inbox = [
            {"name": "trial.claimed", "modified": now - 20},
            {"name": "trial.cancel", "modified": now - 15},
        ]
        self.assertEqual(
            live_claimed_without_result(inbox, {"trial"}, app_start), ()
        )
        fallback = classify_app_state(
            process_present=True,
            worker={},
            inbox=inbox,
            result_job_ids={"trial"},
            now=now,
            app_start_seconds=app_start,
            worker_fresh=False,
        )
        self.assertFalse(fallback["job_active"])

    def test_stale_worker_claimed_without_artefact_is_live(self):
        now = 2_000_000.0
        state = classify_app_state(
            process_present=True,
            worker={},
            inbox=[{"name": "inflight.claimed", "modified": now - 5}],
            result_job_ids=(),
            now=now,
            app_start_seconds=now - 20,
            worker_fresh=False,
        )
        self.assertTrue(state["job_active"])
        self.assertEqual(state["claimed_live"], ("inflight.claimed",))

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

    def test_last_job_age_from_fixture_actual_json(self):
        now = 2_000_000.0
        ended = last_job_ended_at(
            [],
            now=now,
            inbox_items=[{"name": "reuse.actual.json", "modified": now - 9}],
        )
        self.assertAlmostEqual(ended, now - 9)


class Config(unittest.TestCase):
    def test_defaults(self):
        settings = load_settings(environ={})
        self.assertEqual(settings.idle_exit_seconds, 1800)
        self.assertEqual(settings.gpu_threshold_percent, 90.0)
        self.assertEqual(settings.process_images, ("xgpue0.exe",))
        self.assertEqual(settings.gpu_engine_filter, ())
        self.assertEqual(settings.gpu_engine_exclude, ())
        self.assertEqual(settings.terminate_by, ("package", "pid"))
        self.assertEqual(settings.instance_name, "instance.json")
        self.assertFalse(settings.orphan_strict)

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

    def test_orphan_strict_and_instance_name_override(self):
        settings = load_settings(
            environ={
                "XGPU_ORPHAN_STRICT": "1",
                "XGPU_INSTANCE_NAME": "boot.json",
            }
        )
        self.assertTrue(settings.orphan_strict)
        self.assertEqual(settings.instance_name, "boot.json")
        parser = argparse.ArgumentParser()
        add_settings_flags(parser)
        args = parser.parse_args(
            ["--no-orphan-strict", "--instance-name", "instance.json"]
        )
        settings = load_settings(args, environ={"XGPU_ORPHAN_STRICT": "1"})
        self.assertFalse(settings.orphan_strict)
        self.assertEqual(settings.instance_name, "instance.json")

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
        self.processes = listing(app_proc(CreateTime=self.now - 15))

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
                stored = body or b""
                marker = b"\r\n\r\n"
                if marker in stored:
                    stored = stored.split(marker, 1)[1]
                    for suffix in (b"\r\n--xgpu-lifecycle--\r\n", b"\r\n--xgpu-lifecycle--"):
                        if stored.endswith(suffix):
                            stored = stored[: -len(suffix)]
                            break
                try:
                    self.contents[(directory, filename)] = json.loads(stored)
                except (json.JSONDecodeError, UnicodeDecodeError):
                    self.contents[(directory, filename)] = stored
                folder = self.files.setdefault(directory, files())
                names = [item["Name"] for item in folder.get("Items", [])]
                if filename not in names:
                    folder.setdefault("Items", []).append(
                        {"Name": filename, "Type": 32, "FileSize": len(stored)}
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

    def test_prepare_does_not_report_clear_when_marker_absent(self):
        self.files["inbox"] = files()
        portal = self.portal()
        cleared = portal.clear_stale_markers()
        self.assertEqual(cleared["cleared"], [])
        self.assertFalse(
            any(c[0] == "DELETE" and "shutdown" in c[1] for c in self.calls)
        )

    def test_before_check_reuses_idle_ready_app(self):
        portal = self.portal()
        report = portal.check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertTrue(report["ok"])
        self.assertFalse(report["orphan"])
        self.assertTrue(report["gpu_advisory"]["engine_pinned"])
        self.assertFalse(report["gpu_advisory"]["decisive"])

    def test_observe_ignores_finished_claimed_history(self):
        claimed = [f"job-{i:04d}.claimed" for i in range(96)]
        folders = [name[: -len(".claimed")] for name in claimed]
        self.files["inbox"] = files(*claimed, modified=self.now - 20_000)
        self.files["inbox/results"] = files(folders=folders)
        for job_id in folders:
            self.files[f"inbox/results/{job_id}"] = files(
                "status.json", modified=self.now - 20_000
            )
        self.processes = listing()
        portal = self.portal()
        gone = portal.check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertFalse(gone["process_present"])
        self.assertFalse(gone["job_active"])
        self.assertFalse(gone["orphan"])
        self.assertTrue(gone["ok"])
        self.processes = listing(app_proc(CreateTime=self.now - 8))
        fresh = portal.check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertTrue(fresh["process_present"])
        self.assertTrue(fresh["ready"])
        self.assertFalse(fresh["job_active"])
        self.assertFalse(fresh["orphan"])
        self.assertTrue(fresh["ok"])
        self.processes = listing()
        gone_again = portal.check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertIsNone(gone_again["worker_state"])
        self.assertIsNone(gone_again["app_age_seconds"])

    def test_observe_unknown_start_without_instance_or_createtime(self):
        self.processes = listing(app_proc())
        self.files[""] = files("worker.json", "device.json", "host.heartbeat")
        self.contents[("", "host.heartbeat")] = {
            "schema": "floppylm.host.heartbeat.v1",
            "written_at": self.now - 917,
        }
        self.settings = LifecycleSettings(
            **{**self.settings.__dict__, "idle_exit_seconds": 300, "heartbeat_stale_seconds": 300}
        )
        report = self.portal().check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertIsNone(report["orphan"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "start_time_unknown")
        self.assertEqual(report["warning"], START_TIME_UNKNOWN_WARNING)
        self.settings = LifecycleSettings(
            **{**self.settings.__dict__, "orphan_strict": True}
        )
        refused = self.portal().check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertFalse(refused["ok"])
        self.assertEqual(refused["reason"], "start_time_unknown")

    def test_observe_ignores_foreign_heartbeat_on_fresh_instance(self):
        started = self.now - 18
        self.processes = listing(app_proc())
        self.files[""] = files(
            "worker.json", "device.json", "host.heartbeat", "instance.json",
            modified=self.now - 917,
        )
        self.contents[("", "instance.json")] = {
            "schema": "xgpu.instance.v1",
            "instance_id": "fresh-id",
            "started_at": started,
            "pid": 4242,
        }
        self.contents[("", "host.heartbeat")] = {
            "schema": "floppylm.host.heartbeat.v1",
            "instance_id": "old-id",
            "written_at": self.now - 917,
        }
        self.files["inbox/results"] = files(folders=("old-job",))
        self.files["inbox/results/old-job"] = files(
            "status.json", modified=self.now - 917
        )
        self.settings = LifecycleSettings(
            **{**self.settings.__dict__, "idle_exit_seconds": 300, "heartbeat_stale_seconds": 300}
        )
        report = self.portal().check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertAlmostEqual(report["app_age_seconds"], 18)
        self.assertIsNone(report["heartbeat_age_seconds"])
        self.assertIsNone(report["last_job_ended_age_seconds"])
        self.assertFalse(report["orphan"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "ok")

    def test_observe_known_start_stale_idle_is_orphan(self):
        self.processes = listing(app_proc())
        self.files[""] = files(
            "worker.json", "device.json", "host.heartbeat", "instance.json"
        )
        self.contents[("", "instance.json")] = {
            "schema": "xgpu.instance.v1",
            "instance_id": "same-id",
            "started_at": self.now - 4000,
            "pid": 4242,
        }
        self.contents[("", "host.heartbeat")] = {
            "schema": "floppylm.host.heartbeat.v1",
            "instance_id": "same-id",
            "written_at": self.now - 4000,
        }
        self.files["inbox/results"] = files(folders=("old-job",))
        self.files["inbox/results/old-job"] = files(
            "status.json", modified=self.now - 4000
        )
        report = self.portal().check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertTrue(report["start_time_known"])
        self.assertTrue(report["orphan"])
        self.assertFalse(report["ok"])
        self.assertEqual(report["reason"], "orphan_stale_idle")

    def test_write_heartbeat_carries_instance_id(self):
        self.contents[("", "instance.json")] = {
            "schema": "xgpu.instance.v1",
            "instance_id": "live-id",
            "started_at": self.now - 10,
            "pid": 4242,
        }
        self.files[""] = files("worker.json", "device.json", "instance.json")
        self.portal().write_heartbeat()
        payload = self.contents[("", "host.heartbeat")]
        self.assertEqual(payload["schema"], "floppylm.host.heartbeat.v1")
        self.assertEqual(payload["instance_id"], "live-id")
        self.assertIn("written_at", payload)

    def test_observe_fixture_reuse_probe_is_not_job_active(self):
        self.files["inbox"] = files(
            "reuse.claimed",
            "reuse.actual.json",
            "reuse.job.json",
            "reuse.owner.json",
            modified=self.now - 9,
        )
        self.contents[("", "instance.json")] = {
            "schema": "xgpu.instance.v1",
            "instance_id": "fresh-id",
            "started_at": self.now - 60,
            "pid": 4242,
        }
        self.files[""] = files(
            "worker.json", "device.json", "instance.json", modified=self.now - 2
        )
        self.processes = listing(app_proc())
        report = self.portal().check_idle("before", sleep=lambda _: None, now=self.now)
        self.assertFalse(report["job_active"])
        self.assertTrue(report["ready"])
        self.assertTrue(report["ok"])
        self.assertEqual(report["reason"], "ok")
        self.assertAlmostEqual(report["last_job_ended_age_seconds"], 9)

    def test_teardown_removes_host_heartbeat(self):
        report = self.portal().teardown(sleep=lambda _: None)
        self.assertIn("host.heartbeat", report["cleared"])
        self.assertNotIn(("", "host.heartbeat"), self.contents)
        self.assertFalse(
            any(
                item.get("Name") == "host.heartbeat"
                for item in self.files.get("", {}).get("Items", [])
            )
        )


def replace_terminate(settings, by):
    return LifecycleSettings(
        **{**settings.__dict__, "terminate_by": by}
    )


if __name__ == "__main__":
    unittest.main()
