# SHA3 opening notes (not a ZK circuit)

This file used to describe an external `ADDITION_ZK_VERIFY_CMD` wrapper. That path is
**not** a Groth16 / SNARK / Bulletproofs / STARK verifier. This repository does not
ship a ZK circuit.

The privacy path that exists is **SHA3-512 commitment + nullifier opening**:

- `privacy_note_prepare`
- `privacy_mint_open`
- `privacy_spend_open`

`getinfo` reports `privacy_mode=sha3_opening`, `privacy_ok=true`, and
`privacy_verifier=sha3_opening`.

`privacy_mint_zk` / `privacy_spend_zk` remain an ML-DSA-87 signature wrap of a
mint/spend string. A garbage proof returns `error:`. They are not a circuit.

`tools/zk_verify_wrapper.py` exits with an error if invoked. Do not set
`ADDITION_ZK_VERIFY_CMD` and do not advertise a ZK backend.
