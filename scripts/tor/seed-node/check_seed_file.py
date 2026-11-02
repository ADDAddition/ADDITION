#!/usr/bin/env python3
"""Validate a seeds-mainnet.txt (same rules as the daemon: v3, checksum, port)."""
import base64, hashlib, sys

def ok(entry: str) -> bool:
    host, sep, port = entry.rpartition(":")
    if not sep or not port.isdigit() or not 0 < int(port) < 65536: return False
    if len(host) != 62 or not host.endswith(".onion"): return False
    try: raw = base64.b32decode(host[:56].upper())
    except Exception: return False
    pk, ck, ver = raw[:32], raw[32:34], raw[34:]
    return ver == b"\x03" and hashlib.sha3_256(b".onion checksum" + pk + ver).digest()[:2] == ck

path = sys.argv[1] if len(sys.argv) > 1 else "seeds-mainnet.txt"
bad = n = 0
for i, line in enumerate(open(path), 1):
    e = line.split("#", 1)[0].strip()
    if not e: continue
    n += 1
    if not ok(e): bad += 1; print(f"{path}:{i}: invalid seed '{e}'")
print(f"{n} seed(s), {bad} invalid"); sys.exit(1 if bad else 0)
