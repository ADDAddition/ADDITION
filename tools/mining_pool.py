#!/usr/bin/env python3
"""ADDITION local mining pool — coordinator + worker (not NiceHash).

Investigation summary
---------------------
Node `mine` is an in-process RPC on trusted write (loopback). There is no
external share/PoW work-split protocol and no public mine port. Public
read (38545) and LAN write must never receive `mine`.

This package is an honest **local coordinator**:
- Binds loopback only (default 127.0.0.1:18555).
- Accepts worker registrations over JSON HTTP.
- Serializes `mine <reward> <threads>` to native TEXT RPC on loopback
  (default testnet 127.0.0.1:8545) on a single worker thread so concurrent
  mine clients do not pile up against the seed.
- Defines a "share" as one completed mine attempt credited to the worker
  that requested the slot (success or deadline miss). Not a NiceHash market.

Prefer testnet (SHA3-512, mine deadline). Mainnet profile via loopback only;
do not hardcode “mainnet is live”.
"""

from __future__ import annotations

import argparse
import json
import os
import queue
import socket
import sys
import threading
import time
import uuid
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from ipaddress import ip_address
from typing import Any
from urllib.error import URLError
from urllib.request import Request, urlopen

DEFAULT_POOL_HOST = "127.0.0.1"
DEFAULT_POOL_PORT = 18555
DEFAULT_NODE_HOST = "127.0.0.1"
DEFAULT_NODE_PORT = 8545  # testnet write (prefer for demos)
DISCLAIMER = (
    "ADDITION local mining pool coordinator — not NiceHash; "
    "loopback mine only; prefer testnet; no public mine port; "
    "mainnet height follows live getinfo only"
)

# Desktop trusted write ports only. Never mine against public/LAN ports.
ALLOWED_LOOPBACK_WRITE_PORTS = frozenset({8545, 8546, 8547})  # testnet / mainnet / regtest
FORBIDDEN_MINE_PORTS = frozenset(
    {
        18545,  # LAN write (mainnet profile default in node config)
        18546,
        18547,  # must not expose testnet write here
        38545,  # public read
        38546,
        28545,  # P2P
        28546,
        28547,
    }
)


def is_loopback_host(host: str) -> bool:
    if host in {"localhost", "127.0.0.1", "::1"}:
        return True
    try:
        return ip_address(host).is_loopback
    except ValueError:
        return False


def require_loopback(host: str, label: str) -> None:
    if not is_loopback_host(host):
        raise SystemExit("error: %s must be loopback (got %s)" % (label, host))


def assert_safe_mine_target(host: str, port: int) -> None:
    """Refuse public/LAN mine targets so getinfo on 38545/38546 cannot be hung."""
    require_loopback(host, "native TEXT RPC host")
    if port in FORBIDDEN_MINE_PORTS:
        raise SystemExit(
            "error: refusing mine target %s:%s — public/LAN/P2P ports are not allowed. "
            "Use loopback write 8545 (testnet), 8546 (mainnet), or 8547 (regtest)."
            % (host, port)
        )
    if port not in ALLOWED_LOOPBACK_WRITE_PORTS:
        raise SystemExit(
            "error: refusing mine target %s:%s — only loopback write ports "
            "%s are allowed (prefer testnet 8545)."
            % (host, port, sorted(ALLOWED_LOOPBACK_WRITE_PORTS))
        )


def tcp_rpc(host: str, port: int, command: str, timeout: float = 120.0) -> str:
    assert_safe_mine_target(host, port)
    payload = command.strip() + "\n"
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.sendall(payload.encode("utf-8"))
        # Mine can run a long time; read until newline or socket close.
        chunks: list[bytes] = []
        sock.settimeout(timeout)
        while True:
            data = sock.recv(8192)
            if not data:
                break
            chunks.append(data)
            if b"\n" in data:
                break
    return b"".join(chunks).decode("utf-8", errors="replace").strip()


