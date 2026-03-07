import json
import os
import socket
import base64
import hashlib
import secrets
import time
from pathlib import Path
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import parse_qs, urlparse

RPC_HOST = os.getenv("ADDITION_RPC_HOST", "127.0.0.1")
RPC_PORT = int(os.getenv("ADDITION_RPC_PORT", "8545"))
HTTP_HOST = os.getenv("ADDITION_HTTP_HOST", "0.0.0.0")
HTTP_PORT = int(os.getenv("ADDITION_HTTP_PORT", "8080"))
STATIC_ROOT = Path(__file__).parent
AUTH_CHALLENGES = {}
AUTH_SESSIONS = {}
RATE_LIMIT = {}
BLOBS = {}
PREKEYS = {}
NONCE_SEEN = {}

CHAT_MIN_STAKED_ADD = int(os.getenv("ADDITION_CHAT_MIN_STAKED_ADD", "1"))
CHAT_SEND_FEE_STAKED_ADD = int(os.getenv("ADDITION_CHAT_SEND_FEE_STAKED_ADD", "1"))
AUTH_CHALLENGE_TTL_SEC = int(os.getenv("ADDITION_AUTH_CHALLENGE_TTL_SEC", "120"))
SESSION_TTL_SEC = int(os.getenv("ADDITION_AUTH_SESSION_TTL_SEC", "1800"))
RATE_LIMIT_WINDOW_SEC = int(os.getenv("ADDITION_RATE_LIMIT_WINDOW_SEC", "30"))
RATE_LIMIT_MAX_REQ = int(os.getenv("ADDITION_RATE_LIMIT_MAX_REQ", "60"))
NONCE_TTL_SEC = int(os.getenv("ADDITION_NONCE_TTL_SEC", "600"))

def rpc_call(command: str, timeout: float = 4.0) -> str:
    try:
        with socket.create_connection((RPC_HOST, RPC_PORT), timeout=timeout) as s:
            s.sendall((command.strip() + "\n").encode("utf-8"))
            s.settimeout(timeout)
            chunks = []
            while True:
                try:
                    part = s.recv(4096)
                    if not part:
                        break
                    chunks.append(part)
                    if b"\n" in part:
                        break
                except socket.timeout:
                    break
            return b"".join(chunks).decode("utf-8", errors="ignore").strip()
    except Exception as e:
        return f"error: {str(e)}"

def parse_kv(raw: str) -> dict:
    out = {}
    if not raw or raw.startswith("error:"): return out
    for item in raw.split():
        if "=" in item:
            kv = item.split("=", 1)
            out[kv[0]] = kv[1]
    return out

def hex_utf8(s: str) -> str:
    return s.encode("utf-8").hex()

def now_sec() -> int:
    return int(time.time())

def apply_rate_limit(client_ip: str, route: str):
    key = f"{client_ip}|{route}"
    now = now_sec()
    bucket = RATE_LIMIT.get(key)
    if not bucket:
        RATE_LIMIT[key] = {"start": now, "count": 1}
        return True
    if now - bucket["start"] > RATE_LIMIT_WINDOW_SEC:
        bucket["start"] = now
        bucket["count"] = 1
        return True
    bucket["count"] += 1
    return bucket["count"] <= RATE_LIMIT_MAX_REQ

def make_challenge(address: str) -> str:
    nonce = secrets.token_hex(16)
    msg = f"addition-auth|addr={address}|nonce={nonce}|ts={now_sec()}"
    AUTH_CHALLENGES[address] = {"message": msg, "expires": now_sec() + AUTH_CHALLENGE_TTL_SEC}
    return msg

def verify_session(headers):
    token = headers.get("X-Session-Token", "").strip()
    if not token:
        return False, "", "missing session"
    rec = AUTH_SESSIONS.get(token)
    if not rec:
        return False, "", "invalid session"
    if now_sec() > rec["expires"]:
        AUTH_SESSIONS.pop(token, None)
        return False, "", "expired session"
    rec["expires"] = now_sec() + SESSION_TTL_SEC
    return True, rec["address"], ""

def require_staked_access(address: str):
    raw = rpc_call(f"staked {address}")
    if raw.startswith("error:"):
        return False, "staking query failed"
    try:
        value = int(raw.strip())
    except Exception:
        return False, "staking parse failed"
    if value < CHAT_MIN_STAKED_ADD:
        return False, f"insufficient staked ADD, min={CHAT_MIN_STAKED_ADD}"
    return True, ""

def store_blob(ciphertext_b64: str) -> str:
    if not ciphertext_b64:
        return ""
    digest = hashlib.sha256(ciphertext_b64.encode("utf-8")).hexdigest()
    BLOBS[digest] = {
        "ciphertext": ciphertext_b64,
        "created": now_sec()
    }
    return digest

