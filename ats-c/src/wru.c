#include <string.h>

#include <kore/kore.h>
#include <kore/sha2.h>

#include "ats.h"
#include "hmac.h"
#include "wru.h"

#define WRU_TOKEN_BYTES	16
#define WRU_TOKEN_HEX	(WRU_TOKEN_BYTES * 2)

static void
hex16(const unsigned char *in, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int		  i;

	for (i = 0; i < WRU_TOKEN_BYTES; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0x0f];
	}
	out[WRU_TOKEN_HEX] = '\0';
}

void
ats_wru_sign(const char *id, char out[33])
{
	unsigned char	mac[SHA256_DIGEST_LENGTH];
	const char	*key;

	out[0] = '\0';
	if (id == NULL)
		return;

	key = ats_jwt_key();
	if (key == NULL || *key == '\0')
		return;

	ats_hmac_sha256((const u_int8_t *)key, strlen(key),
	    (const u_int8_t *)id, strlen(id), mac);
	hex16(mac, out);
}

int
ats_wru_verify(const char *id, const char *token)
{
	char		 want[33];
	unsigned char	 diff;
	size_t		 i;

	if (id == NULL || token == NULL || strlen(token) != WRU_TOKEN_HEX)
		return (-1);

	ats_wru_sign(id, want);
	if (want[0] == '\0')
		return (-1);

	diff = 0;
	for (i = 0; i < WRU_TOKEN_HEX; i++)
		diff |= (unsigned char)want[i] ^ (unsigned char)token[i];

	return (diff == 0 ? 0 : -1);
}
