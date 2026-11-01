//! C ABI for the ADDITION shielded pool proof system (EXPERIMENTAL, NOT AUDITED).
//!
//! Proof system: RISC Zero zkVM STARK (FRI, hash-based; no pairings, no trusted
//! setup), *succinct* receipts only. The Groth16/BN254 wrapper is deliberately
//! NOT used because it is not post-quantum. Fake/dev-mode receipts are rejected
//! (crate built with `disable-dev-mode` and the receipt variant is checked).
use risc0_zkvm::{InnerReceipt, Receipt};
use shielded_methods::{SHIELDED_SPEND_ELF, SHIELDED_SPEND_ID};
use std::slice;

pub const OK: i32 = 0;
pub const ERR_ARG: i32 = 1;
pub const ERR_STATEMENT: i32 = 2;
pub const ERR_PROVE: i32 = 3;
pub const ERR_DECODE: i32 = 4;
pub const ERR_VERIFY: i32 = 5;
pub const ERR_JOURNAL: i32 = 6;
pub const ERR_NOT_SUCCINCT: i32 = 7;

fn give(v: Vec<u8>, out: *mut *mut u8, out_len: *mut usize) {
    let mut b = v.into_boxed_slice();
    unsafe { *out_len = b.len(); *out = b.as_mut_ptr(); }
    std::mem::forget(b);
}

#[no_mangle]
pub extern "C" fn addition_shielded_image_id(out: *mut u8) {
    if out.is_null() { return; }
    let mut bytes = [0u8; 32];
    for (i, w) in SHIELDED_SPEND_ID.iter().enumerate() { bytes[i * 4..i * 4 + 4].copy_from_slice(&w.to_le_bytes()); }
    unsafe { std::ptr::copy_nonoverlapping(bytes.as_ptr(), out, 32) };
}

#[no_mangle]
pub extern "C" fn addition_shielded_free(p: *mut u8, len: usize) {
    if !p.is_null() { unsafe { drop(Box::from_raw(std::ptr::slice_from_raw_parts_mut(p, len))) } }
}

/// Native (non-ZK) evaluation of the statement; returns the journal the proof would commit.
#[no_mangle]
pub extern "C" fn addition_shielded_eval(w: *const u8, wl: usize, j: *mut *mut u8, jl: *mut usize) -> i32 {
    if w.is_null() || j.is_null() || jl.is_null() { return ERR_ARG; }
    let wit = unsafe { slice::from_raw_parts(w, wl) };
    match shielded_core::evaluate(wit) { Ok(v) => { give(v, j, jl); OK } Err(_) => ERR_STATEMENT }
}

#[cfg(feature = "prove")]
#[no_mangle]
pub extern "C" fn addition_shielded_prove(w: *const u8, wl: usize, r: *mut *mut u8, rl: *mut usize,
                                          j: *mut *mut u8, jl: *mut usize) -> i32 {
    use risc0_zkvm::{default_prover, ExecutorEnv, ProverOpts};
    if w.is_null() || r.is_null() || rl.is_null() || j.is_null() || jl.is_null() { return ERR_ARG; }
    let wit = unsafe { slice::from_raw_parts(w, wl) };
    if shielded_core::evaluate(wit).is_err() { return ERR_STATEMENT; }
    let len = (wit.len() as u32).to_le_bytes();
    let env = match ExecutorEnv::builder().write_slice(&len).write_slice(wit).build() { Ok(e) => e, Err(_) => return ERR_PROVE };
    let info = match default_prover().prove_with_opts(env, SHIELDED_SPEND_ELF, &ProverOpts::succinct()) { Ok(i) => i, Err(_) => return ERR_PROVE };
    let journal = info.receipt.journal.bytes.clone();
    let bytes = match bincode::serialize(&info.receipt) { Ok(b) => b, Err(_) => return ERR_PROVE };
    give(bytes, r, rl);
    give(journal, j, jl);
    OK
}

/// Consensus verifier. Returns OK only for a succinct STARK receipt of the pinned
/// image whose journal equals `expected_journal` byte-for-byte.
#[no_mangle]
pub extern "C" fn addition_shielded_verify(r: *const u8, rl: usize, ej: *const u8, ejl: usize) -> i32 {
    if r.is_null() || ej.is_null() || rl == 0 || rl > 4 * 1024 * 1024 { return ERR_ARG; }
    let rb = unsafe { slice::from_raw_parts(r, rl) };
    let expected = unsafe { slice::from_raw_parts(ej, ejl) };
    let receipt: Receipt = match bincode::deserialize(rb) { Ok(x) => x, Err(_) => return ERR_DECODE };
    if !matches!(receipt.inner, InnerReceipt::Succinct(_)) { return ERR_NOT_SUCCINCT; }
    if receipt.journal.bytes.as_slice() != expected { return ERR_JOURNAL; }
    match receipt.verify(SHIELDED_SPEND_ID) { Ok(()) => OK, Err(_) => ERR_VERIFY }
}
