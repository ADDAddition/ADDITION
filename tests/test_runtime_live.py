#!/usr/bin/env python3
"""Start a local additiond and execute wallet/token/nft/stake/swap/contract/EVM/JSON."""

from __future__ import annotations

import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from http.client import HTTPConnection
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("ADDITIOND", ROOT / "build" / "additiond"))
MINE_TIMEOUT_SEC = 30.0
PRIVACY_KEY = "addition-research-privacy-master-key-32"


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


def pick_ports() -> dict[str, int]:
    preferred = {"write": 18545, "pub": 19545, "evm": 19546, "json": 19547}
    busy = False
    for port in preferred.values():
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                busy = True
                break
        except OSError:
            pass
    if not busy:
        return preferred
    return {"write": 29145, "pub": 29146, "evm": 29147, "json": 29148}


def start_proc(args: list[str], log_path: Path, env: dict[str, str] | None = None) -> subprocess.Popen:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    log = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        args,
        cwd=str(ROOT),
        stdin=subprocess.DEVNULL,
        stdout=log,
        stderr=subprocess.STDOUT,
        env=merged,
    )
    proc._log_handle = log  # type: ignore[attr-defined]
    return proc


def stop_proc(proc: subprocess.Popen | None) -> None:
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
    print("test failed:", msg, file=sys.stderr)
    return 1


