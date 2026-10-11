#!/usr/bin/env python3
"""Portable Device Portal teardown and idle-GPU checks for the console worker.

No host, user, credential or filesystem path is baked in. Every value has a
documented default and can be overridden by a credentials/settings file, an
environment variable, or a CLI flag (CLI wins). See docs/e0/lifecycle.md.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import json
import os
import shlex
import ssl
import sys
import time
from dataclasses import dataclass, replace
from http.cookies import SimpleCookie
from pathlib import Path
from typing import Callable
from urllib.parse import urlencode, urlsplit

RequestFn = Callable[[str, str, dict, bytes | None], tuple[int, list[tuple[str, str]], bytes]]

DEFAULTS = {
    "idle_exit_seconds": 1800,
    "heartbeat_interval_seconds": 30,
    "heartbeat_stale_seconds": 1800,
    "poll_interval_seconds": 1.0,
    "gpu_threshold_percent": 90.0,
    "gpu_sample_count": 3,
    "gpu_sample_interval_seconds": 1.0,
    "gpu_engine_filter": (),
    "idle_cpu_percent": 1.0,
    "teardown_wait_seconds": 60.0,
    "portal_port": 11443,
    "tls_verify": "pin",
    "process_images": ("xgpue0.exe",),
    "terminate_by": ("package", "pid"),
    "shutdown_name": "shutdown",
    "heartbeat_name": "host.heartbeat",
}

ENV = {
    "idle_exit_seconds": "XGPU_IDLE_EXIT_SECONDS",
    "heartbeat_interval_seconds": "XGPU_HEARTBEAT_INTERVAL_SECONDS",
    "heartbeat_stale_seconds": "XGPU_HEARTBEAT_STALE_SECONDS",
    "poll_interval_seconds": "XGPU_POLL_INTERVAL_SECONDS",
    "gpu_threshold_percent": "XGPU_GPU_THRESHOLD_PERCENT",
    "gpu_sample_count": "XGPU_GPU_SAMPLE_COUNT",
    "gpu_sample_interval_seconds": "XGPU_GPU_SAMPLE_INTERVAL_SECONDS",
    "gpu_engine_filter": "XGPU_GPU_ENGINES",
    "idle_cpu_percent": "XGPU_IDLE_CPU_PERCENT",
    "teardown_wait_seconds": "XGPU_TEARDOWN_WAIT_SECONDS",
    "portal_url": "XGPU_PORTAL_URL",
    "portal_port": "XGPU_PORTAL_PORT",
    "tls_verify": "XGPU_PORTAL_TLS_VERIFY",
    "tls_pin": "XGPU_PORTAL_TLS_PIN",
    "process_images": "XGPU_PROCESS_IMAGES",
    "package": "XGPU_E0_PACKAGE",
    "package_family": "XGPU_PACKAGE_FAMILY",
    "terminate_by": "XGPU_TERMINATE_BY",
    "credentials": "XGPU_PORTAL_CREDENTIALS",
    "user": "XGPU_PORTAL_USER",
    "password": "XGPU_PORTAL_PASSWORD",
}

# Accepted aliases so existing Device Portal env names still work.
ENV_ALIASES = {
    "portal_url": ("UWP_DEVICE_URL",),
    "user": ("UWP_DEVICE_USER", "XBOX_USER"),
    "password": ("OPENAPPX_DEVICE_PASSWORD", "XBOX_PASS"),
    "tls_pin": ("OPENAPPX_DEVICE_PIN", "XBOX_CERT_SHA256"),
}

SHUTDOWN_NAME = DEFAULTS["shutdown_name"]
HEARTBEAT_NAME = DEFAULTS["heartbeat_name"]


class PortalAuthError(RuntimeError):
    """Device Portal rejected a mutating call (HTTP 400/403)."""


def split_csv(value: str) -> tuple[str, ...]:
    return tuple(part.strip() for part in value.split(",") if part.strip())


def parse_int_tuple(value: str) -> tuple[int, ...]:
    return tuple(int(part) for part in split_csv(value))


def read_settings_file(path: Path) -> dict[str, str]:
    """Parse KEY=VALUE lines (optional export, comments, quoting) without a shell."""
    values = {}
    for line in path.read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        key, sep, raw = line.removeprefix("export ").strip().partition("=")
        if sep and key.isidentifier():
            parts = shlex.split(raw, comments=True)
            values[key] = parts[0] if parts else ""
    return values


def env_value(mapping: dict[str, str], key: str) -> str | None:
    names = (ENV[key],) + ENV_ALIASES.get(key, ())
    for name in names:
        if mapping.get(name):
            return mapping[name]
    return None


@dataclass
class LifecycleSettings:
    idle_exit_seconds: int = DEFAULTS["idle_exit_seconds"]
    heartbeat_interval_seconds: int = DEFAULTS["heartbeat_interval_seconds"]
    heartbeat_stale_seconds: int = DEFAULTS["heartbeat_stale_seconds"]
    poll_interval_seconds: float = DEFAULTS["poll_interval_seconds"]
    gpu_threshold_percent: float = DEFAULTS["gpu_threshold_percent"]
    gpu_sample_count: int = DEFAULTS["gpu_sample_count"]
    gpu_sample_interval_seconds: float = DEFAULTS["gpu_sample_interval_seconds"]
    gpu_engine_filter: tuple[int, ...] = DEFAULTS["gpu_engine_filter"]
    idle_cpu_percent: float = DEFAULTS["idle_cpu_percent"]
    teardown_wait_seconds: float = DEFAULTS["teardown_wait_seconds"]
    portal_url: str = ""
    portal_port: int = DEFAULTS["portal_port"]
    tls_verify: str = DEFAULTS["tls_verify"]
    tls_pin: str = ""
    process_images: tuple[str, ...] = DEFAULTS["process_images"]
    package: str = ""
    package_family: str = ""
    terminate_by: tuple[str, ...] = DEFAULTS["terminate_by"]
    user: str = ""
    password: str = ""

    def overlay_mapping(self, mapping: dict[str, str]) -> "LifecycleSettings":
        updates = {}
        ints = (
            "idle_exit_seconds",
            "heartbeat_interval_seconds",
            "heartbeat_stale_seconds",
            "gpu_sample_count",
            "portal_port",
        )
        floats = (
            "poll_interval_seconds",
            "gpu_threshold_percent",
            "gpu_sample_interval_seconds",
            "idle_cpu_percent",
            "teardown_wait_seconds",
        )
        strings = (
            "portal_url",
            "tls_verify",
            "tls_pin",
            "package",
            "package_family",
            "user",
            "password",
        )
        for key in ints:
            raw = env_value(mapping, key)
            if raw is not None:
                updates[key] = int(raw)
        for key in floats:
            raw = env_value(mapping, key)
            if raw is not None:
                updates[key] = float(raw)
        for key in strings:
            raw = env_value(mapping, key)
            if raw is not None:
                updates[key] = raw
        raw = env_value(mapping, "gpu_engine_filter")
        if raw is not None:
            updates["gpu_engine_filter"] = parse_int_tuple(raw)
        raw = env_value(mapping, "process_images")
        if raw is not None:
            updates["process_images"] = tuple(name.lower() for name in split_csv(raw))
        raw = env_value(mapping, "terminate_by")
        if raw is not None:
            updates["terminate_by"] = split_csv(raw)
        return replace(self, **updates)

    def overlay_args(self, args: argparse.Namespace) -> "LifecycleSettings":
        updates = {}
        for key in (
            "idle_exit_seconds",
            "heartbeat_interval_seconds",
            "heartbeat_stale_seconds",
            "poll_interval_seconds",
            "gpu_threshold_percent",
            "gpu_sample_count",
            "gpu_sample_interval_seconds",
            "idle_cpu_percent",
            "teardown_wait_seconds",
            "portal_url",
            "portal_port",
            "tls_verify",
            "tls_pin",
            "package",
            "package_family",
        ):
            value = getattr(args, key, None)
            if value not in (None, ""):
                updates[key] = value
        if getattr(args, "gpu_engine", None):
            updates["gpu_engine_filter"] = tuple(args.gpu_engine)
        if getattr(args, "process_image", None):
            updates["process_images"] = tuple(name.lower() for name in args.process_image)
        if getattr(args, "terminate_by", None):
            updates["terminate_by"] = tuple(args.terminate_by)
        return replace(self, **updates)


def load_settings(
    args: argparse.Namespace | None = None,
    environ: dict[str, str] | None = None,
    credentials: Path | None = None,
) -> LifecycleSettings:
    """Defaults < credentials file < environment < CLI."""
    settings = LifecycleSettings()
    env = dict(os.environ if environ is None else environ)
    path = credentials
    if path is None and args is not None and getattr(args, "credentials", None):
        path = Path(args.credentials)
    if path is None and env.get(ENV["credentials"]):
        path = Path(env[ENV["credentials"]])
    if path is not None:
        settings = settings.overlay_mapping(read_settings_file(path))
    settings = settings.overlay_mapping(env)
    if args is not None:
        settings = settings.overlay_args(args)
    if settings.process_images:
        settings = replace(
            settings, process_images=tuple(name.lower() for name in settings.process_images)
        )
    return settings


def package_family_of(full_name: str) -> str:
    return full_name.split("_", 1)[0] if full_name else ""


def app_processes(
    listing: dict,
    *,
    package: str = "",
    package_family: str = "",
    images: tuple[str, ...] = DEFAULTS["process_images"],
) -> list[dict]:
    """Match by configurable image name(s) and/or package / package family."""
    wanted = {name.lower() for name in images}
    found = []
    for proc in listing.get("Processes", []):
        name = str(proc.get("ImageName") or proc.get("name") or "").lower()
        full = str(proc.get("PackageFullName") or "")
        family = package_family_of(full)
        if package and full == package:
            found.append(proc)
        elif package_family and family == package_family:
            found.append(proc)
        elif name and name in wanted:
            found.append(proc)
    return found


def process_id(proc: dict) -> int | None:
    for key in ("ProcessId", "pid", "Id"):
        value = proc.get(key)
        if isinstance(value, int) and value > 0:
            return value
        if isinstance(value, str) and value.isdigit():
            return int(value)
    return None


def cpu_usage(proc: dict) -> float:
    for key in ("CPUUsage", "cpu", "CpuUsage"):
        value = proc.get(key)
        if isinstance(value, (int, float)):
            return float(value)
    return 0.0


def terminate_process_path(pid: int) -> str:
    return "/api/taskmanager/process?" + urlencode({"pid": str(pid)})


def terminate_app_path(package: str) -> str:
    return "/api/taskmanager/app?" + urlencode({"package": package})


def csrf_cookie(cookies: SimpleCookie) -> tuple[str, str] | None:
    """Any cookie whose name looks like a CSRF token."""
    for key, morsel in cookies.items():
        lowered = key.lower()
        if "csrf" in lowered or "xsrf" in lowered:
            return key, morsel.value
    return None


def csrf_headers(cookies: SimpleCookie, extra: dict | None = None) -> dict[str, str]:
    headers = dict(extra or {})
    if cookies:
        headers["Cookie"] = "; ".join(
            f"{key}={morsel.value}" for key, morsel in cookies.items()
        )
    found = csrf_cookie(cookies)
    if found:
        headers["X-CSRF-Token"] = found[1]
    return headers


def ingest_cookies(cookies: SimpleCookie, response_headers: list[tuple[str, str]]) -> str:
    for key, value in response_headers:
        if key.lower() == "set-cookie":
            cookies.load(value)
    found = csrf_cookie(cookies)
    return found[1] if found else ""


def adapter_engines(systemperf: dict) -> list[list[float]]:
    """Every adapter's EnginesUtilization vector. No assumed adapter or engine."""
    gpu = systemperf.get("GPUData")
    if gpu is None:
        gpu = systemperf.get("system", {}).get("GPUData", {})
    adapters = gpu.get("AvailableAdapters") if isinstance(gpu, dict) else None
    if not adapters:
        return []
    out = []
    for adapter in adapters:
        raw = adapter.get("EnginesUtilization") or []
        out.append([float(x) for x in raw])
    return out


