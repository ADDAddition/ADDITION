# addition_snark_v1

Lab Groth16 zkSNARK (arkworks / BN254) for a Poseidon commitment+nullifier
opening. Linked into ADDITION via CMake when `ADDITION_BUILD_ZK_SNARK=ON`.

Enable at runtime with `ADDITION_ZK_SNARK_V1=1`. Default is fail-closed.

This is **not** production SHA3-512 privacy. See `docs/ZK_SNARK_V1.md`.
