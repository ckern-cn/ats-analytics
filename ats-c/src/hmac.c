#include <string.h>

#include <kore/kore.h>
#include <kore/sha2.h>

#include "hmac.h"

#define HMAC_BLOCK_LEN	SHA256_BLOCK_LENGTH

void
ats_hmac_sha256(const u_int8_t *key, size_t key_len, const u_int8_t *msg,
    size_t msg_len, u_int8_t *out)
{
	u_int8_t	kblock[HMAC_BLOCK_LEN], work[HMAC_BLOCK_LEN];
	SHA2_CTX	ctx;
	size_t		i;

	if (key_len > HMAC_BLOCK_LEN) {
		SHA256Init(&ctx);
		SHA256Update(&ctx, key, key_len);
		SHA256Final(kblock, &ctx);
		memset(kblock + SHA256_DIGEST_LENGTH, 0,
		    HMAC_BLOCK_LEN - SHA256_DIGEST_LENGTH);
	} else {
		memcpy(kblock, key, key_len);
		memset(kblock + key_len, 0, HMAC_BLOCK_LEN - key_len);
	}

	for (i = 0; i < HMAC_BLOCK_LEN; i++)
		work[i] = kblock[i] ^ 0x36;

	SHA256Init(&ctx);
	SHA256Update(&ctx, work, HMAC_BLOCK_LEN);
	SHA256Update(&ctx, msg, msg_len);
	SHA256Final(out, &ctx);

	for (i = 0; i < HMAC_BLOCK_LEN; i++)
		work[i] = kblock[i] ^ 0x5c;

	SHA256Init(&ctx);
	SHA256Update(&ctx, work, HMAC_BLOCK_LEN);
	SHA256Update(&ctx, out, SHA256_DIGEST_LENGTH);
	SHA256Final(out, &ctx);

	memset(kblock, 0, sizeof(kblock));
	memset(work, 0, sizeof(work));
}