def average_adapter_engines(samples: list[list[list[float]]]) -> list[list[float]]:
    """Mean utilization per adapter/engine across a sample window."""
    if not samples:
        return []
    adapters = max((len(sample) for sample in samples), default=0)
    averaged = []
    for adapter in range(adapters):
        engines = max(
            (len(sample[adapter]) for sample in samples if adapter < len(sample)),
            default=0,
        )
        means = []
        for engine in range(engines):
            values = [
                sample[adapter][engine]
                for sample in samples
                if adapter < len(sample) and engine < len(sample[adapter])
            ]
            means.append(sum(values) / len(values) if values else 0.0)
        averaged.append(means)
    return averaged


def pinned_engines(
    adapters: list[list[float]],
    threshold: float,
    engine_filter: tuple[int, ...] = (),
) -> list[dict]:
    hits = []
    allowed = set(engine_filter) if engine_filter else None
    for adapter, engines in enumerate(adapters):
        for engine, percent in enumerate(engines):
            if allowed is not None and engine not in allowed:
                continue
            if percent >= threshold:
                hits.append(
                    {"adapter": adapter, "engine": engine, "percent": percent}
                )
    return hits


def orphan_gpu_signature(
    adapters: list[list[float]],
    listing: dict,
    *,
    package: str = "",
    package_family: str = "",
    images: tuple[str, ...] = DEFAULTS["process_images"],
    threshold: float = DEFAULTS["gpu_threshold_percent"],
    engine_filter: tuple[int, ...] = (),
    idle_cpu_percent: float = DEFAULTS["idle_cpu_percent"],
) -> dict:
    procs = app_processes(
        listing, package=package, package_family=package_family, images=images
    )
    pinned = pinned_engines(adapters, threshold, engine_filter)
    cpu_idle = bool(procs) and all(cpu_usage(p) <= idle_cpu_percent for p in procs)
    return {
        "process_present": bool(procs),
        "cpu_idle": cpu_idle,
        "engine_pinned": bool(pinned),
        "pinned": pinned,
        "adapters": adapters,
        "orphan": bool(procs) and cpu_idle and bool(pinned),
    }