def http_json(host: str, port: int, path: str, payload: dict | None = None) -> dict:
    conn = HTTPConnection(host, port, timeout=8)
    try:
        if payload is None:
            conn.request("GET", path)
        else:
            body = json.dumps(payload).encode("utf-8")
            conn.request("POST", path, body=body, headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        raw = resp.read().decode("utf-8")
        return json.loads(raw)
    finally:
        conn.close()


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        return 2

    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-runtime-live-"))
    data_dir = tmp / "node"
    data_dir.mkdir()
    node = None
    evm = None
    adapter = None
    try:
        env = os.environ.copy()
        env["ADDITION_PRIVACY_MASTER_KEY"] = PRIVACY_KEY
        for key in (
            "ADDITION_ENABLE_PUBLIC_RPC",
            "ADDITION_PUBLIC_RPC_PORT",
            "ADDITION_PUBLIC_RPC_BIND",
            "ADDITION_LOCAL_RPC_PORT",
            "ADDITION_P2P_PORT",
            "ADDITION_AUTO_MINE",
        ):
            env.pop(key, None)
        node = start_proc(
            [
                str(BIN),
                "--network",
                "testnet",
                "--data-dir",
                str(data_dir),
                "--local-rpc-port",
                str(ports["write"]),
                "--public-rpc",
                "--public-rpc-bind",
                "127.0.0.1",
                "--public-rpc-port",
                str(ports["pub"]),
            ],
            tmp / "node.log",
            env,
        )
        info0 = wait_port("127.0.0.1", ports["write"])
        if "network=testnet" not in info0:
            return fail("getinfo: " + info0)

        selftest = tcp_rpc("127.0.0.1", ports["write"], "crypto_selftest")
        if not selftest.startswith("ok:"):
            return fail("crypto_selftest: " + selftest)

        alice = tcp_rpc("127.0.0.1", ports["write"], "createwallet alice")
        alice_addr = field(alice, "address")
        if not alice_addr or "algo=ml-dsa-87" not in alice:
            return fail("createwallet: " + alice)
        bob = tcp_rpc("127.0.0.1", ports["write"], "createwallet bob")
        bob_addr = field(bob, "address")
        if not bob_addr:
            return fail("createwallet bob: " + bob)

        mined = tcp_rpc("127.0.0.1", ports["write"], "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5)
        if not mined.startswith("mined block"):
            return fail("mine: " + mined)
        bal = tcp_rpc("127.0.0.1", ports["write"], "getbalance " + alice_addr)
        if bal.strip() != "50":
            return fail("getbalance after mine: " + bal)

        sent = tcp_rpc("127.0.0.1", ports["write"], "wallet_send alice %s 5 1" % bob_addr)
        if "ok:gossiped" not in sent:
            return fail("wallet_send: " + sent)
        mined2 = tcp_rpc("127.0.0.1", ports["write"], "mine " + alice_addr, timeout=MINE_TIMEOUT_SEC + 5)
        if not mined2.startswith("mined block"):
            return fail("mine 2: " + mined2)
        if tcp_rpc("127.0.0.1", ports["write"], "getbalance " + bob_addr).strip() != "5":
            return fail("bob balance after wallet_send")

        if tcp_rpc("127.0.0.1", ports["write"], "token_create AAA alice 100000 1000") != "ok":
            return fail("token_create AAA")
        if tcp_rpc("127.0.0.1", ports["write"], "token_create BBB alice 100000 1000") != "ok":
            return fail("token_create BBB")
        if tcp_rpc("127.0.0.1", ports["write"], "token_mint AAA alice bob 50") != "ok":
            return fail("token_mint")
        if tcp_rpc("127.0.0.1", ports["write"], "token_transfer AAA alice bob 10") != "ok":
            return fail("token_transfer")
        if tcp_rpc("127.0.0.1", ports["write"], "token_balance AAA alice") != "990":
            return fail("token_balance alice")
        if tcp_rpc("127.0.0.1", ports["write"], "token_balance AAA bob") != "60":
            return fail("token_balance bob")
        info = tcp_rpc("127.0.0.1", ports["write"], "token_info AAA")
        if "symbol=AAA" not in info or "total_supply=1050" not in info:
            return fail("token_info: " + info)

        meta = "https://example.com/nft/item1.png#sha3-512:deadbeef"
        if tcp_rpc("127.0.0.1", ports["write"], "nft_mint COL item1 alice " + meta) != "ok":
            return fail("nft_mint")
        if tcp_rpc("127.0.0.1", ports["write"], "nft_transfer COL item1 alice bob") != "ok":
            return fail("nft_transfer")
        if tcp_rpc("127.0.0.1", ports["write"], "nft_owner COL item1") != "bob":
            return fail("nft_owner")
        nft_info = tcp_rpc("127.0.0.1", ports["write"], "nft_info COL item1")
        if "owner=bob" not in nft_info or meta not in nft_info:
            return fail("nft_info: " + nft_info)

        if tcp_rpc("127.0.0.1", ports["write"], "stake " + alice_addr + " 40") != "ok":
            return fail("stake")
        if tcp_rpc("127.0.0.1", ports["write"], "getbalance " + alice_addr) != "54":
            # 50 coinbase + 50 second mine - 5 send - 1 fee - 40 stake = 54
            live = tcp_rpc("127.0.0.1", ports["write"], "getbalance " + alice_addr)
            if live != "54":
                return fail("getbalance after stake: " + live)
        if tcp_rpc("127.0.0.1", ports["write"], "staked " + alice_addr) != "40":
            return fail("staked")
        if tcp_rpc("127.0.0.1", ports["write"], "stake_reward 100") != "ok":
            return fail("stake_reward")
        claimed = tcp_rpc("127.0.0.1", ports["write"], "stake_claim " + alice_addr)
        if claimed in {"0", ""} or claimed.startswith("error:"):
            return fail("stake_claim: " + claimed)
        after_claim = int(tcp_rpc("127.0.0.1", ports["write"], "getbalance " + alice_addr))
        if after_claim <= 54:
            return fail("getbalance after claim: %s" % after_claim)
        if tcp_rpc("127.0.0.1", ports["write"], "unstake " + alice_addr + " 40") != "ok":
            return fail("unstake")
        after_unstake = int(tcp_rpc("127.0.0.1", ports["write"], "getbalance " + alice_addr))
        if after_unstake <= after_claim:
            return fail("getbalance after unstake: %s" % after_unstake)

        if tcp_rpc("127.0.0.1", ports["write"], "swap_tvl") != "tvl=0":
            return fail("swap_tvl empty")
        if tcp_rpc("127.0.0.1", ports["write"], "swap_pool_create AAA BBB 30") != "ok":
            return fail("swap_pool_create")
        if tcp_rpc("127.0.0.1", ports["write"], "add_liquidity AAA BBB alice 200 200") != "ok":
            return fail("add_liquidity")
        pool = tcp_rpc("127.0.0.1", ports["write"], "swap_pool_info AAA BBB")
        if "reserve_AAA=200" not in pool or "reserve_BBB=200" not in pool:
            return fail("swap_pool_info: " + pool)
        if tcp_rpc("127.0.0.1", ports["write"], "swap_tvl") != "tvl=400":
            return fail("swap_tvl live")
        quote = tcp_rpc("127.0.0.1", ports["write"], "swap_quote AAA BBB 10")
        if quote.startswith("error:") or quote == "0":
            return fail("swap_quote: " + quote)
        swapped = tcp_rpc("127.0.0.1", ports["write"], "swap_exact_in AAA BBB alice 10 1")
        if swapped.startswith("error:"):
            return fail("swap_exact_in: " + swapped)
        if tcp_rpc("127.0.0.1", ports["write"], "token_balance AAA alice") != "780":
            return fail("token_balance after swap")

        cid = tcp_rpc("127.0.0.1", ports["write"], "contract_deploy alice kvstore")
        if not cid or cid.startswith("error:"):
            return fail("contract_deploy: " + cid)
        if tcp_rpc("127.0.0.1", ports["write"], "contract_call %s set counter 7" % cid) != "ok":
            return fail("contract set")
        if tcp_rpc("127.0.0.1", ports["write"], "contract_call %s add counter 3" % cid) != "10":
            return fail("contract add")
        if tcp_rpc("127.0.0.1", ports["write"], "contract_call %s get counter 0" % cid) != "10":
            return fail("contract get")

        pub_mine = tcp_rpc("127.0.0.1", ports["pub"], "mine")
        if "command disabled on public RPC" not in pub_mine:
            return fail("public mine: " + pub_mine)
        pub_wallet = tcp_rpc("127.0.0.1", ports["pub"], "createwallet eve")
        if "command disabled on public RPC" not in pub_wallet:
            return fail("public createwallet: " + pub_wallet)
        pub_info = tcp_rpc("127.0.0.1", ports["pub"], "getinfo")
        if "network=testnet" not in pub_info:
            return fail("public getinfo: " + pub_info)
        raw = tcp_rpc("127.0.0.1", ports["pub"], "getblockraw 1")
        if "ok:BLKDATA|" not in raw:
            return fail("public getblockraw: " + raw)

        js = http_json("127.0.0.1", ports["pub"], "/jsonrpc?method=getinfo")
        if "network=testnet" not in str(js.get("result", "")):
            return fail("jsonrpc getinfo: %s" % js)
        js_block = http_json("127.0.0.1", ports["pub"], "/jsonrpc?method=getblockraw&params=1")
        if "ok:BLKDATA|" not in str(js_block.get("result", "")):
            return fail("jsonrpc getblockraw: %s" % js_block)
        js_write = http_json(
            "127.0.0.1",
            ports["pub"],
            "/jsonrpc",
            {"jsonrpc": "2.0", "id": 9, "method": "mine", "params": []},
        )
        if "command disabled on public RPC" not in str(js_write.get("error", {})):
            return fail("jsonrpc mine: %s" % js_write)
        for blocked in ("wallet_send", "stake", "unstake", "swap_exact_in", "add_liquidity"):
            js_blocked = http_json(
                "127.0.0.1",
                ports["pub"],
                "/jsonrpc",
                {"jsonrpc": "2.0", "id": 10, "method": blocked, "params": []},
            )
            if "command disabled on public RPC" not in str(js_blocked.get("error", {})):
                return fail("jsonrpc %s: %s" % (blocked, js_blocked))

        evm_env = os.environ.copy()
        evm_env["ADDITION_EVM_BIND"] = "127.0.0.1"
        evm_env["ADDITION_EVM_PORT"] = str(ports["evm"])
        evm_env["ADDITION_LOCAL_RPC_HOST"] = "127.0.0.1"
        evm_env["ADDITION_LOCAL_RPC_PORT"] = str(ports["write"])
        evm = start_proc(
            [sys.executable, str(ROOT / "web" / "evm" / "evm_rpc_bridge.py")],
            tmp / "evm.log",
            evm_env,
        )
        deadline = time.time() + 10
        evm_ok = False
        last_evm = ""
        while time.time() < deadline:
            try:
                chain_id = http_json(
                    "127.0.0.1",
                    ports["evm"],
                    "/",
                    {"jsonrpc": "2.0", "id": 1, "method": "eth_chainId", "params": []},
                )
                last_evm = str(chain_id)
                if chain_id.get("result") == hex(424242):
                    evm_ok = True
                    break
            except (OSError, json.JSONDecodeError, ValueError):
                time.sleep(0.2)
        if not evm_ok:
            return fail("evm eth_chainId: " + last_evm)
        block_n = http_json(
            "127.0.0.1",
            ports["evm"],
            "/",
            {"jsonrpc": "2.0", "id": 2, "method": "eth_blockNumber", "params": []},
        )
        if int(block_n["result"], 16) < 1:
            return fail("evm eth_blockNumber: %s" % block_n)
        evm_bal = http_json(
            "127.0.0.1",
            ports["evm"],
            "/",
            {"jsonrpc": "2.0", "id": 3, "method": "eth_getBalance", "params": [alice_addr]},
        )
        if int(evm_bal["result"], 16) <= 0:
            return fail("evm eth_getBalance: %s" % evm_bal)

        adapter = start_proc(
            [
                sys.executable,
                str(ROOT / "tools" / "addition_jsonrpc_adapter.py"),
                "--public-read",
                "--bind",
                "127.0.0.1",
                "--port",
                str(ports["json"]),
                "--rpc-host",
                "127.0.0.1",
                "--rpc-port",
                str(ports["pub"]),
            ],
            tmp / "adapter.log",
        )
        deadline = time.time() + 10
        adapter_ok = False
        last_ad = ""
        while time.time() < deadline:
            try:
                ad = http_json("127.0.0.1", ports["json"], "/jsonrpc?method=peers")
                last_ad = str(ad)
                if "result" in ad:
                    adapter_ok = True
                    break
            except (OSError, json.JSONDecodeError, ValueError):
                time.sleep(0.2)
        if not adapter_ok:
            return fail("adapter peers: " + last_ad)
        ad_write = http_json(
            "127.0.0.1",
            ports["json"],
            "/jsonrpc",
            {"jsonrpc": "2.0", "id": 4, "method": "token_create", "params": ["X", "a", 1, 1]},
        )
        if "error" not in ad_write:
            return fail("adapter must refuse writes: %s" % ad_write)

        print("runtime live functions ok")
        print("ports", ports)
        return 0
    finally:
        stop_proc(adapter)
        stop_proc(evm)
        stop_proc(node)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
