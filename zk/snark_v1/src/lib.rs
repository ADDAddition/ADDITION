//! Lab Groth16 SNARK (arkworks / BN254) for a Poseidon commitment+nullifier opening.
//!
//! Statement (mint-shaped, schema-analogous to C_cm / C_nf):
//!   public:  amount, cm, nf
//!   witness: trapdoor r
//!   cm == Poseidon([TAG_CM, amount, r])
//!   nf == Poseidon([TAG_NF, cm, r])
//!
//! This is a REAL zkSNARK prove+verify path. It is NOT production SHA3-512 privacy:
//! the circuit hash is Poseidon over BN254 Fr (documented gap). Live product claim
//! stays `opening_not_zk`; `zk_circuit_status` stays `not_proven` for the SHA3 path.
//!
//! Setup: Groth16 requires a **trusted setup** (circuit-specific CRS). Tests run a
//! fresh setup in-process; production ceremony is out of scope.
//!
//! PR #82 toy Fiat–Shamir Schnorr is a different path and is NOT this SNARK.

#![allow(clippy::too_many_arguments)]

use ark_bn254::{Bn254, Fr};
use ark_crypto_primitives::sponge::constraints::CryptographicSpongeVar;
use ark_crypto_primitives::sponge::poseidon::constraints::PoseidonSpongeVar;
use ark_crypto_primitives::sponge::poseidon::{find_poseidon_ark_and_mds, PoseidonConfig, PoseidonSponge};
use ark_crypto_primitives::sponge::{CryptographicSponge, FieldBasedCryptographicSponge};
use ark_ff::{BigInteger, PrimeField};
use ark_groth16::{Groth16, PreparedVerifyingKey, Proof, ProvingKey, VerifyingKey};
use ark_r1cs_std::alloc::AllocVar;
use ark_r1cs_std::eq::EqGadget;
use ark_r1cs_std::fields::fp::FpVar;
use ark_r1cs_std::fields::FieldVar;
use ark_relations::r1cs::{ConstraintSynthesizer, ConstraintSystemRef, SynthesisError};
use ark_serialize::{CanonicalDeserialize, CanonicalSerialize};
use ark_snark::SNARK;
use ark_std::rand::{rngs::StdRng, SeedableRng};
use ark_std::vec::Vec;
use std::os::raw::{c_char, c_int, c_uchar};
use std::ptr;
use std::slice;

/// Domain tags (field elements) — analogous to "cm|v1|" / "nf|v1|" prefixes.
const TAG_CM: u64 = 0x636d_7631; // "cmv1"
const TAG_NF: u64 = 0x6e66_7631; // "nfv1"

const ERR_OK: c_int = 0;
const ERR_NULL: c_int = 1;
const ERR_BAD_ARG: c_int = 2;
const ERR_SETUP: c_int = 3;
const ERR_PROVE: c_int = 4;
const ERR_VERIFY: c_int = 5;
const ERR_DISABLED: c_int = 7;

fn poseidon_config() -> PoseidonConfig<Fr> {
    // Rate-2 Poseidon over BN254 Fr (arkworks Grain-LFSR parameter search).
    let full_rounds = 8;
    let partial_rounds = 57;
    let alpha = 5_u64;
    let rate = 2;
    let capacity = 1;
    let (ark, mds) = find_poseidon_ark_and_mds::<Fr>(
        Fr::MODULUS_BIT_SIZE as u64,
        rate,
        full_rounds as u64,
        partial_rounds as u64,
        0,
    );
    PoseidonConfig {
        full_rounds,
        partial_rounds,
        alpha,
        ark,
        mds,
        rate,
        capacity,
    }
}

fn poseidon_hash3(tag: Fr, a: Fr, b: Fr) -> Fr {
    let cfg = poseidon_config();
    let mut sponge = PoseidonSponge::<Fr>::new(&cfg);
    sponge.absorb(&tag);
    sponge.absorb(&a);
    sponge.absorb(&b);
    sponge.squeeze_native_field_elements(1)[0]
}

fn fr_from_u64(v: u64) -> Fr {
    Fr::from(v)
}

fn fr_from_bytes32(bytes: &[u8; 32]) -> Fr {
    // Little-endian reduction into Fr (lab encoding; documented).
    Fr::from_le_bytes_mod_order(bytes)
}

