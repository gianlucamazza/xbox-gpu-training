#!/usr/bin/env python3
"""Portable Device Portal teardown and idle-app checks for the console worker.

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
import re
import shlex
import ssl
import sys
import time
from dataclasses import dataclass, replace
from datetime import datetime, timezone
from http.cookies import SimpleCookie
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs, urlencode, urlsplit

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
    "gpu_engine_exclude": (),
    "idle_cpu_percent": 1.0,
    "teardown_wait_seconds": 60.0,
    "portal_port": 11443,
    "tls_verify": "pin",
    "process_images": ("xgpue0.exe",),
    "terminate_by": ("package", "pid"),
    "shutdown_name": "shutdown",
    "heartbeat_name": "host.heartbeat",
    "instance_name": "instance.json",
    "orphan_strict": False,
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
    "gpu_engine_exclude": "XGPU_GPU_ENGINE_EXCLUDE",
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
    "instance_name": "XGPU_INSTANCE_NAME",
    "orphan_strict": "XGPU_ORPHAN_STRICT",
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
INSTANCE_NAME = DEFAULTS["instance_name"]
FILE_TYPE = 32
GPU_ADVISORY_NOTE = (
    "GPU utilisation is advisory and not decisive. On Xbox the foreground "
    "engine often reads ~100% for any idle foreground app."
)
START_TIME_UNKNOWN_WARNING = (
    "instance start time is unknown (no LocalState/instance.json and no "
    "process CreateTime); orphan is not decided. Set XGPU_ORPHAN_STRICT=1 "
    "to refuse instead."
)


class PortalAuthError(RuntimeError):
    """Device Portal rejected a mutating call (HTTP 400/403)."""


def split_csv(value: str) -> tuple[str, ...]:
    return tuple(part.strip() for part in value.split(",") if part.strip())


def parse_int_setting(value: str | int | float) -> int:
    """Accept integers and float-looking strings ('20.0' → 20)."""
    return int(float(value))


def parse_int_tuple(value: str) -> tuple[int, ...]:
    return tuple(parse_int_setting(part) for part in split_csv(value))


def parse_bool_setting(value: str | bool) -> bool:
    if isinstance(value, bool):
        return value
    return str(value).strip().lower() in ("1", "true", "yes", "on")


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
    gpu_engine_exclude: tuple[int, ...] = DEFAULTS["gpu_engine_exclude"]
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
    instance_name: str = DEFAULTS["instance_name"]
    orphan_strict: bool = DEFAULTS["orphan_strict"]

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
            "instance_name",
        )
        for key in ints:
            raw = env_value(mapping, key)
            if raw is not None:
                updates[key] = parse_int_setting(raw)
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
        raw = env_value(mapping, "gpu_engine_exclude")
        if raw is not None:
            updates["gpu_engine_exclude"] = parse_int_tuple(raw)
        raw = env_value(mapping, "process_images")
        if raw is not None:
            updates["process_images"] = tuple(name.lower() for name in split_csv(raw))
        raw = env_value(mapping, "terminate_by")
        if raw is not None:
            updates["terminate_by"] = split_csv(raw)
        raw = env_value(mapping, "orphan_strict")
        if raw is not None:
            updates["orphan_strict"] = parse_bool_setting(raw)
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
        instance_name = getattr(args, "instance_name", None)
        if instance_name not in (None, ""):
            updates["instance_name"] = instance_name
        orphan_strict = getattr(args, "orphan_strict", None)
        if orphan_strict is not None:
            updates["orphan_strict"] = bool(orphan_strict)
        if getattr(args, "gpu_engine", None):
            updates["gpu_engine_filter"] = tuple(args.gpu_engine)
        if getattr(args, "gpu_engine_exclude", None):
            updates["gpu_engine_exclude"] = tuple(args.gpu_engine_exclude)
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


def encode_package_name(package: str) -> str:
    return base64.b64encode(package.encode("utf-8")).decode("ascii")


def terminate_process_path(pid: int) -> str:
    return "/api/taskmanager/process?" + urlencode({"pid": str(pid)})


def terminate_app_path(package: str) -> str:
    return "/api/taskmanager/app?" + urlencode({"package": encode_package_name(package)})


def decode_terminate_package(path: str) -> str:
    query = parse_qs(urlsplit(path).query)
    raw = (query.get("package") or [""])[0]
    return base64.b64decode(raw.encode("ascii")).decode("utf-8")


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
    engine_exclude: tuple[int, ...] = (),
) -> list[dict]:
    hits = []
    allowed = set(engine_filter) if engine_filter else None
    excluded = set(engine_exclude)
    for adapter, engines in enumerate(adapters):
        for engine, percent in enumerate(engines):
            if engine in excluded:
                continue
            if allowed is not None and engine not in allowed:
                continue
            if percent >= threshold:
                hits.append(
                    {"adapter": adapter, "engine": engine, "percent": percent}
                )
    return hits


def gpu_advisory(
    adapters: list[list[float]],
    *,
    threshold: float = DEFAULTS["gpu_threshold_percent"],
    engine_filter: tuple[int, ...] = (),
    engine_exclude: tuple[int, ...] = (),
) -> dict:
    pinned = pinned_engines(
        adapters, threshold, engine_filter=engine_filter, engine_exclude=engine_exclude
    )
    return {
        "decisive": False,
        "note": GPU_ADVISORY_NOTE,
        "engine_pinned": bool(pinned),
        "pinned": pinned,
        "excluded": list(engine_exclude),
        "adapters": adapters,
    }


def listing_items(listing: dict | None) -> list[dict]:
    if not listing:
        return []
    items = listing.get("Items")
    if items is None:
        items = listing.get("items")
    return list(items or [])


def item_name(item: dict) -> str:
    return str(item.get("Name") or item.get("name") or "")


def item_is_file(item: dict) -> bool:
    typ = item.get("Type", item.get("type"))
    return typ in (FILE_TYPE, "File", "file") or (
        typ not in (16, "Folder", "folder", "Directory", "directory")
        and item.get("FileSize", item.get("fileSize")) is not None
    )


def inbox_names(listing: dict | None) -> tuple[str, ...]:
    return tuple(item_name(item) for item in listing_items(listing) if item_name(item))


def parse_portal_time(value, now: float) -> float | None:
    """Absolute unix seconds from a Device Portal date field, or None."""
    if value is None or value == "":
        return None
    if isinstance(value, str):
        match = re.search(r"/Date\((\d+)\)/", value)
        if match:
            return int(match.group(1)) / 1000.0
        try:
            return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()
        except ValueError:
            return None
    if isinstance(value, (int, float)):
        number = float(value)
        if number > 1e16:
            return number / 1e7 - 11644473600.0
        if number > 1e11:
            return number / 1000.0
        if 0 < number < now + 86400:
            return number
    return None


def item_age_seconds(item: dict, now: float) -> float | None:
    for key in (
        "DateModified",
        "dateModified",
        "Modified",
        "LastModified",
        "DateCreated",
        "dateCreated",
    ):
        instant = parse_portal_time(item.get(key), now)
        if instant is not None:
            return max(0.0, now - instant)
    return None


def heartbeat_age_seconds(
    local_listing: dict | None,
    *,
    name: str = HEARTBEAT_NAME,
    now: float,
    written_at: float | None = None,
) -> float | None:
    if written_at is not None:
        return max(0.0, now - written_at)
    for item in listing_items(local_listing):
        if item_name(item) == name:
            return item_age_seconds(item, now)
    return None


def instance_id_of(instance: dict | None) -> str:
    if not instance:
        return ""
    return str(instance.get("instance_id") or "")


def instance_start_seconds(instance: dict | None, now: float) -> float | None:
    """Unix seconds from LocalState/instance.json started_at, or None."""
    if not instance:
        return None
    return parse_portal_time(instance.get("started_at"), now)


def heartbeat_for_instance(
    heartbeat: dict | None,
    *,
    instance_id: str = "",
    started_at: float | None = None,
    now: float,
    listing_written_at: float | None = None,
) -> float | None:
    """Unix time of a heartbeat that belongs to this instance, or None.

    A leftover host.heartbeat from an earlier process is ignored when its
    instance_id differs from LocalState/instance.json or when it was written
    before this instance started. A file with no matching instance_id does
    not bind to the current process.
    """
    doc = heartbeat if isinstance(heartbeat, dict) else {}
    hb_id = str(doc.get("instance_id") or "")
    if instance_id and hb_id != instance_id:
        return None
    written = parse_portal_time(doc.get("written_at"), now)
    if written is None:
        written = listing_written_at
    if written is not None and started_at is not None and written < started_at:
        return None
    return written


def last_job_ended_at(
    result_listings: list[dict],
    *,
    now: float,
    statuses: list[dict] | None = None,
    not_before: float | None = None,
) -> float | None:
    """Unix time of the most recent finished job, optionally only this instance."""
    instants = []
    for listing in result_listings:
        for item in listing_items(listing):
            if item_name(item) == "status.json":
                age = item_age_seconds(item, now)
                if age is not None:
                    instants.append(now - age)
    for status in statuses or []:
        if status.get("state") in ("running",):
            continue
        for key in ("finished_at", "completed_at", "ended_at"):
            instant = parse_portal_time(status.get(key), now)
            if instant is not None:
                instants.append(instant)
    if not_before is not None:
        instants = [instant for instant in instants if instant >= not_before]
    return max(instants) if instants else None


def last_job_ended_age_seconds(
    result_listings: list[dict],
    *,
    now: float,
    statuses: list[dict] | None = None,
    not_before: float | None = None,
) -> float | None:
    instant = last_job_ended_at(
        result_listings, now=now, statuses=statuses, not_before=not_before
    )
    return None if instant is None else max(0.0, now - instant)


def inbox_entries(listing: dict | None, now: float) -> list[dict]:
    entries = []
    for item in listing_items(listing):
        name = item_name(item)
        if not name:
            continue
        instant = None
        for key in ("DateModified", "dateModified", "DateCreated", "dateCreated"):
            instant = parse_portal_time(item.get(key), now)
            if instant is not None:
                break
        entries.append({"name": name, "modified": instant})
    return entries


def result_job_ids(result_listings: list[dict]) -> set[str]:
    ids = set()
    for listing in result_listings:
        for item in listing_items(listing):
            name = item_name(item)
            if name and not item_is_file(item):
                ids.add(name)
    return ids


def process_start_seconds(procs: list[dict], now: float) -> float | None:
    starts = []
    for proc in procs:
        for key in ("CreateTime", "CreationTime", "StartTime", "createTime"):
            instant = parse_portal_time(proc.get(key), now)
            if instant is not None:
                starts.append(instant)
                break
    return min(starts) if starts else None


def claimed_job_id(name: str) -> str:
    lower = name.lower()
    if lower.endswith(".claimed"):
        return name[: -len(".claimed")]
    return ""


def live_claimed_without_result(
    inbox_items: list[dict] | tuple[str, ...],
    result_ids: set[str],
    app_start: float | None,
) -> tuple[str, ...]:
    """Historical .claimed files with a result are ignored.

    A .claimed with no result is a weak in-flight signal only when it is
    newer than this app instance.
    """
    live = []
    for item in inbox_items:
        if isinstance(item, str):
            name, modified = item, None
        else:
            name, modified = str(item.get("name") or ""), item.get("modified")
        job_id = claimed_job_id(name)
        if not job_id or job_id in result_ids:
            continue
        if app_start is None or modified is None or modified < app_start:
            continue
        live.append(name)
    return tuple(live)


def classify_app_state(
    *,
    process_present: bool,
    worker: dict | None = None,
    device: dict | None = None,
    inbox: tuple[str, ...] | list[dict] = (),
    result_job_ids: set[str] | tuple[str, ...] = (),
    now: float | None = None,
    app_start_seconds: float | None = None,
    instance_id: str = "",
    heartbeat_age_seconds: float | None = None,
    heartbeat_written_at: float | None = None,
    last_job_ended_age_seconds: float | None = None,
    last_job_ended_at: float | None = None,
    idle_exit_seconds: float = DEFAULTS["idle_exit_seconds"],
    heartbeat_stale_seconds: float = DEFAULTS["heartbeat_stale_seconds"],
) -> dict:
    """Orphan predicate from this app instance. GPU and job history are not used.

    Start time comes from the caller (instance.json, else process CreateTime).
    worker.json is not a start-time source: floppylm.worker.v1 has no
    started_at and additionalProperties is false.
    """
    worker = worker or {}
    clock = time.time() if now is None else now
    inbox_items = [
        item
        if isinstance(item, dict)
        else {"name": item, "modified": None}
        for item in inbox
    ]
    names = tuple(str(item.get("name") or "") for item in inbox_items)
    result_ids = set(result_job_ids)
    queued = bool(process_present) and any(name.lower().endswith(".ready") for name in names)
    weak_claimed = (
        live_claimed_without_result(inbox_items, result_ids, app_start_seconds)
        if process_present
        else ()
    )
    worker_state = str(worker.get("state") or "") if process_present else None
    if worker_state == "":
        worker_state = None
    active = worker.get("active_job") if process_present else None
    running = bool(process_present) and (
        worker_state == "running" or isinstance(active, dict)
    )
    job_active = running or queued or bool(weak_claimed)
    ready = bool(process_present) and worker_state == "ready" and not job_active
    if last_job_ended_at is None and last_job_ended_age_seconds is not None:
        last_job_ended_at = clock - last_job_ended_age_seconds
    if (
        last_job_ended_at is not None
        and app_start_seconds is not None
        and last_job_ended_at < app_start_seconds
    ):
        last_job_ended_at = None
    if heartbeat_written_at is None and heartbeat_age_seconds is not None:
        heartbeat_written_at = clock - heartbeat_age_seconds
    if (
        heartbeat_written_at is not None
        and app_start_seconds is not None
        and heartbeat_written_at < app_start_seconds
    ):
        heartbeat_written_at = None
    anchors = [
        value
        for value in (app_start_seconds, last_job_ended_at, heartbeat_written_at)
        if value is not None
    ]
    idle_since = max(anchors) if anchors else None
    idle_age = None if idle_since is None else max(0.0, clock - idle_since)
    app_age = (
        None if app_start_seconds is None else max(0.0, clock - app_start_seconds)
    )
    heartbeat_age = (
        None
        if heartbeat_written_at is None
        else max(0.0, clock - heartbeat_written_at)
    )
    heartbeat_stale = heartbeat_age is None or heartbeat_age >= heartbeat_stale_seconds
    young = app_age is not None and app_age < idle_exit_seconds
    leftover = idle_age is not None and idle_age >= idle_exit_seconds
    start_known = app_start_seconds is not None
    if not process_present:
        orphan = False
    elif job_active:
        orphan = False
    elif not start_known:
        orphan = None
    else:
        orphan = leftover and (not young)
    return {
        "process_present": bool(process_present),
        "job_active": job_active,
        "queued": queued,
        "claimed": bool(weak_claimed),
        "claimed_live": weak_claimed,
        "running": running,
        "ready": ready,
        "worker_state": worker_state,
        "instance_id": instance_id or None,
        "start_time_known": start_known,
        "app_age_seconds": app_age,
        "idle_age_seconds": idle_age,
        "heartbeat_age_seconds": heartbeat_age,
        "heartbeat_stale": heartbeat_stale,
        "last_job_ended_age_seconds": (
            None if last_job_ended_at is None else max(0.0, clock - last_job_ended_at)
        ),
        "orphan": orphan,
    }


def lifecycle_report(
    observed: dict, *, when: str, orphan_strict: bool = False
) -> dict:
    warning = None
    if when == "before":
        if observed.get("job_active"):
            ok, reason = False, "job_active"
        elif observed.get("orphan"):
            ok, reason = False, "orphan_stale_idle"
        elif observed.get("orphan") is None:
            reason = "start_time_unknown"
            if orphan_strict:
                ok = False
            else:
                ok = True
                warning = START_TIME_UNKNOWN_WARNING
        else:
            ok, reason = True, "ok"
    elif when == "after":
        if observed.get("process_present"):
            ok, reason = False, "process_still_running"
        else:
            ok, reason = True, "ok"
    else:
        raise ValueError("when must be before or after")
    report = {"when": when, "ok": ok, "reason": reason, **observed}
    if warning:
        report["warning"] = warning
    return report


def localstate_dir(directory: str) -> str:
    cleaned = directory.replace("/", "\\").strip("\\")
    return "\\LocalState" + (("\\" + cleaned) if cleaned else "")


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

    def filesystem_path(
        self, endpoint: str, directory: str, filename: str | None = None
    ) -> str:
        if not self.settings.package:
            raise RuntimeError("package full name is required for LocalState access")
        query = {
            "knownfolderid": "LocalAppData",
            "packagefullname": self.settings.package,
            "path": localstate_dir(directory),
        }
        if filename is not None:
            query["filename"] = filename
        return f"/api/filesystem/apps/{endpoint}?" + urlencode(query)

    def processes(self) -> dict:
        return self.request("GET", "/api/resourcemanager/processes", json_result=True)

    def systemperf(self) -> dict:
        return self.request("GET", "/api/resourcemanager/systemperf", json_result=True)

    def list_files(self, directory: str = "inbox") -> dict:
        return self.request(
            "GET", self.filesystem_path("files", directory), json_result=True
        )

    def download(self, filename: str, directory: str = "") -> bytes:
        return self.request("GET", self.filesystem_path("file", directory, filename))

    def read_json(self, filename: str, directory: str = "") -> dict:
        try:
            payload = json.loads(self.download(filename, directory))
        except FileNotFoundError:
            return {}
        return payload if isinstance(payload, dict) else {}

    def delete_file(self, filename: str, directory: str = "inbox") -> None:
        if not filename or any(c in filename for c in '\\/"\r\n'):
            raise ValueError("unsafe remote filename")
        self.request("DELETE", self.filesystem_path("file", directory, filename))

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

    def _optional_listing(self, directory: str) -> dict:
        try:
            return self.list_files(directory)
        except FileNotFoundError:
            return {}

    def observe(self, listing: dict | None = None, *, sleep=time.sleep, now=None) -> dict:
        listing = listing if listing is not None else self.processes()
        procs = self.matching_processes(listing)
        now_s = time.time() if now is None else now
        instance = self.read_json(self.settings.instance_name, "")
        worker = self.read_json("worker.json", "")
        device = self.read_json("device.json", "")
        heartbeat_doc = self.read_json(HEARTBEAT_NAME, "")
        inbox = self._optional_listing("inbox")
        local = self._optional_listing("")
        results = self._optional_listing("inbox/results")
        result_listings = [results]
        for item in listing_items(results):
            name = item_name(item)
            if name and not item_is_file(item):
                result_listings.append(self._optional_listing("inbox/results/" + name))
        instance_id = instance_id_of(instance)
        app_start = instance_start_seconds(instance, now_s)
        if app_start is None:
            app_start = process_start_seconds(procs, now_s)
        listing_age = heartbeat_age_seconds(local, now=now_s)
        listing_written = None if listing_age is None else now_s - listing_age
        heartbeat_written = heartbeat_for_instance(
            heartbeat_doc,
            instance_id=instance_id,
            started_at=app_start,
            now=now_s,
            listing_written_at=listing_written,
        )
        state = classify_app_state(
            process_present=bool(procs),
            worker=worker,
            device=device,
            inbox=inbox_entries(inbox, now_s),
            result_job_ids=result_job_ids(result_listings),
            now=now_s,
            app_start_seconds=app_start,
            instance_id=instance_id,
            heartbeat_written_at=heartbeat_written,
            last_job_ended_at=last_job_ended_at(
                result_listings, now=now_s, not_before=app_start
            ),
            idle_exit_seconds=self.settings.idle_exit_seconds,
            heartbeat_stale_seconds=self.settings.heartbeat_stale_seconds,
        )
        state["cpu_idle"] = bool(procs) and all(
            cpu_usage(proc) <= self.settings.idle_cpu_percent for proc in procs
        )
        state["gpu_advisory"] = gpu_advisory(
            self.sample_adapters(sleep=sleep),
            threshold=self.settings.gpu_threshold_percent,
            engine_filter=self.settings.gpu_engine_filter,
            engine_exclude=self.settings.gpu_engine_exclude,
        )
        return state

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
        self.request(
            "POST",
            self.filesystem_path("file", directory, filename),
            payload,
            {"Content-Type": f"multipart/form-data; boundary={boundary}"},
        )

    def write_heartbeat(self) -> None:
        instance = self.read_json(self.settings.instance_name, "")
        instance_id = instance_id_of(instance)
        payload = {
            "schema": "floppylm.host.heartbeat.v1",
            "written_at": datetime.now(timezone.utc).timestamp(),
        }
        if instance_id:
            payload["instance_id"] = instance_id
        self.upload(HEARTBEAT_NAME, json.dumps(payload).encode(), directory="")

    def write_shutdown(self, reason: str = "campaign-complete") -> None:
        self.upload(SHUTDOWN_NAME, reason.encode(), directory="inbox")

    def clear_stale_markers(self) -> dict:
        """Delete leftover inbox/shutdown so the next launch does not exit.

        Device Portal accepts DELETE of a missing file. List first and report
        cleared only when the marker was present.
        """
        cleared = []
        listing = self._optional_listing("inbox")
        names = {item_name(item) for item in listing_items(listing)}
        if SHUTDOWN_NAME in names:
            self.delete_file(SHUTDOWN_NAME, "inbox")
            cleared.append("inbox/shutdown")
        return {"cleared": cleared}

    def terminate(self) -> dict:
        listing = self.processes()
        procs = self.matching_processes(listing)
        result = {"packages": [], "pids": [], "package_errors": []}
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
                try:
                    self.request("DELETE", terminate_app_path(package))
                    result["packages"].append(package)
                except (PortalAuthError, RuntimeError, FileNotFoundError) as error:
                    result["package_errors"].append(
                        {"package": package, "error": str(error)}
                    )
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
        killed = False
        if present:
            try:
                self.write_shutdown("campaign-complete")
            except FileNotFoundError:
                pass
            if not self.wait_gone(timeout, sleep=sleep):
                self.terminate()
                killed = True
                if not self.wait_gone(timeout, sleep=sleep):
                    raise TimeoutError("console process still listed after terminate")
        try:
            self.clear_stale_markers()
        except (FileNotFoundError, RuntimeError):
            pass
        after = lifecycle_report(
            self.observe(sleep=sleep),
            when="after",
            orphan_strict=self.settings.orphan_strict,
        )
        after["terminated"] = killed
        return after

    def check_idle(self, when: str, *, sleep=time.sleep, now=None) -> dict:
        return lifecycle_report(
            self.observe(sleep=sleep, now=now),
            when=when,
            orphan_strict=self.settings.orphan_strict,
        )


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
    parser.add_argument("--portal-port", type=parse_int_setting, default=None)
    parser.add_argument("--tls-verify", choices=("pin", "none", "system"), default=None)
    parser.add_argument("--tls-pin", default="")
    parser.add_argument("--package", default="")
    parser.add_argument("--package-family", default="")
    parser.add_argument("--process-image", action="append", default=None)
    parser.add_argument("--idle-exit-seconds", type=parse_int_setting, default=None)
    parser.add_argument("--heartbeat-interval-seconds", type=parse_int_setting, default=None)
    parser.add_argument("--heartbeat-stale-seconds", type=parse_int_setting, default=None)
    parser.add_argument("--poll-interval-seconds", type=float, default=None)
    parser.add_argument("--gpu-threshold-percent", type=float, default=None)
    parser.add_argument("--gpu-sample-count", type=parse_int_setting, default=None)
    parser.add_argument("--gpu-sample-interval-seconds", type=float, default=None)
    parser.add_argument("--gpu-engine", action="append", type=parse_int_setting, default=None)
    parser.add_argument(
        "--gpu-engine-exclude", action="append", type=parse_int_setting, default=None
    )
    parser.add_argument("--idle-cpu-percent", type=float, default=None)
    parser.add_argument("--teardown-wait-seconds", type=float, default=None)
    parser.add_argument(
        "--terminate-by",
        action="append",
        choices=("package", "pid"),
        default=None,
    )
    parser.add_argument("--instance-name", default="")
    parser.add_argument(
        "--orphan-strict",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="refuse when instance start time is unknown (default: warn and pass)",
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    add_settings_flags(parser)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("prepare", help="clear stale inbox/shutdown before launch")
    sub.add_parser("heartbeat", help="refresh LocalState/host.heartbeat")
    shutdown = sub.add_parser("shutdown", help="upload inbox/shutdown")
    shutdown.add_argument("--reason", default="campaign-complete")
    sub.add_parser("terminate", help="DELETE by package and/or pid")
    sub.add_parser("teardown", help="shutdown, wait, terminate if needed, idle check after")
    idle = sub.add_parser("idle-gpu", help="observe job/heartbeat state before or after")
    idle.add_argument("--when", choices=("before", "after"), required=True)
    args = parser.parse_args(argv)

    settings = load_settings(args)
    portal = LifecyclePortal(https_request_fn(settings), settings)
    if args.command == "prepare":
        print(json.dumps(portal.clear_stale_markers(), sort_keys=True), flush=True)
        return 0
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
    report = portal.check_idle(args.when)
    print(json.dumps(report, sort_keys=True), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
