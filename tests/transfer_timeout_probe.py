#!/usr/bin/env python3
"""Self-contained probe for the transfer inactivity timeout watchdog.

The script starts an `etd` daemon with a known TCP configuration, speaks the
wire protocol directly (so the local "just use cp" shortcut never triggers),
and simulates a stalled data sender.  Once the watchdog fires it verifies that
the transfer entry has been removed by issuing a second write request.
"""

from __future__ import annotations

import argparse
import contextlib
import os
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Iterable, Optional, Tuple


def _read_reply_until_ok(cmd_sock: socket.socket) -> Tuple[str, int]:
    """Read daemon reply lines until "OK" is seen; return (uuid, already_have)."""
    uuid = None
    already_have = None
    with cmd_sock.makefile("r", encoding="utf-8", newline="\n") as reader:
        for line in reader:
            line = line.rstrip("\r\n")
            if not line:
                continue
            if line.startswith("UUID:"):
                uuid = line.split(":", 1)[1]
            elif line.startswith("AlreadyHave:"):
                already_have = int(line.split(":", 1)[1])
            elif line.startswith("OK"):
                break
            elif line.startswith("ERR"):
                raise RuntimeError(f"Daemon reported error: {line}")
    if uuid is None:
        raise RuntimeError("Daemon did not provide a UUID in its reply")
    if already_have is None:
        already_have = 0
    return uuid, already_have


def _find_free_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind(("127.0.0.1", 0))
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        return sock.getsockname()[1]


def _spawn_daemon(cmd: Iterable[str]) -> subprocess.Popen:
    return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def _connect_with_retry(addr: Tuple[str, int], timeout: float = 5.0) -> socket.socket:
    deadline = time.time() + timeout
    last_error: Optional[Exception] = None
    while time.time() < deadline:
        try:
            return socket.create_connection(addr, timeout=0.5)
        except OSError as exc:
            last_error = exc
            time.sleep(0.1)
    raise RuntimeError(f"Failed to connect to {addr}: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", default=None, help="path to etd binary (default: repo src/etd)")
    parser.add_argument("--timeout", type=float, default=2.0, help="inactive timeout to configure on the daemon")
    parser.add_argument("--payload-bytes", type=int, default=4096, help="announced payload size (default: %(default)s)")
    parser.add_argument("--wait", type=float, default=5.0, help="seconds to wait for timeout to trigger")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    daemon_path = Path(args.daemon) if args.daemon else repo_root / "src" / "etd"
    if not daemon_path.exists():
        raise FileNotFoundError(f"etd binary not found at {daemon_path}")

    command_port = _find_free_port()
    data_port = _find_free_port()
    daemon_cmd = [
        str(daemon_path),
        "--command", f"tcp://127.0.0.1:{command_port}",
        "--data", f"tcp://127.0.0.1:{data_port}",
        "--inactive-timeout", str(args.timeout),
        "-f",
        "-m", "4",
    ]

    print("Starting daemon:", " ".join(daemon_cmd), flush=True)
    proc = _spawn_daemon(daemon_cmd)

    tmp_path = Path(tempfile.mkstemp(prefix="etd-timeout-")[1])
    try:
        addr = ("127.0.0.1", command_port)
        print(f"Creating first client on {addr} …", flush=True)
        with _connect_with_retry(addr) as first_cmd:
            request = f"write-file-OverWrite {tmp_path}\n".encode("utf-8")
            first_cmd.sendall(request)
            uuid, already_have = _read_reply_until_ok(first_cmd)
            print(f"Client#1 accepted transfer: uuid={uuid} (already_have={already_have})")

            print("Attempting parallel client while transfer is active …", flush=True)
            with _connect_with_retry(addr) as second_cmd:
                second_cmd.sendall(request)
                try:
                    _read_reply_until_ok(second_cmd)
                except RuntimeError as exc:
                    print(f"Client#2 correctly refused during active transfer: {exc}")
                else:
                    print("Client#2 unexpectedly succeeded while transfer active", file=sys.stderr)
                    return 1

            data_addr = ("127.0.0.1", data_port)
            print(f"Connecting data socket {data_addr} …", flush=True)
            with _connect_with_retry(data_addr) as data_sock:
                header = f"{{ uuid:{uuid}, sz:{args.payload_bytes} }}".encode("utf-8")
                data_sock.sendall(header)
                print("Announced payload, now simulating stalled sender …", flush=True)

                time.sleep(args.wait)

                data_sock.settimeout(0.5)
                try:
                    probe = data_sock.recv(1)
                except socket.timeout:
                    probe = b""
                if probe:
                    print("Data socket still open (timeout may not have fired yet)")
                else:
                    print("Data socket closed – monitor likely triggered")

            print("Client#1 requesting same path post-timeout …", flush=True)
            first_cmd.sendall(request)
            uuid2, _ = _read_reply_until_ok(first_cmd)
            print(f"Client#1 got uuid={uuid2} on retry")

        print("Client#3 (fresh connection) requesting same path …", flush=True)
        with _connect_with_retry(addr) as third_cmd:
            third_cmd.sendall(request)
            uuid3, _ = _read_reply_until_ok(third_cmd)
            if uuid3 == uuid:
                print("Client#3 received original uuid – cleanup failed", file=sys.stderr)
                return 1
            print(f"Client#3 accepted with uuid={uuid3}. Timeout watchdog and lock release verified.")
        return 0
    finally:
        print("Stopping daemon …", flush=True)
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            print("Daemon did not exit after SIGINT; killing", file=sys.stderr)
            proc.kill()
            proc.wait(timeout=5)
        # Drain any remaining output for debugging context.
        if proc.stdout:
            leftover = proc.stdout.read()
            if leftover:
                sys.stdout.write(leftover)
        # Ensure socket path is cleared if local file path used
        with contextlib.suppress(FileNotFoundError):
            tmp_path.unlink()


if __name__ == "__main__":
    sys.exit(main())
