#!/usr/bin/env python3
"""Leftover wrapper name. This tree has no ZK / SNARK / bulletproofs circuit."""

import sys


def main() -> int:
    print(
        "ERROR: not a ZK/SNARK/bulletproofs circuit; privacy is SHA3 opening "
        "(privacy_mint_open / privacy_spend_open)"
    )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
