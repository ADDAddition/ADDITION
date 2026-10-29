#pragma once
/* C ABI for zk/shielded_stark (RISC Zero STARK, succinct receipts). EXPERIMENTAL, NOT AUDITED. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { ADDSH_OK = 0, ADDSH_ERR_ARG = 1, ADDSH_ERR_STATEMENT = 2, ADDSH_ERR_PROVE = 3, ADDSH_ERR_DECODE = 4,
       ADDSH_ERR_VERIFY = 5, ADDSH_ERR_JOURNAL = 6, ADDSH_ERR_NOT_SUCCINCT = 7 };
void addition_shielded_image_id(uint8_t out[32]);
void addition_shielded_free(uint8_t* p, size_t len);
int addition_shielded_eval(const uint8_t* w, size_t wl, uint8_t** journal, size_t* jl);
int addition_shielded_prove(const uint8_t* w, size_t wl, uint8_t** receipt, size_t* rl, uint8_t** journal, size_t* jl);
int addition_shielded_verify(const uint8_t* receipt, size_t rl, const uint8_t* expected_journal, size_t ejl);
#ifdef __cplusplus
}
#endif
