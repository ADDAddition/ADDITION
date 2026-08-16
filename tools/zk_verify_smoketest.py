#!/usr/bin/env python3
import subprocess
import sys
from pathlib import Path


def run(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, (p.stdout + p.stderr).strip()


def main() -> int:
    wrapper = Path(__file__).resolve().parent / "zk_verify_wrapper.py"
    py = sys.executable

    rc, out = run([py, str(wrapper), "mint|alice|10|aa|bb", "00", "00"])
    if rc == 0:
        print("FAIL: wrapper must not claim a ZK verify success")
        return 1
    if "SHA3 opening" not in out and "sha3_opening" not in out.lower():
        print("FAIL: wrapper must say SHA3 opening, got:", out)
        return 2
    if "SNARK" not in out and "bulletproofs" not in out.lower():
        print("FAIL: wrapper must reject ZK/SNARK/bulletproofs claims, got:", out)
        return 3

    print("OK: wrapper fails closed as SHA3 opening, not a ZK circuit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
