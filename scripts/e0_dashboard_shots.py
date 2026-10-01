#!/usr/bin/env python3
"""Package-bound screenshots of the on-console E0 dashboard.

Read-only on the console: Device Portal GETs only (screenshot, device.json and
the watched job's status.json); it never submits, cancels or uploads anything.
Every capture is recorded in <out>/shots.json with its SHA-256, the installed
package and commit from device.json and the job status it shows.

Settings come from the environment, as for openappx:
  UWP_DEVICE_URL            https://<console-ip>:11443
  UWP_DEVICE_USER           Device Portal user
  OPENAPPX_DEVICE_PASSWORD  Device Portal password (never on the command line)
  OPENAPPX_DEVICE_PIN       SHA-256 of the Device Portal TLS certificate
  XGPU_E0_PACKAGE           installed package full name (or --package)
"""

import argparse
import base64
import hashlib
import http.client
import json
import os
import ssl
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlencode, urlsplit

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
FINAL_STATES = ("completed", "interrupted", "failed")
# Status fields recorded next to each shot: what the dashboard was showing.
STATUS_KEYS = (
    "job_id",
    "state",
    "phase",
    "trunk_step",
    "cooldown_step",
    "cooldown_end",
    "last_loss",
    "wall_seconds",
)


def png_size(data: bytes) -> tuple[int, int]:
    """Width and height of a PNG; raises ValueError for anything else."""
    if len(data) < 24 or not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        raise ValueError("not a PNG image")
    return struct.unpack(">II", data[16:24])


def milestone(status: dict, seen: set[str], running_step: int = 0) -> str | None:
    """Label of the first status showing a new dashboard state, else None.

    running: first trunk status at or after running_step (so the chart has a
    curve); cooldown-N: first status of branch N's cooldown; the final state.
    """
    state = status.get("state")
    if state in FINAL_STATES:
        label = state
    elif state != "running" or "schedule" not in status:
        return None
    elif status.get("phase") == "cooldown":
        ends = status["schedule"].get("ends", [])
        end = status.get("cooldown_end")
        if end not in ends:
            return None
        label = f"cooldown-{ends.index(end) + 1}"
    elif status.get("trunk_step", 0) >= running_step:
        label = "running"
    else:
        return None
    return None if label in seen else label


class Portal:
    def __init__(self, url: str, user: str, password: str, pin: str, package: str):
        target = urlsplit(url)
        if target.scheme != "https":
            raise ValueError("Device Portal must be an HTTPS origin")
        self.host, self.port = target.hostname, target.port or 11443
        self.pin = pin.replace(":", "").lower()
        self.package = package
        self.auth = "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode()
        # Self-signed certificate: trust is the pinned fingerprint, checked below.
        self.context = ssl.create_default_context()
        self.context.check_hostname = False
        self.context.verify_mode = ssl.CERT_NONE

    @classmethod
    def from_env(cls, package: str) -> "Portal":
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
        return cls(*(os.environ[k] for k in keys), package)

    def get(self, path: str) -> bytes:
        connection = http.client.HTTPSConnection(
            self.host, self.port, context=self.context, timeout=60
        )
        try:
            connection.connect()
            certificate = connection.sock.getpeercert(binary_form=True) or b""
            if hashlib.sha256(certificate).hexdigest() != self.pin:
                raise ssl.SSLCertVerificationError(
                    "Device Portal certificate does not match OPENAPPX_DEVICE_PIN"
                )
            connection.request("GET", path, headers={"Authorization": self.auth})
            response = connection.getresponse()
            body = response.read()
            if response.status == 404:
                raise FileNotFoundError(path)
            if response.status != 200:
                raise RuntimeError(f"Device Portal GET failed: HTTP {response.status}")
            return body
        finally:
            connection.close()

    def local_file(self, directory: str, filename: str) -> bytes:
        query = urlencode(
            {
                "knownfolderid": "LocalAppData",
                "packagefullname": self.package,
                "path": "\\LocalState" + directory.replace("/", "\\"),
                "filename": filename,
            }
        )
        return self.get("/api/filesystem/apps/file?" + query)

    def device(self) -> dict:
        return json.loads(self.local_file("", "device.json"))

    def status(self, job_id: str) -> dict | None:
        try:
            return json.loads(
                self.local_file(f"/inbox/results/{job_id}", "status.json")
            )
        except FileNotFoundError:
            return None

    def screenshot(self) -> bytes:
        return self.get("/ext/screenshot?download=false&hdr=false")


class Recorder:
    def __init__(self, portal: Portal, out: Path):
        self.portal, self.out = portal, out
        self.record_path = out / "shots.json"
        out.mkdir(parents=True, exist_ok=True)
        self.record = (
            json.loads(self.record_path.read_text())
            if self.record_path.exists()
            else {"shots": []}
        )

    def capture(self, label: str, status: dict | None = None) -> dict:
        device = self.portal.device()
        if device.get("package") != self.portal.package:
            raise SystemExit(
                f"device.json package {device.get('package')} "
                f"is not {self.portal.package}"
            )
        data = self.portal.screenshot()
        width, height = png_size(data)
        name = f"{label}.png"
        (self.out / name).write_bytes(data)
        shot = {
            "label": label,
            "file": name,
            "sha256": hashlib.sha256(data).hexdigest(),
            "width": width,
            "height": height,
            "captured_at": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "package": device.get("package"),
            "commit": device.get("commit"),
            "status": {k: status[k] for k in STATUS_KEYS if status and k in status},
        }
        self.record["shots"] = [s for s in self.record["shots"] if s["label"] != label]
        self.record["shots"].append(shot)
        self.record_path.write_text(json.dumps(self.record, indent=2) + "\n")
        print(f"{label}: {name} {width}x{height} {shot['sha256'][:12]}", flush=True)
        return shot


def watch(
    recorder: Recorder,
    job_id: str,
    interval: float,
    settle: float,
    timeout: float,
    running_step: int,
) -> int:
    """Capture each milestone of one job, then the dashboard after it ends."""
    seen = {s["label"] for s in recorder.record["shots"]}
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = recorder.portal.status(job_id)
        label = milestone(status, seen, running_step) if status else None
        if label:
            # The dashboard polls once per second; let it render this status.
            time.sleep(settle)
            recorder.capture(label, status)
            seen.add(label)
            if label in FINAL_STATES:
                return 0 if label == "completed" else 1
        time.sleep(interval)
    print(f"timeout waiting for {job_id}", file=sys.stderr)
    return 2


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--package", default="")
    parser.add_argument("--out", type=Path, required=True)
    sub = parser.add_subparsers(dest="command", required=True)
    capture = sub.add_parser("capture", help="one screenshot now")
    capture.add_argument("--label", required=True)
    capture.add_argument("--job", help="record this job's status with the shot")
    follow = sub.add_parser("watch", help="screenshots at each milestone of a job")
    follow.add_argument("--job", required=True)
    follow.add_argument("--interval", type=float, default=5)
    follow.add_argument("--settle", type=float, default=3)
    follow.add_argument("--timeout", type=float, default=3600)
    follow.add_argument(
        "--running-step", type=int, default=1024, help="trunk step of the running shot"
    )
    args = parser.parse_args()

    recorder = Recorder(Portal.from_env(args.package), args.out)
    if args.command == "capture":
        recorder.capture(
            args.label, recorder.portal.status(args.job) if args.job else None
        )
        return 0
    return watch(
        recorder, args.job, args.interval, args.settle, args.timeout, args.running_step
    )


if __name__ == "__main__":
    sys.exit(main())
