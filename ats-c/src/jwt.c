/*
 * HS256 tokens for the admin session.
 *
 *	header : {"typ":"JWT","alg":"HS256"}
 *	claims : iss, aud, iat, nbf, exp, user_name
 *
 * The key comes from ATS_JWT_KEY or data/jwt.key. Verification checks the
 * signature, alg, nbf/iat/exp and that user_name matches the configured
 * admin, so changing the admin user invalidates old cookies.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <kore/kore.h>
#include <kore/sha2.h>

#include "ats.h"
#include "cJSON.h"
#include "hmac.h"
#include "jwt.h"

static const char	jwt_header[] = "{\"typ\":\"JWT\",\"alg\":\"HS256\"}";

static char *
b64url_raw(const void *data, size_t len)
{
	char	*out;

	if (kore_base64url_encode(data, len, &out, KORE_BASE64_RAW)
	    != KORE_RESULT_OK)
		return (NULL);

	return (out);
}

static int
b64url_val(int c)
{
	if (c >= 'A' && c <= 'Z')
		return (c - 'A');
	if (c >= 'a' && c <= 'z')
		return (c - 'a' + 26);
	if (c >= '0' && c <= '9')
		return (c - '0' + 52);
	if (c == '-')
		return (62);
	if (c == '_')
		return (63);

	return (-1);
}

static int
b64url_decode(const char *in, u_int8_t **out, size_t *out_len)
{
	u_int8_t	*res;
	u_int32_t	acc;
	size_t		i, len, o;
	int		bits, v;

	len = strlen(in);
	if (len % 4 == 1)
		return (KORE_RESULT_ERROR);

	if ((res = malloc(len / 4 * 3 + 3)) == NULL)
		fatal("out of memory");

	acc = 0;
	bits = 0;
	o = 0;

	for (i = 0; i < len; i++) {
		if ((v = b64url_val((unsigned char)in[i])) == -1) {
			free(res);
			return (KORE_RESULT_ERROR);
		}

		acc = (acc << 6) | (u_int32_t)v;
		bits += 6;

		if (bits >= 8) {
			bits -= 8;
			res[o++] = (u_int8_t)((acc >> bits) & 0xff);
		}
	}

	*out = res;
	*out_len = o;

	return (KORE_RESULT_OK);
}

/* Takes ownership of claims. */
static char *
sign_claims(const char *key, cJSON *claims)
{
	char		*payload_txt, *b64h, *b64p, *b64s;
	u_int8_t	 mac[SHA256_DIGEST_LENGTH];
	struct kore_buf	 buf;

	if (claims == NULL)
		return (NULL);

	payload_txt = cJSON_PrintUnformatted(claims);
	cJSON_Delete(claims);
	if (payload_txt == NULL)
		return (NULL);

	b64h = b64url_raw(jwt_header, strlen(jwt_header));
	b64p = b64url_raw(payload_txt, strlen(payload_txt));
	cJSON_free(payload_txt);

	if (b64h == NULL || b64p == NULL) {
		if (b64h != NULL)
			kore_free(b64h);
		if (b64p != NULL)
			kore_free(b64p);
		return (NULL);
	}

	kore_buf_init(&buf, strlen(b64h) + strlen(b64p) + 16);
	kore_buf_append(&buf, b64h, strlen(b64h));
	kore_buf_append(&buf, ".", 1);
	kore_buf_append(&buf, b64p, strlen(b64p));

	ats_hmac_sha256((const u_int8_t *)key, strlen(key), buf.data,
	    buf.offset, mac);

	kore_free(b64h);
	kore_free(b64p);

	if ((b64s = b64url_raw(mac, sizeof(mac))) == NULL) {
		kore_free(buf.data);
		return (NULL);
	}

	kore_buf_append(&buf, ".", 1);
	kore_buf_append(&buf, b64s, strlen(b64s));
	kore_free(b64s);

	return (kore_buf_stringify(&buf, NULL));
}

