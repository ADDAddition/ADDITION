#!/usr/bin/env python3
"""Client-held keys over RPC (mobile): external-signed stake/unstake/vote and AMLS1 relay RPCs.

Write RPC stays 127.0.0.1. This is not economic security, not ZK, not
cross-chain, and not a mainnet claim. Public live RPC stays testnet.
"""

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


def field(text: str, name: str) -> str:
    match = re.search(r"(?:^|[\s:])%s=(\S+)" % re.escape(name), text)
    return match.group(1) if match else ""


def port_open(host: str, port: int) -> bool:
    try:
        with socket.create_connection((host, port), timeout=0.3):
            return True
    except OSError:
        return False


def pick_ports() -> dict[str, int]:
    for base in (18665, 19265, 19865):
        cand = {}
        for i, n in enumerate("abc"):
            cand[n + "_write"] = base + i
            cand[n + "_p2p"] = base + 11000 + i
        if not any(port_open("127.0.0.1", p) for p in cand.values()):
            return cand
    raise RuntimeError("no free port block")


def tcp_rpc(host: str, port: int, command: str, timeout: float = 20.0) -> str:
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


def start_node(args: list[str], log_path: Path) -> subprocess.Popen:
    env = os.environ.copy()
    env["ADDITION_ENABLE_P2P_RPC"] = "1"
    env.pop("ADDITION_ENABLE_PUBLIC_RPC", None)
    env.pop("ADDITION_LOCAL_RPC_PORT", None)
    env.pop("ADDITION_P2P_PORT", None)
    env.pop("ADDITION_AUTO_MINE", None)
    env.pop("ADDITION_MAINNET_MODE", None)
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
    print("test failed:", msg, file=sys.stderr)
    return 1




STAKING_TOML = """
staking_activation_height = 0
stake_min_amount = 10
stake_maturity = 2
stake_unbond_delay = 3
stake_votes_per_block = 1
stake_vote_quorum = 1
stake_min_live_tickets = 1
stake_reward_bps = 3000
"""


def rpc_all(port: int, command: str, timeout: float = 20.0) -> str:
    """Like tcp_rpc but keeps reading multi-line replies until the socket idles."""
    with socket.create_connection(("127.0.0.1", port), timeout=timeout) as sock:
        sock.sendall((command.strip() + "\n").encode())
        chunks: list[bytes] = []
        while True:
            try:
                data = sock.recv(65536)
            except socket.timeout:
                break
            if not data:
                break
            chunks.append(data)
            sock.settimeout(0.7)
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def hexs(s: str) -> str:
    return s.encode().hex()


