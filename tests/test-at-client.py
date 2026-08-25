#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
import os
import pathlib
import pty
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
CLIENT = pathlib.Path(
    os.environ.get("SPRD_AT_CLIENT", ROOT / "build" / "sprd-at-tty")
)


def run_modem(chunks, timeout="1000", close_early=False):
    master, slave = pty.openpty()
    device = os.ttyname(slave)

    def modem():
        request = b""
        while not request.endswith(b"\r\n"):
            data = os.read(master, 1024)
            if not data:
                return
            request += data
        if close_early:
            os.close(master)
            return
        for chunk in chunks:
            os.write(master, chunk)
            time.sleep(0.02)

    worker = threading.Thread(target=modem, daemon=True)
    worker.start()
    result = subprocess.run(
        [str(CLIENT), "-d", device, "-t", timeout, "AT"],
        text=True,
        capture_output=True,
        timeout=5,
        check=False,
    )
    worker.join(timeout=1)
    os.close(slave)
    if not close_early:
        os.close(master)
    return result


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    result = run_modem([b"\r\n", b"OK", b"\r\n"])
    require(result.returncode == 0, result.stderr)
    require("OK" in result.stdout, result.stdout)

    result = run_modem([b"\r\n+CEREG: 1\r\n", b"\r\nOK\r\n"])
    require(result.returncode == 0, result.stderr)
    require("+CEREG: 1" in result.stdout, result.stdout)

    result = run_modem([b"\r\nERROR\r\n"])
    require(result.returncode == 1, result.stderr)
    require("modem rejected command" in result.stderr, result.stderr)

    result = run_modem([], timeout="100")
    require(result.returncode == 1, result.stderr)
    require("timeout waiting" in result.stderr, result.stderr)

    result = run_modem([], close_early=True)
    require(result.returncode == 1, result.stderr)
    require("disconnect" in result.stderr, result.stderr)

    result = run_modem([b"X" * 65536])
    require(result.returncode == 1, result.stderr)
    require("response exceeds" in result.stderr, result.stderr)

    with tempfile.TemporaryDirectory() as temp:
        temp_path = pathlib.Path(temp)
        first = temp_path / "at-one"
        second = temp_path / "at-two"
        first.touch()
        second.touch()
        special = temp_path / "sprd-at-tty"
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=c11",
                "-O2",
                f'-DDEVICE_GLOB="{temp_path}/at-*"',
                f'-DLEGACY_DEVICE="{temp_path}/legacy"',
                str(ROOT / "tools" / "sprd-at-tty.c"),
                "-o",
                str(special),
            ],
            check=True,
        )
        result = subprocess.run(
            [str(special), "AT"],
            text=True,
            capture_output=True,
            check=False,
        )
        require(result.returncode == 1, result.stderr)
        require("multiple SPRD AT ports" in result.stderr, result.stderr)

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except AssertionError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
