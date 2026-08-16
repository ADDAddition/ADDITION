#!/usr/bin/env python3
"""Live additiond: --mainnet is a separate chain; testnet labels stay on --network testnet."""

from __future__ import annotations

import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))
PRIVACY_KEY = "addition-mainnet-local-test-key-32bxx"


def field(text: str, name: str) -> str:
    match = re.search(r"(?:^|[\s:])%s=(\S+)" % re.escape(name), text)
    return match.group(1) if match else ""


def tcp_rpc(host: str, port: int, command: str, timeout: float = 8.0) -> str:
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        chunks: list[bytes] = []
        while True:
            data = sock.recv(8192)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def wait_port(host: str, port: int, timeout: float = 45.0) -> str:
    deadline = time.time() + timeout
    last = ""
    while time.time() < deadline:
        try:
            last = tcp_rpc(host, port, "getinfo")
            if last:
                return last
        except OSError:
            time.sleep(0.2)
    raise TimeoutError("timeout waiting for %s:%s last=%s" % (host, port, last))


def start_node(args: list[str], log_path: Path, extra_env: dict[str, str] | None = None) -> subprocess.Popen:
    env = os.environ.copy()
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_PUBLIC_RPC_PORT", None)
    env.pop("ADDITION_PUBLIC_RPC_BIND", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_MAINNET_MODE", None)
    env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
    if extra_env:
        env.update(extra_env)
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        args,
        cwd=str(ROOT),
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=env,
    )
    proc._log_handle = log  # type: ignore[attr-defined]
    return proc


def stop_node(proc: subprocess.Popen | None) -> None:
    if proc is None:
        return
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=4)
    handle = getattr(proc, "_log_handle", None)
    if handle is not None:
        handle.close()


def fail(msg: str) -> int:
    print("FAIL:", msg)
    return 1


