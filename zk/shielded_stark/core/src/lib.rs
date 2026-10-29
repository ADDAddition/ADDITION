//! ADDITION shielded-pool statement (EXPERIMENTAL, NOT AUDITED).
//!
//! Shared by the RISC Zero guest (prover side, executed inside the zkVM) and the
//! host. All hashing is SHA-256 with domain-separation prefixes; the C++ node
//! re-implements the *public* parts (merkle tree, commitments for wallets) and
//! must stay byte-for-byte compatible with this file.
#![no_std]
extern crate alloc;
use alloc::vec::Vec;
use sha2::{Digest, Sha256};

pub const TREE_DEPTH: usize = 32;
pub const MAX_IN: usize = 4;
pub const MAX_OUT: usize = 4;
pub type H32 = [u8; 32];

fn h(parts: &[&[u8]]) -> H32 {
    let mut s = Sha256::new();
    for p in parts { s.update(p); }
    s.finalize().into()
}
pub fn owner_tag(nk: &H32) -> H32 { h(&[b"ADD/owner/v1", nk]) }
pub fn note_commitment(value: u64, owner: &H32, rho: &H32, r: &H32) -> H32 {
    h(&[b"ADD/cm/v1", &value.to_le_bytes(), owner, rho, r])
}
pub fn nullifier(nk: &H32, rho: &H32) -> H32 { h(&[b"ADD/nf/v1", nk, rho]) }
pub fn merkle_node(l: &H32, r: &H32) -> H32 { h(&[b"ADD/mk/v1", l, r]) }
/// rho for output j is derived (not chosen) so a sender cannot create two notes
/// with the same nullifier ("faerie gold"): bind is unique per tx, nfs unique on-chain.
pub fn output_rho(bind: &H32, nfs: &[H32], j: u32) -> H32 {
    let mut s = Sha256::new();
    s.update(b"ADD/rho/v1"); s.update(bind);
    for n in nfs { s.update(n); }
    s.update(j.to_le_bytes());
    s.finalize().into()
}
pub fn merkle_root_from_path(leaf: &H32, pos: u64, sib: &[H32; TREE_DEPTH]) -> H32 {
    let mut cur = *leaf;
    for (d, s) in sib.iter().enumerate() {
        cur = if (pos >> d) & 1 == 0 { merkle_node(&cur, s) } else { merkle_node(s, &cur) };
    }
    cur
}

pub struct Reader<'a> { b: &'a [u8], i: usize }
impl<'a> Reader<'a> {
    pub fn new(b: &'a [u8]) -> Self { Self { b, i: 0 } }
    pub fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.i + n > self.b.len() { return None; }
        let s = &self.b[self.i..self.i + n]; self.i += n; Some(s)
    }
    pub fn h32(&mut self) -> Option<H32> { self.take(32).map(|s| s.try_into().unwrap()) }
    pub fn u64(&mut self) -> Option<u64> { self.take(8).map(|s| u64::from_le_bytes(s.try_into().unwrap())) }
    pub fn u32(&mut self) -> Option<u32> { self.take(4).map(|s| u32::from_le_bytes(s.try_into().unwrap())) }
    pub fn done(&self) -> bool { self.i == self.b.len() }
}

/// Witness (private) layout, little endian:
///   bind[32] vpub_in u64 vpub_out u64
///   n_in u32 { nk[32] value u64 rho[32] r[32] pos u64 siblings[32*32] }*
///   n_out u32 { value u64 owner[32] r[32] }*
/// Journal (public) layout:
///   "ADDSHLD1" bind[32] anchor[32] vpub_in u64 vpub_out u64
///   n_in u32 nf[32]* n_out u32 cm[32]*
pub fn evaluate(witness: &[u8]) -> Result<Vec<u8>, &'static str> {
    let mut rd = Reader::new(witness);
    let e = "malformed witness";
    let bind = rd.h32().ok_or(e)?;
    let vpub_in = rd.u64().ok_or(e)?;
    let vpub_out = rd.u64().ok_or(e)?;
    let n_in = rd.u32().ok_or(e)? as usize;
    if n_in > MAX_IN { return Err("too many inputs"); }
    let mut anchor: H32 = [0u8; 32];
    let mut nfs: Vec<H32> = Vec::with_capacity(n_in);
    let mut sum_in: u128 = vpub_in as u128;
    for k in 0..n_in {
        let nk = rd.h32().ok_or(e)?;
        let value = rd.u64().ok_or(e)?;
        let rho = rd.h32().ok_or(e)?;
        let r = rd.h32().ok_or(e)?;
        let pos = rd.u64().ok_or(e)?;
        if pos >> TREE_DEPTH != 0 { return Err("position out of range"); }
        let mut sib = [[0u8; 32]; TREE_DEPTH];
        for s in sib.iter_mut() { *s = rd.h32().ok_or(e)?; }
        let cm = note_commitment(value, &owner_tag(&nk), &rho, &r);
        let root = merkle_root_from_path(&cm, pos, &sib);
        if k == 0 { anchor = root; } else if root != anchor { return Err("inputs use different anchors"); }
        let nf = nullifier(&nk, &rho);
        if nfs.contains(&nf) { return Err("duplicate nullifier"); }
        nfs.push(nf);
        sum_in += value as u128;
    }
    let n_out = rd.u32().ok_or(e)? as usize;
    if n_out > MAX_OUT { return Err("too many outputs"); }
    let mut cms: Vec<H32> = Vec::with_capacity(n_out);
    let mut sum_out: u128 = vpub_out as u128;
    for j in 0..n_out {
        let value = rd.u64().ok_or(e)?;   // u64 typing == 64-bit range proof
        let owner = rd.h32().ok_or(e)?;
        let r = rd.h32().ok_or(e)?;
        let rho = output_rho(&bind, &nfs, j as u32);
        cms.push(note_commitment(value, &owner, &rho, &r));
        sum_out += value as u128;
    }
    if !rd.done() { return Err("trailing witness bytes"); }
    if sum_in != sum_out { return Err("value not conserved"); }
    let mut j = Vec::with_capacity(8 + 32 * 2 + 16 + 8 + 32 * (n_in + n_out));
    j.extend_from_slice(b"ADDSHLD1");
    j.extend_from_slice(&bind);
    j.extend_from_slice(&anchor);
    j.extend_from_slice(&vpub_in.to_le_bytes());
    j.extend_from_slice(&vpub_out.to_le_bytes());
    j.extend_from_slice(&(n_in as u32).to_le_bytes());
    for n in &nfs { j.extend_from_slice(n); }
    j.extend_from_slice(&(n_out as u32).to_le_bytes());
    for c in &cms { j.extend_from_slice(c); }
    Ok(j)
}