fn fr_to_be_bytes32(f: &Fr) -> [u8; 32] {
    let mut out = [0u8; 32];
    let bytes = f.into_bigint().to_bytes_be();
    let start = 32usize.saturating_sub(bytes.len());
    out[start..].copy_from_slice(&bytes);
    out
}

/// Circuit: knowledge of trapdoor for Poseidon cm/nf opening (lab hash).
#[derive(Clone)]
struct OpeningCircuit {
    amount: Option<Fr>,
    trapdoor: Option<Fr>,
    commitment: Option<Fr>,
    nullifier: Option<Fr>,
}

impl ConstraintSynthesizer<Fr> for OpeningCircuit {
    fn generate_constraints(self, cs: ConstraintSystemRef<Fr>) -> Result<(), SynthesisError> {
        let amount = FpVar::new_input(cs.clone(), || {
            self.amount.ok_or(SynthesisError::AssignmentMissing)
        })?;
        let cm = FpVar::new_input(cs.clone(), || {
            self.commitment.ok_or(SynthesisError::AssignmentMissing)
        })?;
        let nf = FpVar::new_input(cs.clone(), || {
            self.nullifier.ok_or(SynthesisError::AssignmentMissing)
        })?;
        let trapdoor = FpVar::new_witness(cs.clone(), || {
            self.trapdoor.ok_or(SynthesisError::AssignmentMissing)
        })?;

        let cfg = poseidon_config();
        let tag_cm = FpVar::constant(fr_from_u64(TAG_CM));
        let tag_nf = FpVar::constant(fr_from_u64(TAG_NF));

        // cm == Poseidon(TAG_CM, amount, trapdoor)
        let mut sponge_cm = PoseidonSpongeVar::new(cs.clone(), &cfg);
        sponge_cm.absorb(&tag_cm)?;
        sponge_cm.absorb(&amount)?;
        sponge_cm.absorb(&trapdoor)?;
        let cm_calc = sponge_cm.squeeze_field_elements(1)?.remove(0);
        cm_calc.enforce_equal(&cm)?;

        // nf == Poseidon(TAG_NF, cm, trapdoor)
        let mut sponge_nf = PoseidonSpongeVar::new(cs, &cfg);
        sponge_nf.absorb(&tag_nf)?;
        sponge_nf.absorb(&cm)?;
        sponge_nf.absorb(&trapdoor)?;
        let nf_calc = sponge_nf.squeeze_field_elements(1)?.remove(0);
        nf_calc.enforce_equal(&nf)?;

        Ok(())
    }
}

fn compute_cm_nf(amount: Fr, trapdoor: Fr) -> (Fr, Fr) {
    let cm = poseidon_hash3(fr_from_u64(TAG_CM), amount, trapdoor);
    let nf = poseidon_hash3(fr_from_u64(TAG_NF), cm, trapdoor);
    (cm, nf)
}

struct KeypairBytes {
    pk: Vec<u8>,
    vk: Vec<u8>,
}

