#!/usr/bin/env python3
"""Host-side console process teardown and idle-GPU checks for XgpuE0.

Device lifecycle for the UWP worker is owned by this repository. The companion
campaign runner should call these commands before and after a campaign, and from
the host unit's stop path. This script never prints credentials.

Settings come from the environment, as for the dashboard recorder:
  UWP_DEVICE_URL            https://<console-ip>:11443
  UWP_DEVICE_USER           Device Portal user
  OPENAPPX_DEVICE_PASSWORD  Device Portal password (never on the command line)
  OPENAPPX_DEVICE_PIN       SHA-256 of the Device Portal TLS certificate
  XGPU_E0_PACKAGE           installed package full name (or --package)

Device Portal DELETE/POST require the CSRF-Token cookie echoed as X-CSRF-Token.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.client
import json
import os
import ssl
import sys
import time
from http.cookies import SimpleCookie
from typing import Callable
from urllib.parse import urlencode, urlsplit

APP_IMAGE = "xgpue0.exe"
# Engine 5 is the counter that stayed pinned in the 2026-10-11 orphan report.
# This is a detection threshold for that signature, not a power measurement.
PINNED_ENGINE_INDEX = 5
PINNED_ENGINE_PERCENT = 90.0
IDLE_CPU_PERCENT = 1.0
DEFAULT_WAIT_SECONDS = 60.0
SHUTDOWN_NAME = "shutdown"
HEARTBEAT_NAME = "host.heartbeat"

RequestFn = Callable[[str, str, dict, bytes | None], tuple[int, list[tuple[str, str]], bytes]]


def app_processes(listing: dict, package: str) -> list[dict]:
    """Processes that belong to the E0 package or image name."""
    found = []
    for proc in listing.get("Processes", []):
        name = str(proc.get("ImageName") or proc.get("name") or "")
        full = str(proc.get("PackageFullName") or "")
        if full == package or name.lower() == APP_IMAGE:
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


def terminate_path(pid: int) -> str:
    return "/api/taskmanager/process?" + urlencode({"pid": str(pid)})


def csrf_headers(cookies: SimpleCookie, extra: dict | None = None) -> dict[str, str]:
    """Echo CSRF-Token from the cookie jar as X-CSRF-Token."""
    headers = dict(extra or {})
    if cookies:
        headers["Cookie"] = "; ".join(
            f"{key}={morsel.value}" for key, morsel in cookies.items()
        )
    token = cookies.get("CSRF-Token") if cookies else None
    if token and token.value:
        headers["X-CSRF-Token"] = token.value
    return headers


def ingest_cookies(cookies: SimpleCookie, response_headers: list[tuple[str, str]]) -> str:
    """Load Set-Cookie headers and return the current CSRF-Token value."""
    for key, value in response_headers:
        if key.lower() == "set-cookie":
            cookies.load(value)
    token = cookies.get("CSRF-Token")
    return token.value if token else ""


def engines_utilization(systemperf: dict) -> list[float]:
    """Read EnginesUtilization from a Device Portal systemperf payload."""
    gpu = systemperf.get("GPUData")
    if gpu is None:
        gpu = systemperf.get("system", {}).get("GPUData", {})
    adapters = gpu.get("AvailableAdapters") if isinstance(gpu, dict) else None
    if not adapters:
        return []
    raw = adapters[0].get("EnginesUtilization") or []
    return [float(x) for x in raw]


def orphan_gpu_signature(
    systemperf: dict, listing: dict, package: str
) -> dict:
    """True when the E0 process is present, CPU is idle, and engine 5 is pinned."""
    procs = app_processes(listing, package)
    engines = engines_utilization(systemperf)
    pinned = (
        len(engines) > PINNED_ENGINE_INDEX
        and engines[PINNED_ENGINE_INDEX] >= PINNED_ENGINE_PERCENT
    )
    cpu_idle = bool(procs) and all(cpu_usage(p) <= IDLE_CPU_PERCENT for p in procs)
    return {
        "process_present": bool(procs),
        "cpu_idle": cpu_idle,
        "engine_pinned": pinned,
        "engines": engines,
        "orphan": bool(procs) and cpu_idle and pinned,
    }


def idle_gpu_report(
    systemperf: dict, listing: dict, package: str, *, when: str
) -> dict:
    """Before: refuse the 2026-10-11 orphan signature. After: process gone, engine not pinned."""
    observed = orphan_gpu_signature(systemperf, listing, package)
    if when == "before":
        ok = not observed["orphan"]
        reason = "orphan_gpu_signature" if observed["orphan"] else "ok"
    elif when == "after":
        ok = not observed["process_present"] and not observed["engine_pinned"]
        if observed["process_present"]:
            reason = "process_still_running"
        elif observed["engine_pinned"]:
            reason = "engine_still_pinned"
        else:
            reason = "ok"
    else:
        raise ValueError("when must be before or after")
    return {"when": when, "ok": ok, "reason": reason, **observed}


class LifecyclePortal:
    """HTTPS Device Portal client that echoes CSRF-Token on mutating calls."""

    def __init__(self, request_fn: RequestFn, package: str):
        self._request = request_fn
        self.package = package
        self._cookies = SimpleCookie()
        self._csrf = ""

    def request(
        self,
        method: str,
        path: str,
        body: bytes | None = None,
        headers: dict | None = None,
        *,
        json_result: bool = False,
    ):
        if method in ("POST", "DELETE") and not self._csrf:
            self.request("GET", "/")
            if not self._csrf:
                raise RuntimeError("Device Portal did not issue a CSRF cookie")
        status, response_headers, payload = self._request(
            method, path, csrf_headers(self._cookies, headers), body
        )
        self._csrf = ingest_cookies(self._cookies, response_headers) or self._csrf
        if status == 404:
            raise FileNotFoundError(path)
        if not 200 <= status < 300:
            raise RuntimeError(f"Device Portal {method} failed: HTTP {status}")
        return json.loads(payload) if json_result else payload

    def processes(self) -> dict:
        return self.request("GET", "/api/resourcemanager/processes", json_result=True)

    def systemperf(self) -> dict:
        return self.request("GET", "/api/resourcemanager/systemperf", json_result=True)

    def upload(self, filename: str, data: bytes, directory: str = "inbox") -> None:
        if not filename or any(c in filename for c in '\\/"\r\n'):
            raise ValueError("unsafe remote filename")
        boundary = "xgpu-lifecycle"
        header = (
            f'--{boundary}\r\nContent-Disposition: form-data; name="file"; '
            f'filename="{filename}"\r\nContent-Type: application/octet-stream\r\n\r\n'
        ).encode()
        payload = header + data + f"\r\n--{boundary}--\r\n".encode()
        query = urlencode(
            {
                "knownfolderid": "LocalAppData",
                "packagefullname": self.package,
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

    def terminate(self) -> list[int]:
        """DELETE each E0 pid. CSRF is obtained on the first mutating call."""
        killed = []
        for proc in app_processes(self.processes(), self.package):
            pid = process_id(proc)
            if pid is None:
                continue
            self.request("DELETE", terminate_path(pid))
            killed.append(pid)
        return killed

    def wait_gone(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if not app_processes(self.processes(), self.package):
                return True
            time.sleep(1)
        return not app_processes(self.processes(), self.package)

    def teardown(self, *, wait_seconds: float = DEFAULT_WAIT_SECONDS) -> dict:
        """Ask the app to exit, then DELETE the process if it is still listed."""
        present = bool(app_processes(self.processes(), self.package))
        if present:
            try:
                self.write_shutdown("campaign-complete")
            except FileNotFoundError:
                # LocalState is 404 when the process is already gone.
                pass
            if not self.wait_gone(wait_seconds):
                self.terminate()
                if not self.wait_gone(wait_seconds):
                    raise TimeoutError("XgpuE0 process still listed after terminate")
        after = idle_gpu_report(self.systemperf(), self.processes(), self.package, when="after")
        after["terminated"] = present
        return after

    def check_idle_gpu(self, when: str) -> dict:
        return idle_gpu_report(self.systemperf(), self.processes(), self.package, when=when)


def https_request_fn(url: str, user: str, password: str, pin: str) -> RequestFn:
    target = urlsplit(url)
    if target.scheme != "https":
        raise ValueError("Device Portal must be an HTTPS origin")
    host, port = target.hostname, target.port or 11443
    pin = pin.replace(":", "").lower()
    auth = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    def request(
        method: str, path: str, headers: dict, body: bytes | None
    ) -> tuple[int, list[tuple[str, str]], bytes]:
        connection = http.client.HTTPSConnection(host, port, context=context, timeout=60)
        try:
            connection.connect()
            certificate = connection.sock.getpeercert(binary_form=True) or b""
            if hashlib.sha256(certificate).hexdigest() != pin:
                raise ssl.SSLCertVerificationError(
                    "Device Portal certificate does not match OPENAPPX_DEVICE_PIN"
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


def portal_from_env(package: str) -> LifecyclePortal:
    keys = (
        "UWP_DEVICE_URL",
        "UWP_DEVICE_USER",
        "OPENAPPX_DEVICE_PASSWORD",
        "OPENAPPX_DEVICE_PIN",
    )
    missing = [k for k in keys if not os.environ.get(k)]
    package = package or os.environ.get("XGPU_E0_PACKAGE", "")
    if not package:
        missing.append("XGPU_E0_PACKAGE (or --package)")
    if missing:
        raise SystemExit("missing settings: " + ", ".join(missing))
    url, user, password, pin = (os.environ[k] for k in keys)
    return LifecyclePortal(https_request_fn(url, user, password, pin), package)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--package", default="")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("heartbeat", help="refresh LocalState/host.heartbeat")
    shutdown = sub.add_parser("shutdown", help="upload inbox/shutdown")
    shutdown.add_argument("--reason", default="campaign-complete")
    sub.add_parser("terminate", help="DELETE the E0 process via Device Portal")
    teardown = sub.add_parser(
        "teardown", help="shutdown, wait, terminate if needed, then idle-GPU after"
    )
    teardown.add_argument("--wait-seconds", type=float, default=DEFAULT_WAIT_SECONDS)
    idle = sub.add_parser("idle-gpu", help="sample systemperf before or after a campaign")
    idle.add_argument("--when", choices=("before", "after"), required=True)
    args = parser.parse_args(argv)

    portal = portal_from_env(args.package)
    if args.command == "heartbeat":
        portal.write_heartbeat()
        print("heartbeat: ok", flush=True)
        return 0
    if args.command == "shutdown":
        portal.write_shutdown(args.reason)
        print("shutdown: ok", flush=True)
        return 0
    if args.command == "terminate":
        killed = portal.terminate()
        print(json.dumps({"terminated_pids": killed}), flush=True)
        return 0
    if args.command == "teardown":
        report = portal.teardown(wait_seconds=args.wait_seconds)
        print(json.dumps(report, sort_keys=True), flush=True)
        return 0 if report["ok"] else 1
    report = portal.check_idle_gpu(args.when)
    print(json.dumps(report, sort_keys=True), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