def kv_map(line: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for part in line.split():
        if "=" not in part:
            continue
        key, value = part.split("=", 1)
        out[key] = value
    return out


@dataclass
class Worker:
    worker_id: str
    name: str
    threads: int = 1
    shares: int = 0
    blocks: int = 0
    last_seen: float = field(default_factory=time.time)
    last_result: str = ""


@dataclass
class MineJob:
    job_id: str
    worker_id: str
    reward_address: str
    threads: int
    created_at: float = field(default_factory=time.time)
    done: bool = False
    ok: bool = False
    result_line: str = ""
    duration_ms: int = 0


class PoolState:
    def __init__(self, node_host: str, node_port: int, default_reward: str) -> None:
        assert_safe_mine_target(node_host, node_port)
        self.node_host = node_host
        self.node_port = node_port
        self.default_reward = default_reward
        self.workers: dict[str, Worker] = {}
        self.jobs: dict[str, MineJob] = {}
        self.queue: queue.Queue[str] = queue.Queue()
        self.lock = threading.Lock()
        self.blocks_found = 0
        self.shares_total = 0
        self.last_mine_ms = 0
        self.last_error = ""
        self.busy_job_id: str | None = None
        self.stop = threading.Event()
        self.miner_thread = threading.Thread(target=self._miner_loop, name="pool-miner", daemon=True)

    def start(self) -> None:
        self.miner_thread.start()

    def shutdown(self) -> None:
        self.stop.set()

    def register(self, name: str, threads: int) -> Worker:
        wid = uuid.uuid4().hex[:12]
        w = Worker(worker_id=wid, name=name or "worker", threads=max(1, threads))
        with self.lock:
            self.workers[wid] = w
        return w

    def request_slot(self, worker_id: str, reward: str | None, threads: int | None) -> MineJob:
        with self.lock:
            w = self.workers.get(worker_id)
            if w is None:
                raise KeyError("unknown worker")
            w.last_seen = time.time()
            thr = max(1, int(threads if threads is not None else w.threads))
            addr = (reward or self.default_reward).strip() or self.default_reward
            job = MineJob(
                job_id=uuid.uuid4().hex[:12],
                worker_id=worker_id,
                reward_address=addr,
                threads=thr,
            )
            self.jobs[job.job_id] = job
            self.queue.put(job.job_id)
            return job

    def status(self) -> dict[str, Any]:
        with self.lock:
            info_line = ""
            try:
                info_line = tcp_rpc(self.node_host, self.node_port, "getinfo", timeout=3.0)
            except Exception as exc:  # noqa: BLE001 — surface in status
                info_line = "error: %s" % exc
            info = kv_map(info_line) if not info_line.startswith("error:") else {}
            network = (info.get("network") or "").lower()
            height = int(info["height"]) if info.get("height", "").isdigit() else None
            est_hs = None
            if self.last_mine_ms > 0:
                # Crude local estimate: one in-process mine window → hashes unknown; report ms only.
                est_hs = None
            return {
                "disclaimer": DISCLAIMER,
                "kind": "local_coordinator",
                "nicehash": False,
                "public_mine": False,
                "node": "%s:%s" % (self.node_host, self.node_port),
                "queue_depth": self.queue.qsize(),
                "busy_job_id": self.busy_job_id,
                "blocks_found": self.blocks_found,
                "shares_total": self.shares_total,
                "last_mine_ms": self.last_mine_ms,
                "last_error": self.last_error,
                "estimated_hashrate": est_hs,
                "network": network or None,
                "height": height,
                "mainnetHasBlocks": bool(network == "mainnet" and height is not None and height >= 1),
                "pow_algorithm": info.get("pow_algorithm"),
                "workers": [
                    {
                        "worker_id": w.worker_id,
                        "name": w.name,
                        "threads": w.threads,
                        "shares": w.shares,
                        "blocks": w.blocks,
                        "last_seen": w.last_seen,
                        "last_result": w.last_result,
                    }
                    for w in self.workers.values()
                ],
            }

    def _miner_loop(self) -> None:
        while not self.stop.is_set():
            try:
                job_id = self.queue.get(timeout=0.5)
            except queue.Empty:
                continue
            with self.lock:
                job = self.jobs.get(job_id)
                if job is None:
                    continue
                self.busy_job_id = job_id
            started = time.time()
            try:
                # Single serialized mine — unlocks inside node while hashing, but we still
                # avoid opening many concurrent mine clients against the write socket.
                line = tcp_rpc(
                    self.node_host,
                    self.node_port,
                    "mine %s %s" % (job.reward_address, job.threads),
                    timeout=max(60.0, float(os.environ.get("ADDITION_POOL_MINE_TIMEOUT", "600"))),
                )
                ok = not line.startswith("error:")
            except Exception as exc:  # noqa: BLE001
                line = "error: %s" % exc
                ok = False
            duration_ms = int((time.time() - started) * 1000)
            with self.lock:
                job.done = True
                job.ok = ok
                job.result_line = line
                job.duration_ms = duration_ms
                self.last_mine_ms = duration_ms
                self.shares_total += 1
                w = self.workers.get(job.worker_id)
                if w is not None:
                    w.shares += 1
                    w.last_seen = time.time()
                    w.last_result = line[:240]
                    if ok and "mined block" in line:
                        w.blocks += 1
                        self.blocks_found += 1
                if not ok:
                    self.last_error = line
                self.busy_job_id = None


def make_handler(state: PoolState):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *args) -> None:
            print("%s - %s" % (self.address_string(), fmt % args))

        def _json(self, code: int, payload: object) -> None:
            data = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def _read_json(self) -> dict:
            length = int(self.headers.get("Content-Length") or "0")
            raw = self.rfile.read(max(0, length)).decode("utf-8", errors="replace")
            if not raw.strip():
                return {}
            obj = json.loads(raw)
            if not isinstance(obj, dict):
                raise ValueError("body must be a JSON object")
            return obj

        def do_GET(self) -> None:
            if self.path.split("?", 1)[0] in {"/", "/status"}:
                self._json(200, state.status())
                return
            if self.path.startswith("/job/"):
                job_id = self.path.split("/job/", 1)[1].strip("/")
                with state.lock:
                    job = state.jobs.get(job_id)
                    if job is None:
                        self._json(404, {"error": "unknown job"})
                        return
                    self._json(
                        200,
                        {
                            "job_id": job.job_id,
                            "worker_id": job.worker_id,
                            "done": job.done,
                            "ok": job.ok,
                            "result": job.result_line,
                            "duration_ms": job.duration_ms,
                            "reward_address": job.reward_address,
                            "threads": job.threads,
                        },
                    )
                return
            self._json(404, {"error": "not found", "disclaimer": DISCLAIMER})

        def do_POST(self) -> None:
            try:
                body = self._read_json()
            except Exception as exc:  # noqa: BLE001
                self._json(400, {"error": str(exc)})
                return
            path = self.path.split("?", 1)[0]
            if path == "/register":
                w = state.register(str(body.get("name") or "worker"), int(body.get("threads") or 1))
                self._json(
                    200,
                    {
                        "worker_id": w.worker_id,
                        "name": w.name,
                        "threads": w.threads,
                        "disclaimer": DISCLAIMER,
                    },
                )
                return
            if path == "/mine":
                wid = str(body.get("worker_id") or "")
                try:
                    job = state.request_slot(
                        wid,
                        str(body["reward"]) if body.get("reward") else None,
                        int(body["threads"]) if body.get("threads") is not None else None,
                    )
                except KeyError:
                    self._json(404, {"error": "unknown worker"})
                    return
                self._json(
                    200,
                    {
                        "job_id": job.job_id,
                        "queued": True,
                        "reward_address": job.reward_address,
                        "threads": job.threads,
                        "note": "in-process mine via loopback TEXT RPC; share = completed attempt",
                    },
                )
                return
            self._json(404, {"error": "not found"})

    return Handler


