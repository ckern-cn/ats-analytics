/*
 * MD5 (RFC 1321). Kept local because the only consumer is the
 * unique_visitors column, whose value must equal PHP md5() byte for byte.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "md5.h"

struct md5_ctx {
	uint32_t	a, b, c, d;
	uint64_t	len;
	uint8_t		buf[64];
	size_t		have;
};

static uint32_t
rotl(uint32_t x, int s)
{
	return ((x << s) | (x >> (32 - s)));
}

static void
md5_block(struct md5_ctx *ctx, const uint8_t *p)
{
	static const uint32_t	k[64] = {
		0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
		0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
		0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
		0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
		0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
		0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
		0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
		0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
		0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
		0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
		0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
		0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
		0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
		0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
		0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
		0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
	};
	static const int	s[64] = {
		7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
		5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
		4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
		6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
	};
	uint32_t	m[16], a, b, c, d, f, tmp;
	int		i, g;

	for (i = 0; i < 16; i++)
		m[i] = (uint32_t)p[i * 4] |
		    ((uint32_t)p[i * 4 + 1] << 8) |
		    ((uint32_t)p[i * 4 + 2] << 16) |
		    ((uint32_t)p[i * 4 + 3] << 24);

	a = ctx->a;
	b = ctx->b;
	c = ctx->c;
	d = ctx->d;

	for (i = 0; i < 64; i++) {
		if (i < 16) {
			f = (b & c) | (~b & d);
			g = i;
		} else if (i < 32) {
			f = (d & b) | (~d & c);
			g = (5 * i + 1) & 15;
		} else if (i < 48) {
			f = b ^ c ^ d;
			g = (3 * i + 5) & 15;
		} else {
			f = c ^ (b | ~d);
			g = (7 * i) & 15;
		}

		tmp = d;
		d = c;
		c = b;
		b = b + rotl(a + f + k[i] + m[g], s[i]);
		a = tmp;
	}

	ctx->a += a;
	ctx->b += b;
	ctx->c += c;
	ctx->d += d;
}

static void
md5_update(struct md5_ctx *ctx, const void *data, size_t len)
{
	const uint8_t	*p = data;
	size_t		n;

	ctx->len += len;

	if (ctx->have != 0) {
		n = 64 - ctx->have;
		if (n > len)
			n = len;
		memcpy(ctx->buf + ctx->have, p, n);
		ctx->have += n;
		p += n;
		len -= n;
		if (ctx->have == 64) {
			md5_block(ctx, ctx->buf);
			ctx->have = 0;
		}
	}

	while (len >= 64) {
		md5_block(ctx, p);
		p += 64;
		len -= 64;
	}

	if (len > 0) {
		memcpy(ctx->buf, p, len);
		ctx->have = len;
	}
}

void
ats_md5_hex(const void *data, size_t len, char out[33])
{
	struct md5_ctx	ctx;
	uint8_t		digest[16], pad[128];
	uint64_t	bits;
	size_t		padlen, i;

	ctx.a = 0x67452301;
	ctx.b = 0xefcdab89;
	ctx.c = 0x98badcfe;
	ctx.d = 0x10325476;
	ctx.len = 0;
	ctx.have = 0;

	md5_update(&ctx, data, len);

	bits = ctx.len * 8;
	memset(pad, 0, sizeof(pad));
	pad[0] = 0x80;

	padlen = (ctx.have < 56) ? (56 - ctx.have) : (120 - ctx.have);
	md5_update(&ctx, pad, padlen);

	for (i = 0; i < 8; i++)
		pad[i] = (uint8_t)((bits >> (8 * i)) & 0xff);
	md5_update(&ctx, pad, 8);

	digest[0] = (uint8_t)(ctx.a);
	digest[1] = (uint8_t)(ctx.a >> 8);
	digest[2] = (uint8_t)(ctx.a >> 16);
	digest[3] = (uint8_t)(ctx.a >> 24);
	digest[4] = (uint8_t)(ctx.b);
	digest[5] = (uint8_t)(ctx.b >> 8);
	digest[6] = (uint8_t)(ctx.b >> 16);
	digest[7] = (uint8_t)(ctx.b >> 24);
	digest[8] = (uint8_t)(ctx.c);
	digest[9] = (uint8_t)(ctx.c >> 8);
	digest[10] = (uint8_t)(ctx.c >> 16);
	digest[11] = (uint8_t)(ctx.c >> 24);
	digest[12] = (uint8_t)(ctx.d);
	digest[13] = (uint8_t)(ctx.d >> 8);
	digest[14] = (uint8_t)(ctx.d >> 16);
	digest[15] = (uint8_t)(ctx.d >> 24);

	for (i = 0; i < 16; i++)
		(void)sprintf(&out[i * 2], "%02x", digest[i]);
	out[32] = '\0';
}