def idle_gpu_report(observed: dict, *, when: str) -> dict:
    if when == "before":
        ok = not observed["orphan"]
        reason = "orphan_gpu_signature" if observed["orphan"] else "ok"
    elif when == "after":
        if observed["process_present"]:
            ok, reason = False, "process_still_running"
        elif observed["engine_pinned"]:
            ok, reason = False, "engine_still_pinned"
        else:
            ok, reason = True, "ok"
    else:
        raise ValueError("when must be before or after")
    return {"when": when, "ok": ok, "reason": reason, **observed}


class LifecyclePortal:
    def __init__(self, request_fn: RequestFn, settings: LifecycleSettings):
        self._request = request_fn
        self.settings = settings
        self._cookies = SimpleCookie()
        self._csrf = ""

    def _refresh_csrf(self) -> None:
        self._csrf = ""
        status, headers, _ = self._request("GET", "/", csrf_headers(self._cookies), None)
        self._csrf = ingest_cookies(self._cookies, headers)
        if status == 404:
            raise FileNotFoundError("/")
        if not self._csrf:
            raise RuntimeError("Device Portal did not issue a CSRF cookie")

    def request(
        self,
        method: str,
        path: str,
        body: bytes | None = None,
        headers: dict | None = None,
        *,
        json_result: bool = False,
        retrying: bool = False,
    ):
        if method in ("POST", "DELETE") and not self._csrf:
            self._refresh_csrf()
        status, response_headers, payload = self._request(
            method, path, csrf_headers(self._cookies, headers), body
        )
        self._csrf = ingest_cookies(self._cookies, response_headers) or self._csrf
        if method in ("POST", "DELETE") and status in (400, 403) and not retrying:
            self._refresh_csrf()
            return self.request(
                method, path, body, headers, json_result=json_result, retrying=True
            )
        if status == 404:
            raise FileNotFoundError(path)
        if status in (400, 403):
            raise PortalAuthError(f"Device Portal {method} failed: HTTP {status}")
        if not 200 <= status < 300:
            raise RuntimeError(f"Device Portal {method} failed: HTTP {status}")
        return json.loads(payload) if json_result else payload

    def processes(self) -> dict:
        return self.request("GET", "/api/resourcemanager/processes", json_result=True)

    def systemperf(self) -> dict:
        return self.request("GET", "/api/resourcemanager/systemperf", json_result=True)

    def sample_adapters(self, *, sleep=time.sleep) -> list[list[float]]:
        samples = []
        count = max(1, self.settings.gpu_sample_count)
        for index in range(count):
            samples.append(adapter_engines(self.systemperf()))
            if index + 1 < count and self.settings.gpu_sample_interval_seconds > 0:
                sleep(self.settings.gpu_sample_interval_seconds)
        return average_adapter_engines(samples)

    def matching_processes(self, listing: dict | None = None) -> list[dict]:
        listing = listing if listing is not None else self.processes()
        return app_processes(
            listing,
            package=self.settings.package,
            package_family=self.settings.package_family,
            images=self.settings.process_images,
        )

    def observe_gpu(self, listing: dict | None = None, *, sleep=time.sleep) -> dict:
        listing = listing if listing is not None else self.processes()
        return orphan_gpu_signature(
            self.sample_adapters(sleep=sleep),
            listing,
            package=self.settings.package,
            package_family=self.settings.package_family,
            images=self.settings.process_images,
            threshold=self.settings.gpu_threshold_percent,
            engine_filter=self.settings.gpu_engine_filter,
            idle_cpu_percent=self.settings.idle_cpu_percent,
        )

    def upload(self, filename: str, data: bytes, directory: str = "inbox") -> None:
        if not filename or any(c in filename for c in '\\/"\r\n'):
            raise ValueError("unsafe remote filename")
        if not self.settings.package:
            raise RuntimeError("package full name is required for LocalState uploads")
        boundary = "xgpu-lifecycle"
        header = (
            f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
            f'filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n'
        ).encode()
        payload = header + data + f"\r\n--{boundary}--\r\n".encode()
        query = urlencode(
            {
                "knownfolderid": "LocalAppData",
                "packagefullname": self.settings.package,
                "path": "\\LocalState\\" + directory.replace("/", "\\"),
                "filename": filename,
            }
        )
        self.request(
            "POST",
            "/api/filesystem/apps/file?" + query,
            payload,
            {"Content-Type": f"multipart/form-data; boundary={boundary}"},
        )

    def write_heartbeat(self) -> None:
        self.upload(
            HEARTBEAT_NAME,
            json.dumps({"schema": "floppylm.host.heartbeat.v1"}).encode(),
            directory="",
        )

    def write_shutdown(self, reason: str = "campaign-complete") -> None:
        self.upload(SHUTDOWN_NAME, reason.encode(), directory="inbox")

    def terminate(self) -> dict:
        listing = self.processes()
        procs = self.matching_processes(listing)
        result = {"packages": [], "pids": []}
        by = set(self.settings.terminate_by)
        packages = []
        if self.settings.package:
            packages.append(self.settings.package)
        for proc in procs:
            full = str(proc.get("PackageFullName") or "")
            if full and full not in packages:
                packages.append(full)
        if "package" in by:
            for package in packages:
                self.request("DELETE", terminate_app_path(package))
                result["packages"].append(package)
        if "pid" in by:
            for proc in procs:
                pid = process_id(proc)
                if pid is None:
                    continue
                self.request("DELETE", terminate_process_path(pid))
                result["pids"].append(pid)
        return result

    def wait_gone(self, timeout: float, *, sleep=time.sleep) -> bool:
        deadline = time.monotonic() + timeout
        while True:
            if not self.matching_processes():
                return True
            if time.monotonic() >= deadline:
                return not self.matching_processes()
            sleep(self.settings.poll_interval_seconds)

    def teardown(self, *, wait_seconds: float | None = None, sleep=time.sleep) -> dict:
        timeout = (
            self.settings.teardown_wait_seconds if wait_seconds is None else wait_seconds
        )
        present = bool(self.matching_processes())
        if present:
            try:
                self.write_shutdown("campaign-complete")
            except FileNotFoundError:
                pass
            if not self.wait_gone(timeout, sleep=sleep):
                self.terminate()
                if not self.wait_gone(timeout, sleep=sleep):
                    raise TimeoutError("console process still listed after terminate")
        after = idle_gpu_report(self.observe_gpu(sleep=sleep), when="after")
        after["terminated"] = present
        return after

    def check_idle_gpu(self, when: str, *, sleep=time.sleep) -> dict:
        return idle_gpu_report(self.observe_gpu(sleep=sleep), when=when)