def run_coordinator(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=DISCLAIMER)
    p.add_argument("--bind", default=os.environ.get("ADDITION_POOL_BIND", DEFAULT_POOL_HOST))
    p.add_argument("--port", type=int, default=int(os.environ.get("ADDITION_POOL_PORT", DEFAULT_POOL_PORT)))
    p.add_argument("--node-host", default=os.environ.get("ADDITION_LOCAL_RPC_HOST", DEFAULT_NODE_HOST))
    p.add_argument("--node-port", type=int, default=int(os.environ.get("ADDITION_LOCAL_RPC_PORT", DEFAULT_NODE_PORT)))
    p.add_argument("--reward", default=os.environ.get("ADDITION_POOL_REWARD", "miner1"))
    args = p.parse_args(argv)
    require_loopback(args.bind, "pool bind")
    assert_safe_mine_target(args.node_host, args.node_port)
    state = PoolState(args.node_host, args.node_port, args.reward)
    state.start()
    print(
        "ADDITION local pool on http://%s:%s -> mine %s:%s (%s)"
        % (args.bind, args.port, args.node_host, args.node_port, DISCLAIMER)
    )
    server = ThreadingHTTPServer((args.bind, args.port), make_handler(state))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        state.shutdown()
        server.server_close()
    return 0