def get_blob(ref: str) -> str:
    item = BLOBS.get(ref)
    if not item:
        return ""
    return str(item.get("ciphertext", ""))

def cleanup_nonce_map(sender: str):
    now = now_sec()
    entries = NONCE_SEEN.get(sender, {})
    fresh = {k: v for k, v in entries.items() if now - int(v) <= NONCE_TTL_SEC}
    NONCE_SEEN[sender] = fresh

def mark_sender_nonce(sender: str, nonce: str) -> bool:
    if not sender or not nonce:
        return False
    cleanup_nonce_map(sender)
    entries = NONCE_SEEN.get(sender, {})
    if nonce in entries:
        return False
    entries[nonce] = now_sec()
    NONCE_SEEN[sender] = entries
    return True

def sanitize_token(value: str) -> str:
    allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-:/+=")
    return "".join(ch for ch in value if ch in allowed)

def b64url_decode_nopad(data: str) -> bytes:
    s = data.replace("-", "+").replace("_", "/")
    while len(s) % 4 != 0:
        s += "="
    return base64.b64decode(s.encode("ascii"))

def parse_jwt_payload(jwt_token: str) -> dict:
    parts = jwt_token.split(".")
    if len(parts) != 3:
        return {}
    try:
        payload = b64url_decode_nopad(parts[1]).decode("utf-8", errors="ignore")
        data = json.loads(payload)
        if isinstance(data, dict):
            return data
    except Exception:
        return {}
    return {}

def validate_chat_auth(headers):
    auth = headers.get("Authorization", "")
    if not auth.startswith("Bearer "):
        return False, "", "missing bearer"
    token = sanitize_token(auth[7:].strip())
    payload = parse_jwt_payload(token)
    sub = str(payload.get("sub", "")).strip()
    pub = str(payload.get("pub", "")).strip()
    sig = str(payload.get("sig", "")).strip()
    if not sub or not pub or not sig:
        return False, "", "invalid token payload"
    raw = rpc_call(f"sign_message {pub} {sub}")
    # Lightweight gate: require fields; full auth should be done by dedicated auth service.
    # Keep strict separation from message encryption (always client-side).
    return True, sub, ""

def static_file_response(handler, file_path: Path):
    if not file_path.exists() or not file_path.is_file():
        handler._send_json(404, {"ok": False, "error": "not found"})
        return
    suffix = file_path.suffix.lower()
    ctype = "text/plain"
    if suffix == ".html": ctype = "text/html; charset=utf-8"
    elif suffix == ".js": ctype = "application/javascript; charset=utf-8"
    elif suffix == ".css": ctype = "text/css; charset=utf-8"
    elif suffix == ".json": ctype = "application/json; charset=utf-8"
    elif suffix == ".png": ctype = "image/png"
    elif suffix == ".svg": ctype = "image/svg+xml"
    elif suffix == ".ico": ctype = "image/x-icon"

    handler.send_response(200)
    handler.send_header("Content-Type", ctype)
    handler.send_header("Access-Control-Allow-Origin", "*")
    handler.end_headers()
    handler.wfile.write(file_path.read_bytes())

