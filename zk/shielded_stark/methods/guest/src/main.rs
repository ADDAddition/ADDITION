//! zkVM guest: proves the shielded spend/output statement in shielded-core::evaluate.
use risc0_zkvm::guest::env;
fn main() {
    let mut len_buf = [0u8; 4];
    env::read_slice(&mut len_buf);
    let len = u32::from_le_bytes(len_buf) as usize;
    let mut w = vec![0u8; len];
    env::read_slice(&mut w);
    let journal = shielded_core::evaluate(&w).expect("statement does not hold");
    env::commit_slice(&journal);
}