fn setup_keys(rng: &mut StdRng) -> Result<KeypairBytes, SynthesisError> {
    let circuit = OpeningCircuit {
        amount: None,
        trapdoor: None,
        commitment: None,
        nullifier: None,
    };
    let (pk, vk) = Groth16::<Bn254>::circuit_specific_setup(circuit, rng)?;
    let mut pk_bytes = Vec::new();
    pk.serialize_compressed(&mut pk_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    let mut vk_bytes = Vec::new();
    vk.serialize_compressed(&mut vk_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    Ok(KeypairBytes {
        pk: pk_bytes,
        vk: vk_bytes,
    })
}

fn prove_opening(
    pk_bytes: &[u8],
    amount: Fr,
    trapdoor: Fr,
    rng: &mut StdRng,
) -> Result<(Vec<u8>, Fr, Fr), SynthesisError> {
    let (cm, nf) = compute_cm_nf(amount, trapdoor);
    let pk = ProvingKey::<Bn254>::deserialize_compressed(pk_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    let circuit = OpeningCircuit {
        amount: Some(amount),
        trapdoor: Some(trapdoor),
        commitment: Some(cm),
        nullifier: Some(nf),
    };
    let proof = Groth16::<Bn254>::prove(&pk, circuit, rng)?;
    let mut proof_bytes = Vec::new();
    proof
        .serialize_compressed(&mut proof_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    Ok((proof_bytes, cm, nf))
}

fn verify_opening(vk_bytes: &[u8], proof_bytes: &[u8], amount: Fr, cm: Fr, nf: Fr) -> Result<bool, SynthesisError> {
    let vk = VerifyingKey::<Bn254>::deserialize_compressed(vk_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    let proof = Proof::<Bn254>::deserialize_compressed(proof_bytes)
        .map_err(|_| SynthesisError::Unsatisfiable)?;
    let pvk = PreparedVerifyingKey::from(vk);
    let public = [amount, cm, nf];
    Groth16::<Bn254>::verify_with_processed_vk(&pvk, &public, &proof)
        .map_err(|_| SynthesisError::Unsatisfiable)
}

fn snark_enabled() -> bool {
    // Opt-in lab path. Default off → C++ fail-closed wrappers refuse.
    match std::env::var("ADDITION_ZK_SNARK_V1") {
        Ok(v) => matches!(v.as_str(), "1" | "true" | "TRUE" | "yes" | "YES"),
        Err(_) => false,
    }
}

fn write_buf(dst: *mut c_uchar, dst_len: usize, src: &[u8], written: *mut usize) -> c_int {
    if written.is_null() {
        return ERR_NULL;
    }
    unsafe {
        *written = src.len();
    }
    if dst.is_null() {
        return ERR_OK; // size query
    }
    if dst_len < src.len() {
        return ERR_BAD_ARG;
    }
    unsafe {
        ptr::copy_nonoverlapping(src.as_ptr(), dst, src.len());
    }
    ERR_OK
}

fn read_slice<'a>(ptr: *const c_uchar, len: usize) -> Option<&'a [u8]> {
    if ptr.is_null() || len == 0 {
        return None;
    }
    Some(unsafe { slice::from_raw_parts(ptr, len) })
}

/// Returns 1 if env flag enables the lab SNARK path.
#[no_mangle]
pub extern "C" fn addition_snark_v1_enabled() -> c_int {
    if snark_enabled() {
        1
    } else {
        0
    }
}

/// Backend id C string (static).
#[no_mangle]
pub extern "C" fn addition_snark_v1_backend_id() -> *const c_char {
    b"arkworks_groth16_bn254_poseidon_opening\0".as_ptr() as *const c_char
}

/// Proving system name C string (static).
#[no_mangle]
pub extern "C" fn addition_snark_v1_system_name() -> *const c_char {
    b"Groth16 (arkworks) over BN254\0".as_ptr() as *const c_char
}

/// Hash label for the circuit commitment (gap vs SHA3-512).
#[no_mangle]
pub extern "C" fn addition_snark_v1_circuit_hash_label() -> *const c_char {
    b"Poseidon(BN254-Fr) lab hash - NOT production SHA3-512\0".as_ptr() as *const c_char
}

/// Trusted-setup honesty label.
#[no_mangle]
pub extern "C" fn addition_snark_v1_setup_label() -> *const c_char {
    b"trusted_setup_circuit_specific (Groth16 CRS; lab in-process setup)\0".as_ptr() as *const c_char
}

/// Circuit-specific Groth16 setup. Fails closed if ADDITION_ZK_SNARK_V1 is not set.
/// Pass null buffers to query required sizes via out lengths.
#[no_mangle]
pub extern "C" fn addition_snark_v1_setup(
    pk_out: *mut c_uchar,
    pk_cap: usize,
    pk_len: *mut usize,
    vk_out: *mut c_uchar,
    vk_cap: usize,
    vk_len: *mut usize,
) -> c_int {
    if !snark_enabled() {
        return ERR_DISABLED;
    }
    if pk_len.is_null() || vk_len.is_null() {
        return ERR_NULL;
    }
    let mut rng = StdRng::from_entropy();
    let keys = match setup_keys(&mut rng) {
        Ok(k) => k,
        Err(_) => return ERR_SETUP,
    };
    let r1 = write_buf(pk_out, pk_cap, &keys.pk, pk_len);
    if r1 != ERR_OK {
        return r1;
    }
    write_buf(vk_out, vk_cap, &keys.vk, vk_len)
}

/// Prove Poseidon opening. Writes proof + public cm/nf as 32-byte big-endian field elements.
#[no_mangle]
pub extern "C" fn addition_snark_v1_prove(
    pk: *const c_uchar,
    pk_len: usize,
    amount: u64,
    trapdoor32: *const c_uchar,
    proof_out: *mut c_uchar,
    proof_cap: usize,
    proof_len: *mut usize,
    cm_out32: *mut c_uchar,
    nf_out32: *mut c_uchar,
) -> c_int {
    if !snark_enabled() {
        return ERR_DISABLED;
    }
    if proof_len.is_null() || trapdoor32.is_null() || cm_out32.is_null() || nf_out32.is_null() {
        return ERR_NULL;
    }
    let pk_bytes = match read_slice(pk, pk_len) {
        Some(s) => s,
        None => return ERR_BAD_ARG,
    };
    let mut td = [0u8; 32];
    unsafe {
        ptr::copy_nonoverlapping(trapdoor32, td.as_mut_ptr(), 32);
    }
    let amount_fr = fr_from_u64(amount);
    let trapdoor_fr = fr_from_bytes32(&td);
    let mut rng = StdRng::from_entropy();
    let (proof, cm, nf) = match prove_opening(pk_bytes, amount_fr, trapdoor_fr, &mut rng) {
        Ok(v) => v,
        Err(_) => return ERR_PROVE,
    };
    let cm_b = fr_to_be_bytes32(&cm);
    let nf_b = fr_to_be_bytes32(&nf);
    unsafe {
        ptr::copy_nonoverlapping(cm_b.as_ptr(), cm_out32, 32);
        ptr::copy_nonoverlapping(nf_b.as_ptr(), nf_out32, 32);
    }
    write_buf(proof_out, proof_cap, &proof, proof_len)
}

/// Verify Groth16 proof for public (amount, cm, nf). Returns 0 and *ok_out=1 on success.
#[no_mangle]
pub extern "C" fn addition_snark_v1_verify(
    vk: *const c_uchar,
    vk_len: usize,
    proof: *const c_uchar,
    proof_len: usize,
    amount: u64,
    cm32: *const c_uchar,
    nf32: *const c_uchar,
    ok_out: *mut c_int,
) -> c_int {
    if ok_out.is_null() {
        return ERR_NULL;
    }
    unsafe {
        *ok_out = 0;
    }
    if !snark_enabled() {
        return ERR_DISABLED;
    }
    if cm32.is_null() || nf32.is_null() {
        return ERR_NULL;
    }
    let vk_bytes = match read_slice(vk, vk_len) {
        Some(s) => s,
        None => return ERR_BAD_ARG,
    };
    let proof_bytes = match read_slice(proof, proof_len) {
        Some(s) => s,
        None => return ERR_BAD_ARG,
    };
    let mut cm_b = [0u8; 32];
    let mut nf_b = [0u8; 32];
    unsafe {
        ptr::copy_nonoverlapping(cm32, cm_b.as_mut_ptr(), 32);
        ptr::copy_nonoverlapping(nf32, nf_b.as_mut_ptr(), 32);
    }
    let amount_fr = fr_from_u64(amount);
    let cm = Fr::from_be_bytes_mod_order(&cm_b);
    let nf = Fr::from_be_bytes_mod_order(&nf_b);
    match verify_opening(vk_bytes, proof_bytes, amount_fr, cm, nf) {
        Ok(true) => {
            unsafe {
                *ok_out = 1;
            }
            ERR_OK
        }
        Ok(false) => ERR_OK, // verified false → ok_out stays 0
        Err(_) => ERR_VERIFY,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn prove_verify_ok_and_tamper_fails() {
        std::env::set_var("ADDITION_ZK_SNARK_V1", "1");
        let mut rng = StdRng::seed_from_u64(42);
        let keys = setup_keys(&mut rng).expect("setup");
        let amount = fr_from_u64(7);
        let mut td = [0u8; 32];
        td[31] = 0x2a;
        let trapdoor = fr_from_bytes32(&td);
        let (proof, cm, nf) = prove_opening(&keys.pk, amount, trapdoor, &mut rng).expect("prove");
        assert!(verify_opening(&keys.vk, &proof, amount, cm, nf).unwrap());

        // Wrong public amount must fail.
        assert!(!verify_opening(&keys.vk, &proof, fr_from_u64(8), cm, nf).unwrap());

        // Tampered proof must fail (or deserialize/verify error → treat as fail).
        let mut bad = proof.clone();
        if let Some(b) = bad.last_mut() {
            *b ^= 0xff;
        }
        let tampered_ok = verify_opening(&keys.vk, &bad, amount, cm, nf).unwrap_or(false);
        assert!(!tampered_ok);
    }
}