def https_request_fn(settings: LifecycleSettings) -> RequestFn:
    if not settings.portal_url:
        raise ValueError("Device Portal URL is required (flag, env, or credentials file)")
    target = urlsplit(settings.portal_url)
    if target.scheme != "https":
        raise ValueError("Device Portal must be an HTTPS origin")
    host, port = target.hostname, target.port or settings.portal_port
    if not host:
        raise ValueError("Device Portal URL must include a host")
    pin = settings.tls_pin.replace(":", "").strip().lower()
    if settings.tls_verify == "pin":
        if len(pin) != 64 or any(c not in "0123456789abcdef" for c in pin):
            raise ValueError("tls_verify=pin requires a SHA-256 pin")
    auth = "Basic " + base64.b64encode(
        f"{settings.user}:{settings.password}".encode()
    ).decode()
    if settings.tls_verify == "system":
        context = ssl.create_default_context()
    else:
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE

    def request(
        method: str, path: str, headers: dict, body: bytes | None
    ) -> tuple[int, list[tuple[str, str]], bytes]:
        connection = http.client.HTTPSConnection(host, port, context=context, timeout=60)
        try:
            connection.connect()
            if settings.tls_verify == "pin":
                certificate = connection.sock.getpeercert(binary_form=True) or b""
                if hashlib.sha256(certificate).hexdigest() != pin:
                    raise ssl.SSLCertVerificationError(
                        "Device Portal certificate does not match the configured pin"
                    )
            connection.request(
                method,
                path,
                body=body,
                headers={"Authorization": auth, **headers},
            )
            response = connection.getresponse()
            return response.status, response.getheaders(), response.read()
        finally:
            connection.close()

    return request


