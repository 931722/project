#ifndef MLDSA_HELPER_H
#define MLDSA_HELPER_H

#include <oqs/oqs.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    OQS_SIG *sig;
    uint8_t *public_key;
    uint8_t *secret_key;
} MLDSA_Ctx;

int mldsa_init(MLDSA_Ctx *ctx);
void mldsa_free(MLDSA_Ctx *ctx);

const uint8_t *mldsa_get_pk(MLDSA_Ctx *ctx, size_t *len);
const uint8_t *mldsa_get_sk(MLDSA_Ctx *ctx, size_t *len);

int mldsa_sign(MLDSA_Ctx *ctx, const uint8_t *msg, size_t msg_len,
               uint8_t *sig, size_t *sig_len);

int mldsa_verify(MLDSA_Ctx *ctx, const uint8_t *pk,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *sig, size_t sig_len);

#endif