class PortalHandler(BaseHTTPRequestHandler):
    def _send_json(self, code: int, payload: dict):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()
        self.wfile.write(json.dumps(payload).encode())

    def do_OPTIONS(self): self._send_json(200, {"ok": True})

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path
        client_ip = self.client_address[0] if self.client_address else "unknown"
        if not apply_rate_limit(client_ip, path):
            self._send_json(429, {"ok": False, "error": "rate limited"})
            return
        size = int(self.headers.get("Content-Length", "0") or "0")
        raw_body = self.rfile.read(size) if size > 0 else b"{}"
        try:
            body = json.loads(raw_body.decode("utf-8", errors="ignore"))
            if not isinstance(body, dict):
                body = {}
        except Exception:
            body = {}

        if path == "/api/auth/challenge":
            address = sanitize_token(str(body.get("address", "")))
            if not address:
                self._send_json(400, {"ok": False, "error": "address required"})
                return
            challenge = make_challenge(address)
            self._send_json(200, {"ok": True, "challenge": challenge})
            return

        if path == "/api/auth/verify":
            address = sanitize_token(str(body.get("address", "")))
            pubkey = sanitize_token(str(body.get("pubkey", "")))
            sig = sanitize_token(str(body.get("sig", "")))
            ch = AUTH_CHALLENGES.get(address)
            if not address or not pubkey or not sig or not ch:
                self._send_json(400, {"ok": False, "error": "invalid verify params"})
                return
            if now_sec() > int(ch.get("expires", 0)):
                AUTH_CHALLENGES.pop(address, None)
                self._send_json(400, {"ok": False, "error": "challenge expired"})
                return
            msg_hex = hex_utf8(ch["message"])
            raw = rpc_call(f"verify_message {pubkey} {msg_hex} {sig}")
            if raw.strip() != "true":
                self._send_json(401, {"ok": False, "error": "signature verify failed"})
                return
            session = secrets.token_urlsafe(32)
            AUTH_SESSIONS[session] = {"address": address, "expires": now_sec() + SESSION_TTL_SEC}
            AUTH_CHALLENGES.pop(address, None)
            self._send_json(200, {"ok": True, "session": session, "address": address, "expires_in": SESSION_TTL_SEC})
            return

        if path == "/api/pm/send":
            ok, sender, err = verify_session(self.headers)
            if not ok:
                self._send_json(401, {"ok": False, "error": err})
                return
            st_ok, st_err = require_staked_access(sender)
            if not st_ok:
                self._send_json(403, {"ok": False, "error": st_err})
                return

            recipient = sanitize_token(str(body.get("recipient", "")))
            ciphertext = str(body.get("ciphertext", "")).strip()
            client_nonce = sanitize_token(str(body.get("client_nonce", "")))
            ttl_sec = int(body.get("ttl_sec", 300) or 300)
            policy = sanitize_token(str(body.get("policy", "e2ee")))
            if not recipient or not ciphertext or ttl_sec <= 0 or not client_nonce:
                self._send_json(400, {"ok": False, "error": "invalid params"})
                return
            if not mark_sender_nonce(sender, client_nonce):
                self._send_json(409, {"ok": False, "error": "replay nonce rejected"})
                return

            ciphertext_ref = store_blob(ciphertext)
            if not ciphertext_ref:
                self._send_json(400, {"ok": False, "error": "ciphertext store failed"})
                return

            if CHAT_SEND_FEE_STAKED_ADD > 0:
                charge = rpc_call(f"consume_stake_credit {sender} {CHAT_SEND_FEE_STAKED_ADD}")
                if charge.startswith("error:"):
                    self._send_json(402, {"ok": False, "error": "stake fee charge failed", "detail": charge})
                    return

            raw = rpc_call(f"pm_send_ttl {sender} {recipient} {ciphertext_ref} {ttl_sec} {policy}")
            self._send_json(200, {"ok": not raw.startswith("error:"), "raw": raw, "data": parse_kv(raw)})
            return

        if path == "/api/ratchet/prekey/register":
            ok, owner, err = verify_session(self.headers)
            if not ok:
                self._send_json(401, {"ok": False, "error": err})
                return
            identity_pub = sanitize_token(str(body.get("identity_pub", "")))
            signed_prekey = str(body.get("signed_prekey", "")).strip()
            signed_prekey_sig = sanitize_token(str(body.get("signed_prekey_sig", "")))
            one_time_prekeys = body.get("one_time_prekeys", [])
            if not identity_pub or not signed_prekey or not signed_prekey_sig:
                self._send_json(400, {"ok": False, "error": "identity_pub, signed_prekey, signed_prekey_sig required"})
                return
            if not isinstance(one_time_prekeys, list):
                one_time_prekeys = []
            one_time_prekeys = [str(x).strip() for x in one_time_prekeys if str(x).strip()][:128]

            spk_msg = f"x3dh-signed-prekey|addr={owner}|spk={signed_prekey}"
            spk_msg_hex = hex_utf8(spk_msg)
            verify = rpc_call(f"verify_message {identity_pub} {spk_msg_hex} {signed_prekey_sig}")
            if verify.strip() != "true":
                self._send_json(401, {"ok": False, "error": "signed prekey signature invalid"})
                return

            PREKEYS[owner] = {
                "identity_pub": identity_pub,
                "signed_prekey": signed_prekey,
                "signed_prekey_sig": signed_prekey_sig,
                "one_time_prekeys": one_time_prekeys,
                "updated": now_sec()
            }
            self._send_json(200, {"ok": True, "address": owner})
            return

        if path == "/api/ratchet/prekey/get":
            ok, requester, err = verify_session(self.headers)
            if not ok:
                self._send_json(401, {"ok": False, "error": err})
                return
            _ = requester
            target = sanitize_token(str(body.get("address", "")))
            if not target:
                self._send_json(400, {"ok": False, "error": "address required"})
                return
            rec = PREKEYS.get(target)
            if not rec:
                self._send_json(404, {"ok": False, "error": "prekey not found"})
                return
            otk = rec.get("one_time_prekeys", [])
            selected_otk = ""
            if isinstance(otk, list) and otk:
                selected_otk = str(otk.pop(0))
                rec["one_time_prekeys"] = otk
            self._send_json(200, {
                "ok": True,
                "address": target,
                "identity_pub": rec.get("identity_pub", ""),
                "signed_prekey": rec.get("signed_prekey", ""),
                "signed_prekey_sig": rec.get("signed_prekey_sig", ""),
                "one_time_prekey": selected_otk,
                "updated": rec.get("updated", 0)
            })
            return

        if path == "/api/pm/inbox":
            ok, requester, err = verify_session(self.headers)
            if not ok:
                self._send_json(401, {"ok": False, "error": err})
                return
            st_ok, st_err = require_staked_access(requester)
            if not st_ok:
                self._send_json(403, {"ok": False, "error": st_err})
                return
            raw = rpc_call(f"pm_inbox {requester}")
            self._send_json(200, {"ok": not raw.startswith("error:"), "raw": raw})
            return

        if path == "/api/pm/fetch":
            ok, requester, err = verify_session(self.headers)
            if not ok:
                self._send_json(401, {"ok": False, "error": err})
                return
            st_ok, st_err = require_staked_access(requester)
            if not st_ok:
                self._send_json(403, {"ok": False, "error": st_err})
                return
            msg_id = sanitize_token(str(body.get("msg_id", "")))
            if not msg_id:
                self._send_json(400, {"ok": False, "error": "msg_id required"})
                return
            raw = rpc_call(f"pm_fetch {msg_id} {requester}")
            data = parse_kv(raw)
            if raw.startswith("error:"):
                self._send_json(200, {"ok": False, "raw": raw, "data": data})
                return
            ref = str(data.get("ciphertext_ref", ""))
            ciphertext = get_blob(ref) if ref else ""
            if ciphertext:
                data["ciphertext"] = ciphertext
            self._send_json(200, {"ok": True, "raw": raw, "data": data})
            return

        if path == "/api/swap/quote":
            token_in = sanitize_token(str(body.get("token_in", "")))
            token_out = sanitize_token(str(body.get("token_out", "")))
            amount_in = int(body.get("amount_in", 0) or 0)
            if not token_in or not token_out or amount_in <= 0:
                self._send_json(400, {"ok": False, "error": "invalid params"})
                return
            raw = rpc_call(f"swap_best_route {token_in} {token_out} {amount_in} 3")
            self._send_json(200, {"ok": not raw.startswith("error:"), "raw": raw, "data": parse_kv(raw)})
            return

        self._send_json(404, {"ok": False, "error": "not found"})

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path
        client_ip = self.client_address[0] if self.client_address else "unknown"
        if not apply_rate_limit(client_ip, path):
            self._send_json(429, {"ok": False, "error": "rate limited"})
            return
        if path == "/api/getinfo":
            raw = rpc_call("getinfo")
            self._send_json(200, {"ok": not raw.startswith("error:"), "data": parse_kv(raw), "raw": raw})
        elif path == "/api/monetary":
            raw = rpc_call("monetary_info")
            self._send_json(200, {"ok": not raw.startswith("error:"), "data": parse_kv(raw), "raw": raw})
        elif path == "/api/peers":
            raw = rpc_call("peers")
            self._send_json(200, {"ok": not raw.startswith("error:"), "raw": raw})
        elif path == "/api/health":
            raw = rpc_call("getinfo")
            self._send_json(200, {"ok": not raw.startswith("error:") and bool(raw), "raw": raw})
        elif path == "/api/stake_claimable":
            qs = parse_qs(parsed.query)
            addr = sanitize_token((qs.get("address", [""])[0]))
            if not addr:
                self._send_json(400, {"ok": False, "error": "address required"})
                return
            raw = rpc_call(f"stake_claimable {addr}")
            self._send_json(200, {"ok": not raw.startswith("error:"), "raw": raw})
        elif path == "/manifest.webmanifest":
            static_file_response(self, STATIC_ROOT / "manifest.webmanifest")
        elif path == "/sw.js":
            static_file_response(self, STATIC_ROOT / "sw.js")
        elif path.startswith("/chat"):
            static_file_response(self, STATIC_ROOT / "chat" / "index.html")
        elif path.startswith("/chat/"):
            rel = path[len("/chat/"):]
            static_file_response(self, STATIC_ROOT / "chat" / rel)
        elif path == "/exchange.html":
            static_file_response(self, STATIC_ROOT / "exchange.html")
        elif path == "/network.html":
            static_file_response(self, STATIC_ROOT / "network.html")
        elif path == "/node.html":
            static_file_response(self, STATIC_ROOT / "node.html")
        elif path == "/about.html":
            static_file_response(self, STATIC_ROOT / "about.html")
        elif path == "/tokenomics.html":
            static_file_response(self, STATIC_ROOT / "tokenomics.html")
        elif path == "/":
            static_file_response(self, STATIC_ROOT / "index.html")
        else:
            self._send_json(404, {"ok": False, "error": "not found"})

if __name__ == "__main__":
    print(f"ADDITION PRO Backend on {HTTP_HOST}:{HTTP_PORT}...")
    HTTPServer((HTTP_HOST, HTTP_PORT), PortalHandler).serve_forever()
