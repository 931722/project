#include "mldsa_helper.h"
#include <stdlib.h>
#include <string.h>

int mldsa_init(MLDSA_Ctx *ctx) {
    ctx->sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_44);
    if (!ctx->sig) return -1;

    ctx->public_key = malloc(ctx->sig->length_public_key);
    ctx->secret_key = malloc(ctx->sig->length_secret_key);

    if (!ctx->public_key || !ctx->secret_key) return -1;

    if (OQS_SIG_keypair(ctx->sig, ctx->public_key, ctx->secret_key) != OQS_SUCCESS)
        return -1;

    return 0;
}

void mldsa_free(MLDSA_Ctx *ctx) {
    if (ctx->sig) OQS_SIG_free(ctx->sig);
    if (ctx->public_key) free(ctx->public_key);
    if (ctx->secret_key) free(ctx->secret_key);
}

const uint8_t *mldsa_get_pk(MLDSA_Ctx *ctx, size_t *len) {
    *len = ctx->sig->length_public_key;
    return ctx->public_key;
}

const uint8_t *mldsa_get_sk(MLDSA_Ctx *ctx, size_t *len) {
    *len = ctx->sig->length_secret_key;
    return ctx->secret_key;
}

int mldsa_sign(MLDSA_Ctx *ctx, const uint8_t *msg, size_t msg_len,
               uint8_t *sig, size_t *sig_len) {
    return OQS_SIG_sign(ctx->sig, sig, sig_len, msg, msg_len, ctx->secret_key);
}

int mldsa_verify(MLDSA_Ctx *ctx, const uint8_t *pk,
                 const uint8_t *msg, size_t msg_len,
                 const uint8_t *sig, size_t sig_len) {
    return OQS_SIG_verify(ctx->sig, msg, msg_len, sig, sig_len, pk);
}