def add_settings_flags(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--credentials", default="")
    parser.add_argument("--portal-url", default="")
    parser.add_argument("--portal-port", type=int, default=None)
    parser.add_argument("--tls-verify", choices=("pin", "none", "system"), default=None)
    parser.add_argument("--tls-pin", default="")
    parser.add_argument("--package", default="")
    parser.add_argument("--package-family", default="")
    parser.add_argument("--process-image", action="append", default=None)
    parser.add_argument("--idle-exit-seconds", type=int, default=None)
    parser.add_argument("--heartbeat-interval-seconds", type=int, default=None)
    parser.add_argument("--heartbeat-stale-seconds", type=int, default=None)
    parser.add_argument("--poll-interval-seconds", type=float, default=None)
    parser.add_argument("--gpu-threshold-percent", type=float, default=None)
    parser.add_argument("--gpu-sample-count", type=int, default=None)
    parser.add_argument("--gpu-sample-interval-seconds", type=float, default=None)
    parser.add_argument("--gpu-engine", action="append", type=int, default=None)
    parser.add_argument("--idle-cpu-percent", type=float, default=None)
    parser.add_argument("--teardown-wait-seconds", type=float, default=None)
    parser.add_argument(
        "--terminate-by",
        action="append",
        choices=("package", "pid"),
        default=None,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_settings_flags(parser)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("heartbeat", help="refresh LocalState/host.heartbeat")
    shutdown = sub.add_parser("shutdown", help="upload inbox/shutdown")
    shutdown.add_argument("--reason", default="campaign-complete")
    sub.add_parser("terminate", help="DELETE by package and/or pid")
    sub.add_parser("teardown", help="shutdown, wait, terminate if needed, idle-GPU after")
    idle = sub.add_parser("idle-gpu", help="sample systemperf before or after a campaign")
    idle.add_argument("--when", choices=("before", "after"), required=True)
    args = parser.parse_args(argv)

    settings = load_settings(args)
    portal = LifecyclePortal(https_request_fn(settings), settings)
    if args.command == "heartbeat":
        portal.write_heartbeat()
        print("heartbeat: ok", flush=True)
        return 0
    if args.command == "shutdown":
        portal.write_shutdown(args.reason)
        print("shutdown: ok", flush=True)
        return 0
    if args.command == "terminate":
        print(json.dumps(portal.terminate(), sort_keys=True), flush=True)
        return 0
    if args.command == "teardown":
        report = portal.teardown()
        print(json.dumps(report, sort_keys=True), flush=True)
        return 0 if report["ok"] else 1
    report = portal.check_idle_gpu(args.when)
    print(json.dumps(report, sort_keys=True), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
