#!/usr/bin/env python3
"""Drive a fixed remote-to-remote transfer between two local etransfer daemons.

Two ``etd`` instances are launched (with independently selectable data channel
schemes). Once their control sockets accept connections the script invokes the
``etc`` client with two remote URLs so that the client performs a daemon ⇨
daemon copy. The source URL is hard-coded to ``/dev/zero,400MB`` and the
destination to ``/dev/null`` which yields a 400 MiB stream without touching the
local filesystem.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, Optional, Sequence, Tuple

SOURCE_REMOTE_PATH = "/dev/zero:400MB"
DEST_REMOTE_PATH = "/dev/null"


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


def _split_schemes(spec: str) -> list:
    """Parse a comma-separated scheme spec into a non-empty list of schemes.

    Used by both ``--source-data-scheme`` and ``--dest-data-scheme`` so a
    single daemon can be told to listen on several transports simultaneously
    (e.g. ``srt,udt``).
    """
    schemes = [s.strip() for s in spec.split(",")]
    schemes = [s for s in schemes if s]
    if not schemes:
        raise argparse.ArgumentTypeError(f"empty scheme spec: {spec!r}")
    return schemes


def _format_remote_url(port: int, path: str) -> str:
    # etc expects "proto://host[#port]:/path" for remote URLs; we always talk TCP
    clean_path = path if path.startswith("/") else f"/{path}"
    return f"tcp://127.0.0.1#{port}:{clean_path}"


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", default=None, help="path to etd binary (default: repo src/etd); used for both ends unless overridden")
    parser.add_argument("--source-daemon", default=None, help="override etd binary for the source daemon")
    parser.add_argument("--dest-daemon", default=None, help="override etd binary for the destination daemon")
    parser.add_argument("--client", default=None, help="path to etc binary (default: repo src/etc)")
    parser.add_argument("--data-scheme", default="tcp",
                        help="data channel scheme(s) for both daemons. Accepts a comma-separated "
                             "list; each scheme gets its own --data port on the daemon. "
                             "Default: %(default)s")
    parser.add_argument("--source-data-scheme", default=None,
                        help="override data scheme(s) for the source daemon (comma-separated)")
    parser.add_argument("--dest-data-scheme", default=None,
                        help="override data scheme(s) for the destination daemon (comma-separated)")
    parser.add_argument("--daemon-extra", action="append", default=[], help="additional flag(s) passed to both etd instances")
    parser.add_argument("--client-extra", action="append", default=[], help="additional flag(s) passed to etc")
    parser.add_argument("--timeout", type=float, default=None, help="abort the etc client run after this many seconds (no timeout by default)")
    parser.add_argument("--keep-daemons", action="store_true", help="leave daemons running after the transfer (for debugging)")
    args = parser.parse_args(argv)

    repo_root = Path(__file__).resolve().parents[1]
    default_etd = repo_root / "src" / "etd"
    source_etd_path = Path(args.source_daemon or args.daemon or default_etd)
    dest_etd_path = Path(args.dest_daemon or args.daemon or default_etd)
    etc_path = Path(args.client) if args.client else repo_root / "src" / "etc"

    for tool, path in ("source etd", source_etd_path), ("destination etd", dest_etd_path), ("etc", etc_path):
        if not path.exists():
            raise FileNotFoundError(f"{tool} binary not found at {path}")

    source_cmd_port = _find_free_port()
    dest_cmd_port = _find_free_port()

    source_schemes = _split_schemes(args.source_data_scheme or args.data_scheme)
    dest_schemes = _split_schemes(args.dest_data_scheme or args.data_scheme)

    # One --data port per requested scheme so daemons can listen on multiple
    # transports concurrently (matches the real-world setup that exposed the
    # SRT-vs-pre-v3-source forwarding bug).
    source_data_ports = [_find_free_port() for _ in source_schemes]
    dest_data_ports = [_find_free_port() for _ in dest_schemes]

    def _data_flags(schemes: Sequence[str], ports: Sequence[int]) -> list[str]:
        flags: list[str] = []
        for scheme, port in zip(schemes, ports):
            flags.extend(["--data", _format_data_addr(scheme, port)])
        return flags

    source_cmd = [
        str(source_etd_path),
        "--command",
        _format_data_addr("tcp", source_cmd_port),
        *_data_flags(source_schemes, source_data_ports),
        "-f",
        "-m",
        "4",
        "--buffer",
        str(32 * 1024 * 1024),
    ]
    dest_cmd = [
        str(dest_etd_path),
        "--command",
        _format_data_addr("tcp", dest_cmd_port),
        *_data_flags(dest_schemes, dest_data_ports),
        "-f",
        "-m",
        "4",
        "--buffer",
        str(32 * 1024 * 1024),
    ]
    source_cmd.extend(args.daemon_extra)
    dest_cmd.extend(args.daemon_extra)

    source_proc = _spawn(source_cmd)
    dest_proc = _spawn(dest_cmd)

    try:
        # Wait until both command ports accept connections.
        for label, port in (("source", source_cmd_port), ("destination", dest_cmd_port)):
            print(f"Waiting for {label} daemon on tcp://127.0.0.1:{port} …", flush=True)
            with _connect_with_retry(("127.0.0.1", port)):
                pass

        source_url = _format_remote_url(source_cmd_port, SOURCE_REMOTE_PATH)
        dest_url = _format_remote_url(dest_cmd_port, DEST_REMOTE_PATH)

        client_cmd = [str(etc_path), "-m", "4"]
        client_cmd.extend(args.client_extra)
        client_cmd.extend([source_url, dest_url])

        print("Invoking etc:", " ".join(client_cmd), flush=True)
        try:
            result = subprocess.run(
                client_cmd, check=False, capture_output=True, text=True, timeout=args.timeout,
            )
        except subprocess.TimeoutExpired as exc:
            print(f"etc did not finish within {args.timeout}s; aborting", file=sys.stderr)
            if exc.stdout:
                print("---- begin etc stdout (partial) ----")
                sys.stdout.write(exc.stdout if isinstance(exc.stdout, str) else exc.stdout.decode(errors="replace"))
                print("\n---- end etc stdout (partial) ----")
            if exc.stderr:
                print("---- begin etc stderr (partial) ----", file=sys.stderr)
                sys.stderr.write(exc.stderr if isinstance(exc.stderr, str) else exc.stderr.decode(errors="replace"))
                print("\n---- end etc stderr (partial) ----", file=sys.stderr)
            return 124

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
        if not args.keep_daemons:
            _stop_daemon("source", source_proc)
            _stop_daemon("destination", dest_proc)
        else:
            print("--keep-daemons specified; daemons left running", flush=True)


if __name__ == "__main__":
    raise SystemExit(main())