def _http_json(url: str, payload: dict | None = None, timeout: float = 10.0) -> dict:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = Request(url, data=data, method="GET" if payload is None else "POST")
    if payload is not None:
        req.add_header("Content-Type", "application/json")
    with urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def run_worker(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="ADDITION local pool worker (requests serialized mine slots)")
    p.add_argument("--pool", default=os.environ.get("ADDITION_POOL_URL", "http://127.0.0.1:18555"))
    p.add_argument("--name", default=os.environ.get("ADDITION_POOL_WORKER_NAME", "local-worker"))
    p.add_argument("--threads", type=int, default=int(os.environ.get("ADDITION_POOL_WORKER_THREADS", "2")))
    p.add_argument("--reward", default=os.environ.get("ADDITION_POOL_REWARD", ""))
    p.add_argument("--once", action="store_true", help="request one slot and wait for result")
    p.add_argument("--interval", type=float, default=2.0)
    args = p.parse_args(argv)
    # Pool URL host must be loopback.
    from urllib.parse import urlparse

    parsed = urlparse(args.pool)
    require_loopback(parsed.hostname or "", "pool URL host")
    print(DISCLAIMER)
    reg = _http_json(
        args.pool.rstrip("/") + "/register",
        {"name": args.name, "threads": args.threads},
    )
    wid = reg["worker_id"]
    print("registered worker_id=%s" % wid)

    def one_slot() -> None:
        body: dict[str, Any] = {"worker_id": wid, "threads": args.threads}
        if args.reward:
            body["reward"] = args.reward
        job = _http_json(args.pool.rstrip("/") + "/mine", body)
        job_id = job["job_id"]
        print("queued job_id=%s" % job_id)
        while True:
            st = _http_json(args.pool.rstrip("/") + "/job/" + job_id)
            if st.get("done"):
                print("result ok=%s ms=%s line=%s" % (st.get("ok"), st.get("duration_ms"), st.get("result")))
                return
            time.sleep(0.5)

    try:
        if args.once:
            one_slot()
            return 0
        while True:
            one_slot()
            time.sleep(max(0.2, args.interval))
    except (URLError, KeyboardInterrupt) as exc:
        print("worker stop: %s" % exc)
        return 1


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if not argv or argv[0] in {"-h", "--help"}:
        print("usage: mining_pool.py coordinator|worker [options]")
        print(DISCLAIMER)
        return 2
    cmd = argv[0]
    rest = argv[1:]
    if cmd in {"coordinator", "pool", "server"}:
        return run_coordinator(rest)
    if cmd in {"worker", "client"}:
        return run_worker(rest)
    print("unknown command: %s" % cmd, file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
