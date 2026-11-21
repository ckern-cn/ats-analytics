#ifndef ATS_HMAC_H_INCLUDED
#define ATS_HMAC_H_INCLUDED

#include <sys/types.h>

#include <kore/kore.h>

void	ats_hmac_sha256(const u_int8_t *key, size_t key_len,
	    const u_int8_t *msg, size_t msg_len, u_int8_t *out);

#endif
