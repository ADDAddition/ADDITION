#pragma once

/* C ABI for addition_snark_v1 (arkworks Groth16 / BN254 / Poseidon opening).
 * Lab prove+verify path. NOT production SHA3-512 privacy.
 * Enable with ADDITION_ZK_SNARK_V1=1; otherwise all mutating ops return ERR_DISABLED.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ADDITION_SNARK_V1_OK = 0,
    ADDITION_SNARK_V1_ERR_NULL = 1,
    ADDITION_SNARK_V1_ERR_BAD_ARG = 2,
    ADDITION_SNARK_V1_ERR_SETUP = 3,
    ADDITION_SNARK_V1_ERR_PROVE = 4,
    ADDITION_SNARK_V1_ERR_VERIFY = 5,
    ADDITION_SNARK_V1_ERR_SERIALIZE = 6,
    ADDITION_SNARK_V1_ERR_DISABLED = 7
};

int addition_snark_v1_enabled(void);
const char* addition_snark_v1_backend_id(void);
const char* addition_snark_v1_system_name(void);
const char* addition_snark_v1_circuit_hash_label(void);
const char* addition_snark_v1_setup_label(void);

int addition_snark_v1_setup(uint8_t* pk_out,
                            size_t pk_cap,
                            size_t* pk_len,
                            uint8_t* vk_out,
                            size_t vk_cap,
                            size_t* vk_len);

int addition_snark_v1_prove(const uint8_t* pk,
                            size_t pk_len,
                            uint64_t amount,
                            const uint8_t* trapdoor32,
                            uint8_t* proof_out,
                            size_t proof_cap,
                            size_t* proof_len,
                            uint8_t* cm_out32,
                            uint8_t* nf_out32);

int addition_snark_v1_verify(const uint8_t* vk,
                             size_t vk_len,
                             const uint8_t* proof,
                             size_t proof_len,
                             uint64_t amount,
                             const uint8_t* cm32,
                             const uint8_t* nf32,
                             int* ok_out);

#ifdef __cplusplus
}
#endif