def main() -> int:
    if not BIN.is_file():
        print("error: additiond not built at", BIN, file=sys.stderr)
        return 2
    ports = pick_ports()
    tmp = Path(tempfile.mkdtemp(prefix="addition-client-sign-"))
    cfg = tmp / "staking.toml"
    cfg.write_text(STAKING_TOML, encoding="utf-8")
    procs = {}
    try:
        for n in "ab":
            d = tmp / n
            d.mkdir()
            args = [str(BIN), "--regtest", "--config", str(cfg), "--data-dir", str(d),
                    "--local-rpc-port", str(ports[n + "_write"]), "--p2p-port", str(ports[n + "_p2p"])]
            procs[n] = start_node(args, tmp / (n + ".log"))
            wait_port("127.0.0.1", ports[n + "_write"])
        A = lambda c: rpc_all(ports["a_write"], c)
        B = lambda c: rpc_all(ports["b_write"], c)
        A("addpeer 127.0.0.1:%s" % ports["b_p2p"])
        B("addpeer 127.0.0.1:%s" % ports["a_p2p"])

        # ---- staking with an external signer (throwaway regtest keys) ----
        miner = field(A("createwallet miner"), "address")
        dev = A("createwallet device")  # stands in for the phone; only pub/address used below
        addr, pub = field(dev, "address"), field(dev, "pub")
        if not miner or not addr or not pub:
            return fail("createwallet: " + dev[:200])
        for _ in range(3):
            if "error" in A("mine " + miner):
                return fail("mine")
        if "error" in A("wallet_send miner %s 40 1" % addr):
            return fail("fund device")
        A("mine " + miner)
        n0 = field(A("get_nonce " + addr), "nonce")
        if n0 == "":
            return fail("get_nonce: " + A("get_nonce " + addr))

        def sign(msg_hex: str) -> str:
            out = A("sign_message device " + msg_hex)  # client-side signer stand-in
            return out.strip()

        built = A("stake_build %s %s 20 1" % (addr, pub))
        sh, nonce, fee = field(built, "sign_hash"), field(built, "nonce"), field(built, "fee")
        if not sh or nonce != n0:
            return fail("stake_build: " + built)
        bad = A("stake_submit %s %s 20 %s %s %s" % (addr, pub, fee, nonce, A("sign_message miner " + hexs(sh))))
        if not bad.startswith("error"):
            return fail("wrong-key signature accepted: " + bad)
        ok = A("stake_submit %s %s 20 %s %s %s" % (addr, pub, fee, nonce, sign(hexs(sh))))
        ticket = field(ok, "ticket")
        if not ok.startswith("ok:stake_lock") or not ticket:
            return fail("stake_submit: " + ok)
        replay = A("stake_submit %s %s 20 %s %s %s" % (addr, pub, fee, nonce, sign(hexs(sh))))
        if replay.startswith("ok"):
            return fail("replayed stake accepted")
        voted = False
        for _ in range(8):
            out = A("mine " + miner)
            if "error" not in out:
                continue
            # Mining waits for a quorum vote: the remote signer votes via vote_build/vote_submit.
            vb = A("vote_build %s 1" % ticket)
            if field(vb, "selected") != "1":
                return fail("ticket not selected: " + vb + " | " + out)
            vs = A("vote_submit %s %s %s 1 %s" % (ticket, field(vb, "voted_height"), field(vb, "voted_hash"),
                                                 sign(field(vb, "message_hex"))))
            if not vs.startswith("ok:vote"):
                return fail("vote_submit: " + vs)
            out = A("mine " + miner)
            if "error" in out:
                return fail("mine after remote vote: " + out)
            voted = True
            break
        if not voted:
            return fail("no vote round reached; stake_info=" + A("stake_info"))
        ub = A("unstake_build %s %s %s 1" % (addr, pub, ticket))
        if not field(ub, "sign_hash"):
            return fail("unstake_build: " + ub)
        us = A("unstake_submit %s %s %s %s %s %s" % (addr, pub, ticket, field(ub, "fee"), field(ub, "nonce"),
                                                     sign(hexs(field(ub, "sign_hash")))))
        if not us.startswith("ok:stake_unlock"):
            return fail("unstake_submit: " + us)

        # ---- AMLS1 relay with client-held keys ----
        pubd = A("msg_kp_publish alicew alice-client-test 7")
        if "fingerprint=" not in pubd:
            return fail("node-held msg_kp_publish (regtest compat): " + pubd)
        pkg = field(A("msg_kp_get alice-client-test"), "package")
        if not pkg:
            return fail("msg_kp_get")
        tampered = pkg[:-4] + ("0000" if pkg[-4:] != "0000" else "1111")
        if B("msg_kp_submit " + tampered).startswith("ok"):
            return fail("tampered key package accepted")
        sub = B("msg_kp_submit " + pkg)
        if not sub.startswith("ok verified=ml-dsa-87"):
            return fail("msg_kp_submit: " + sub)
        if field(B("msg_kp_get alice-client-test"), "package") != pkg:
            return fail("package not served by relay b")
        bk = B("msg_key bobw")
        mbox, bpk = field(bk, "mailbox"), field(bk, "pubkey")
        sent = A("msg_send %s 600 hello-from-client-api" % bpk)
        if "msg_id=" not in sent:
            return fail("msg_send: " + sent)
        lines = [l for l in A("msg_env_fetch %s" % mbox).splitlines() if l.startswith("PMSG|v1|")]
        if len(lines) != 1:
            return fail("msg_env_fetch on a: %d" % len(lines))
        if B("msg_env_post " + lines[0][:-2] + "zz").startswith("ok stored"):
            return fail("malformed envelope stored")
        post = B("msg_env_post " + lines[0])
        if not post.startswith("ok") or field(post, "msg_id") != field(sent, "msg_id"):
            return fail("msg_env_post: " + post)
        got = B("msg_env_fetch %s" % mbox)
        if lines[0] not in got:
            return fail("msg_env_fetch on b: " + got[:200])
        if "hello-from-client-api" not in B("msg_read bobw " + field(sent, "msg_id")):
            return fail("node-held wallet could not read relayed envelope")
        if not B("msg_env_fetch " + "0" * 32).startswith("envelopes=0"):
            return fail("foreign mailbox not empty")
        print("client signing ok: stake/vote/unstake external-signed; AMLS1 kp_submit/kp_get/env_post/env_fetch ok")
        return 0
    finally:
        for p in procs.values():
            stop_node(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