char *
ats_jwt_sign(const char *user_name, long long iat, long long exp)
{
	cJSON	*claims;
	const char *key;

	if ((claims = cJSON_CreateObject()) == NULL)
		return (NULL);

	key = ats_jwt_key();
	if (key == NULL || *key == '\0') {
		cJSON_Delete(claims);
		return (NULL);
	}

	cJSON_AddStringToObject(claims, "iss", "ats");
	cJSON_AddStringToObject(claims, "aud", "ats-admin");
	cJSON_AddNumberToObject(claims, "iat", (double)iat);
	cJSON_AddNumberToObject(claims, "nbf", (double)iat);
	cJSON_AddNumberToObject(claims, "exp", (double)exp);
	cJSON_AddStringToObject(claims, "user_name", user_name);

	return (sign_claims(key, claims));
}

int
ats_jwt_verify(const char *token)
{
	const char		*p1, *p2;
	char			*seg;
	u_int8_t		mac[SHA256_DIGEST_LENGTH], *want, *raw;
	volatile u_int8_t	diff;
	cJSON			*head, *claims, *item;
	size_t			raw_len, want_len;
	long long		now;
	int			r;
	size_t			i;

	if (token == NULL || *token == '\0')
		return (-1);

	if ((p1 = strchr(token, '.')) == NULL || p1 == token)
		return (-1);
	if ((p2 = strchr(p1 + 1, '.')) == NULL || p2 == p1 + 1)
		return (-1);
	if (*(p2 + 1) == '\0' || strchr(p2 + 1, '.') != NULL)
		return (-1);

	if ((seg = strndup(token, (size_t)(p1 - token))) == NULL)
		fatal("out of memory");

	if (b64url_decode(seg, &raw, &raw_len) == KORE_RESULT_ERROR) {
		free(seg);
		return (-1);
	}
	free(seg);

	head = cJSON_ParseWithLength((const char *)raw, raw_len);
	free(raw);
	if (head == NULL)
		return (-1);

	item = cJSON_GetObjectItemCaseSensitive(head, "alg");
	if (item == NULL || !cJSON_IsString(item) ||
	    strcmp(item->valuestring, "HS256") != 0) {
		cJSON_Delete(head);
		return (-1);
	}
	cJSON_Delete(head);

	{
		const char *key = ats_jwt_key();
		if (key == NULL || *key == '\0')
			return (-1);
		ats_hmac_sha256((const u_int8_t *)key, strlen(key),
		    (const u_int8_t *)token, (size_t)(p2 - token), mac);
	}

	want = NULL;
	r = b64url_decode(p2 + 1, &want, &want_len);
	if (r == KORE_RESULT_ERROR || want == NULL ||
	    want_len != sizeof(mac)) {
		free(want);
		return (-1);
	}

	diff = 0;
	for (i = 0; i < sizeof(mac); i++)
		diff |= mac[i] ^ want[i];
	free(want);

	if (diff != 0)
		return (-1);

	if ((seg = strndup(p1 + 1, (size_t)(p2 - p1 - 1))) == NULL)
		fatal("out of memory");

	r = b64url_decode(seg, &raw, &raw_len);
	free(seg);
	if (r == KORE_RESULT_ERROR)
		return (-1);

	claims = cJSON_ParseWithLength((const char *)raw, raw_len);
	free(raw);
	if (claims == NULL || !cJSON_IsObject(claims)) {
		if (claims != NULL)
			cJSON_Delete(claims);
		return (-1);
	}

	now = (long long)time(NULL);

	if ((item = cJSON_GetObjectItemCaseSensitive(claims, "nbf")) != NULL &&
	    (!cJSON_IsNumber(item) || (long long)item->valuedouble > now)) {
		cJSON_Delete(claims);
		return (-1);
	}

	if ((item = cJSON_GetObjectItemCaseSensitive(claims, "iat")) != NULL &&
	    (!cJSON_IsNumber(item) || (long long)item->valuedouble > now)) {
		cJSON_Delete(claims);
		return (-1);
	}

	if ((item = cJSON_GetObjectItemCaseSensitive(claims, "exp")) == NULL ||
	    !cJSON_IsNumber(item) || now >= (long long)item->valuedouble) {
		cJSON_Delete(claims);
		return (-1);
	}

	if ((item = cJSON_GetObjectItemCaseSensitive(claims,
	    "user_name")) == NULL || !cJSON_IsString(item) ||
	    strcmp(item->valuestring, ats_admin_user()) != 0) {
		cJSON_Delete(claims);
		return (-1);
	}

	cJSON_Delete(claims);

	return (0);
}
