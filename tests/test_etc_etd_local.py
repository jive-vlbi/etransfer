#!/usr/bin/env python3
"""Exercise a local etc → etd transfer using the fake /dev/zero and /dev/null endpoints.

The script launches a single ``etd`` daemon (with a configurable data-channel
scheme) and waits until its control socket accepts connections. Once ready, it
invokes the ``etc`` client to pull ``/dev/zero:400MB`` from the daemon and write
it to ``/dev/null`` on the local machine. This provides a quick integration
smoke-test without touching the filesystem.
"""

from __future__ import annotations

import argparse
import contextlib
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, Sequence, Tuple

SOURCE_REMOTE_PATH = "/dev/zero:400MB"
DEST_LOCAL_PATH = "/dev/null"


def _find_free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return int(sock.getsockname()[1])


def _connect_with_retry(addr: Tuple[str, int], timeout: float = 5.0) -> socket.socket:
    deadline = time.time() + timeout
    last_error: Optional[Exception] = None
    while time.time() < deadline:
        try:
            return socket.create_connection(addr, timeout=0.5)
        except OSError as exc:  # pragma: no cover - diagnostic path
            last_error = exc
            time.sleep(0.1)
    raise RuntimeError(f"Failed to connect to {addr}: {last_error}")


def _spawn(cmd: Sequence[str]) -> subprocess.Popen[str]:
    print("Starting:", " ".join(cmd), flush=True)
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def _drain_output(label: str, proc: subprocess.Popen[str]) -> None:
    if proc.stdout:
        leftover = proc.stdout.read()
        if leftover:
            print(f"---- begin {label} output ----")
            sys.stdout.write(leftover)
            if not leftover.endswith("\n"):
                sys.stdout.write("\n")
            print(f"---- end {label} output ----")


def _stop_daemon(label: str, proc: subprocess.Popen[str]) -> None:
    if proc.poll() is not None:
        _drain_output(label, proc)
        return
    print(f"Stopping {label} (pid={proc.pid}) …", flush=True)
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        print(f"{label} did not exit after SIGINT; killing", file=sys.stderr)
        proc.kill()
        proc.wait(timeout=5)
    finally:
        _drain_output(label, proc)


def _format_data_addr(proto: str, port: int) -> str:
    return f"{proto}://127.0.0.1:{port}"


def _format_remote_url(port: int, path: str) -> str:
    clean_path = path if path.startswith("/") else f"/{path}"
    return f"tcp://127.0.0.1#{port}:{clean_path}"


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", default=None, help="path to etd binary (default: repo src/etd)")
    parser.add_argument("--client", default=None, help="path to etc binary (default: repo src/etc)")
    parser.add_argument(
        "--data-scheme",
        action="append",
        default=[],
        help="data channel scheme(s); repeat the flag to advertise multiple (default: tcp)",
    )
    parser.add_argument("--daemon-extra", action="append", default=[], help="additional flag(s) passed to the etd instance")
    parser.add_argument("--client-extra", action="append", default=[], help="additional flag(s) passed to etc")
    parser.add_argument("--dest", default=DEST_LOCAL_PATH, help="local destination path (default: %(default)s)")
    parser.add_argument("--keep-daemon", action="store_true", help="leave daemon running after the transfer (for debugging)")
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parents[1]
    etd_path = Path(args.daemon) if args.daemon else repo_root / "src" / "etd"
    etc_path = Path(args.client) if args.client else repo_root / "src" / "etc"

    for tool, path in ("etd", etd_path), ("etc", etc_path):
        if not path.exists():
            raise FileNotFoundError(f"{tool} binary not found at {path}")

    cmd_port = _find_free_port()
    data_schemes = args.data_scheme or ["tcp"]
    data_endpoints = [_format_data_addr(scheme, _find_free_port()) for scheme in data_schemes]

    daemon_cmd = [
        str(etd_path),
        "--command",
        _format_data_addr("tcp", cmd_port),
    ]
    for endpoint in data_endpoints:
        daemon_cmd.extend(["--data", endpoint])
    daemon_cmd.extend([
        "-f",
        "-m",
        "4",
        "--buffer",
        str(32 * 1024 * 1024),
    ])
    daemon_cmd.extend(args.daemon_extra)

    daemon_proc = _spawn(daemon_cmd)

    try:
        print(f"Waiting for daemon on tcp://127.0.0.1:{cmd_port} …", flush=True)
        with _connect_with_retry(("127.0.0.1", cmd_port)):
            pass

        source_url = _format_remote_url(cmd_port, SOURCE_REMOTE_PATH)

        client_cmd = [str(etc_path), "-m", "4"]
        client_cmd.extend(args.client_extra)
        client_cmd.extend([source_url, args.dest])

        print("Invoking etc:", " ".join(client_cmd), flush=True)
        result = subprocess.run(client_cmd, check=False, capture_output=True, text=True)

        if result.stdout:
            print("---- begin etc stdout ----")
            print(result.stdout.rstrip("\n"))
            print("---- end etc stdout ----")
        if result.stderr:
            print("---- begin etc stderr ----", file=sys.stderr)
            sys.stderr.write(result.stderr)
            if not result.stderr.endswith("\n"):
                sys.stderr.write("\n")
            print("---- end etc stderr ----", file=sys.stderr)

        if result.returncode != 0:
            print(f"etc exited with status {result.returncode}", file=sys.stderr)
            return result.returncode

        print("Transfer command completed successfully.")
        return 0
    finally:
        if not args.keep_daemon:
            _stop_daemon("daemon", daemon_proc)
        else:
            print("--keep-daemon specified; daemon left running", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