def main() -> int:
    if not BIN.exists():
        return fail("missing additiond at %s" % BIN)

    tmp = Path(tempfile.mkdtemp(prefix="addition-mainnet-live-"))
    mainnet_dir = tmp / "mainnet"
    testnet_dir = tmp / "testnet"
    mix_dir = tmp / "mix"
    mainnet_dir.mkdir()
    testnet_dir.mkdir()
    log_main = tmp / "mainnet.log"
    log_test = tmp / "testnet.log"
    log_mix = tmp / "mix.log"
    main_proc = None
    test_proc = None
    try:
        main_write = 18646
        test_write = 18645
        main_pub = 19646

        main_proc = start_node(
            [
                str(BIN),
                "--mainnet",
                "--genesis",
                str(ROOT / "genesis-mainnet.json"),
                "--data-dir",
                str(mainnet_dir),
                "--local-rpc-port",
                str(main_write),
                "--public-rpc",
                "--public-rpc-port",
                str(main_pub),
                "--public-rpc-bind",
                "127.0.0.1",
                "--p2p-port",
                "28646",
            ],
            log_main,
        )
        main_info = wait_port("127.0.0.1", main_write)
        if field(main_info, "network") != "mainnet":
            return fail("mainnet getinfo network: " + main_info)
        if field(main_info, "network_id") != "ADDITION_MAINNET_V1":
            return fail("mainnet getinfo network_id: " + main_info)
        if field(main_info, "network_name") != "addition-mainnet":
            return fail("mainnet getinfo network_name: " + main_info)
        if "network=testnet" in main_info or "ADDITION_TESTNET_V1" in main_info:
            return fail("mainnet getinfo leaked testnet labels: " + main_info)
        if field(main_info, "pow_algorithm") != "memory_hard":
            return fail("mainnet pow: " + main_info)
        if field(main_info, "mine_deadline_sec") != "0":
            return fail("mainnet mine_deadline_sec must be 0 (no 30s leftover): " + main_info)
        diff = int(field(main_info, "difficulty_target") or "0")
        if diff != 0x000000FFFFFFFFFF:
            return fail("mainnet difficulty_target must stay 0x000000FFFFFFFFFF: " + main_info)

        pub = tcp_rpc("127.0.0.1", main_pub, "getinfo")
        if field(pub, "network") != "mainnet" or field(pub, "network_id") != "ADDITION_MAINNET_V1":
            return fail("public mainnet getinfo: " + pub)
        mine_pub = tcp_rpc("127.0.0.1", main_pub, "mine")
        if "disabled on public RPC" not in mine_pub:
            return fail("public mine should be disabled: " + mine_pub)

        marker = (mainnet_dir / "network.dat").read_text(encoding="utf-8")
        if "network_id=ADDITION_MAINNET_V1" not in marker or "network_mode=mainnet" not in marker:
            return fail("mainnet network.dat: " + marker)

        test_proc = start_node(
            [
                str(BIN),
                "--network",
                "testnet",
                "--genesis",
                str(ROOT / "genesis.json"),
                "--data-dir",
                str(testnet_dir),
                "--local-rpc-port",
                str(test_write),
                "--p2p-port",
                "28645",
                "--bootstrap",
                "127.0.0.1:28645",
            ],
            log_test,
        )
        test_info = wait_port("127.0.0.1", test_write)
        if field(test_info, "network") != "testnet":
            return fail("testnet getinfo network: " + test_info)
        if field(test_info, "network_id") != "ADDITION_TESTNET_V1":
            return fail("testnet getinfo network_id: " + test_info)
        if "network=mainnet" in test_info or "ADDITION_MAINNET_V1" in test_info:
            return fail("testnet getinfo leaked mainnet labels: " + test_info)

        mined = tcp_rpc("127.0.0.1", test_write, "mine miner1", timeout=20.0)
        if mined.startswith("error:"):
            return fail("testnet mine: " + mined)
        test_info2 = tcp_rpc("127.0.0.1", test_write, "getinfo")
        if field(test_info2, "height") == "0":
            return fail("testnet height still 0 after mine: " + test_info2)

        stop_node(test_proc)
        test_proc = None
        mix_dir.mkdir()
        src_blocks = testnet_dir / "blocks.dat"
        if src_blocks.exists():
            shutil.copy2(src_blocks, mix_dir / "blocks.dat")
        mix_proc = start_node(
            [
                str(BIN),
                "--mainnet",
                "--genesis",
                str(ROOT / "genesis-mainnet.json"),
                "--data-dir",
                str(mix_dir),
                "--local-rpc-port",
                "18647",
                "--p2p-port",
                "28647",
            ],
            log_mix,
        )
        deadline = time.time() + 20
        while mix_proc.poll() is None and time.time() < deadline:
            time.sleep(0.2)
        if mix_proc.poll() is None:
            stop_node(mix_proc)
            return fail("mainnet should refuse a testnet blocks.dat and exit")
        mix_log = log_mix.read_text(encoding="utf-8", errors="replace")
        if "chain load failed" not in mix_log and "network marker mismatch" not in mix_log:
            return fail("expected mix rejection in log: " + mix_log)

        t0 = time.monotonic()
        try:
            mined = tcp_rpc("127.0.0.1", main_write, "mine miner1", timeout=35.0)
            elapsed = time.monotonic() - t0
            if "deadline exceeded (30s)" in mined:
                return fail("mainnet mine aborted at 30s: " + mined)
            if mined.startswith("error:") and elapsed < 34.0:
                return fail("mainnet mine failed early (%.1fs): %s" % (elapsed, mined))
            print("ok: mainnet mine reply after %.1fs (no 30s abort)" % elapsed)
        except (socket.timeout, TimeoutError, OSError) as exc:
            elapsed = time.monotonic() - t0
            if elapsed < 30.0:
                return fail("mainnet mine connection died before 30s: %s" % exc)
            print("ok: mainnet mine still running after %.1fs (no 30s abort)" % elapsed)

        print("ok: mainnet getinfo network=mainnet network_id=ADDITION_MAINNET_V1")
        print("ok: testnet getinfo unchanged")
        print("ok: testnet blocks rejected on --mainnet data-dir")
        return 0
    except Exception as exc:
        return fail(str(exc))
    finally:
        stop_node(main_proc)
        stop_node(test_proc)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
