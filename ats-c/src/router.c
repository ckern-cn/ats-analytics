/*
 * CodeIgniter's request pipeline, minus the framework:
 *
 *	path = URI::filterPath(raw path)
 *	uri  = urldecode(path)			Router::handle()
 *	key  = trim(uri, "/ ")			Router::checkRoutes()
 *	match ^routeKey$ against key		PCRE, /u, route order
 *	throttle (global) -> AuthFilter (route) -> controller
 *
 * No match is a PageNotFoundException, i.e. 404 without any filter running;
 * the PHP app disables auto-routing, and its route table has no HEAD entry,
 * so every method other than GET/POST is a 404 as well.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <kore/kore.h>
#include <kore/http.h>

#include "handlers.h"
#include "router.h"
#include "throttle.h"
#include "util.h"

#define ARRAY_LEN(a)	(sizeof(a) / sizeof((a)[0]))

struct route {
	const char	*key;		/* CI4 route pattern */
	int		 post;		/* 0 = GET route, 1 = POST route */
	int		 auth;		/* AuthFilter::class */
	int	       (*handler)(struct http_request *);
};

static const struct route	routes[] = {
	{ "wru/(.*)",		0, 0, ats_h_wru },
	{ "rv.js/(.*)",		0, 0, ats_h_rv },
	{ "pv/(.*)",		0, 0, ats_h_pv },
	{ "dv",			0, 1, ats_h_dv },
	{ "dv/(.*)",		0, 1, ats_h_dv },
	{ "create",		0, 1, ats_h_create },
	{ "do_create",		1, 1, ats_h_do_create },
	{ "edit/(.*)",		0, 1, ats_h_edit },
	{ "do_update",		1, 1, ats_h_do_update },
	{ "do_delete",		1, 1, ats_h_do_delete },
	{ "export/(.*)",		0, 1, ats_h_export },
	{ "login",		0, 0, ats_h_login },
	{ "do_login",		1, 0, ats_h_do_login },
	{ "logout",		1, 0, ats_h_logout },
	{ "lang/(.*)",		0, 0, ats_h_lang },
};

static int	utf8_valid(const unsigned char *, size_t);
static size_t	decoded_len(const char *, size_t);
static int	rest_ok(const char *, size_t);
static int	literal_match(const char *, const char *, size_t);
static int	prefix_match(const char *, size_t, const char *, size_t);
static int	rvjs_match(const char *, size_t);
static int	route_match(const struct route *, const char *, size_t);

/*
 * preg_match('#^...$#u') fails on malformed UTF-8, which turns the request
 * into a 404. Reject overlong forms, surrogates and > U+10FFFF like PCRE.
 */
static int
utf8_valid(const unsigned char *s, size_t len)
{
	size_t		 i;
	unsigned int	 cp;
	int		 n, k;

	i = 0;
	while (i < len) {
		if (s[i] < 0x80) {
			i++;
			continue;
		}

		if (s[i] >= 0xC2 && s[i] <= 0xDF) {
			n = 1;
			cp = s[i] & 0x1F;
		} else if (s[i] >= 0xE0 && s[i] <= 0xEF) {
			n = 2;
			cp = s[i] & 0x0F;
		} else if (s[i] >= 0xF0 && s[i] <= 0xF4) {
			n = 3;
			cp = s[i] & 0x07;
		} else {
			return (0);
		}

		i++;
		for (k = 0; k < n; k++, i++) {
			if (i >= len || (s[i] & 0xC0) != 0x80)
				return (0);
			cp = (cp << 6) | (s[i] & 0x3F);
		}

		if (n == 2 && (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)))
			return (0);
		if (n == 3 && (cp < 0x10000 || cp > 0x10FFFF))
			return (0);
	}

	return (1);
}

/* Length of uri_decode(path): every valid %HH triple shrinks by two bytes. */
static size_t
decoded_len(const char *s, size_t len)
{
	size_t	i, n;

	n = 0;
	for (i = 0; i < len; i++) {
		if (s[i] == '%' && len - i >= 3 &&
		    isxdigit((unsigned char)s[i + 1]) &&
		    isxdigit((unsigned char)s[i + 2]))
			i += 2;
		n++;
	}

	return (n);
}

/* (.*) cannot cross '\n', but PCRE's '$' matches before one final newline. */
static int
rest_ok(const char *s, size_t len)
{
	size_t	i;

	for (i = 0; i < len; i++) {
		if (s[i] == '\n')
			return (i + 1 == len);
	}

	return (1);
}

/* '#^key$#' for keys without metacharacters. */
static int
literal_match(const char *key, const char *s, size_t len)
{
	size_t	klen;

	klen = strlen(key);

	if (len == klen && memcmp(key, s, klen) == 0)
		return (1);
	if (len == klen + 1 && memcmp(key, s, klen) == 0 &&
	    s[klen] == '\n')
		return (1);

	return (0);
}

/* '#^prefix(.*)$#': wru/, dv/, pv/. */
static int
prefix_match(const char *prefix, size_t plen, const char *s, size_t len)
{
	if (len < plen || memcmp(prefix, s, plen) != 0)
		return (0);

	return (rest_ok(s + plen, len - plen));
}

/*
 * '#^rv.js/(.*)$#': the '.' is not escaped in the PHP source, so it matches
 * any single byte except '\n' (which is why /rvXjs/foo hits AppCtrl too).
 */
static int
rvjs_match(const char *s, size_t len)
{
	if (len < 6)
		return (0);
	if (s[0] != 'r' || s[1] != 'v' || s[3] != 'j' || s[4] != 's' ||
	    s[5] != '/')
		return (0);
	if (s[2] == '\n')
		return (0);

	return (rest_ok(s + 6, len - 6));
}

static int
route_match(const struct route *r, const char *s, size_t len)
{
	if (!strcmp(r->key, "wru/(.*)"))
		return (prefix_match("wru/", 4, s, len));
	if (!strcmp(r->key, "dv/(.*)"))
		return (prefix_match("dv/", 3, s, len));
	if (!strcmp(r->key, "pv/(.*)"))
		return (prefix_match("pv/", 3, s, len));
	if (!strcmp(r->key, "rv.js/(.*)"))
		return (rvjs_match(s, len));

	{
		size_t klen = strlen(r->key);

		/* Escape hatch for the generic "<name>/(.*)" patterns. */
		if (klen > 4 && !strcmp(r->key + klen - 4, "(.*)"))
			return (prefix_match(r->key, klen - 4, s, len));
	}

	return (literal_match(r->key, s, len));
}

int
ats_dispatch(struct http_request *req)
{
	struct ats_sb	 fp;
	const struct route *r;
	char		*dec, *key, *ip;
	size_t		 len, klen;
	int		 i, is_post;

	ats_sb_init(&fp);
	ats_filter_path(&fp, req->path);

	dec = kore_malloc(fp.len + 1);
	if (ats_url_decode(fp.p, fp.len, dec, fp.len + 1) == -1) {
		ats_sb_free(&fp);
		kore_free(dec);
		ats_send_status(req, 404);
		return (KORE_RESULT_OK);
	}
	len = decoded_len(fp.p, fp.len);
	ats_sb_free(&fp);

	if (!utf8_valid((const unsigned char *)dec, len)) {
		kore_free(dec);
		ats_send_status(req, 404);
		return (KORE_RESULT_OK);
	}

	/* $uri === '/' ? $uri : trim($uri, '/ ') */
	key = dec;
	klen = len;
	if (!(klen == 1 && key[0] == '/')) {
		while (klen > 0 && (key[0] == '/' || key[0] == ' ')) {
			key++;
			klen--;
		}
		while (klen > 0 &&
		    (key[klen - 1] == '/' || key[klen - 1] == ' '))
			klen--;
	}

	/* nginx's loopback-only probe; not part of the CI4 surface. */
	if (req->method == HTTP_METHOD_GET && klen == 8 &&
	    memcmp(key, "__health", 8) == 0) {
		kore_free(dec);
		return (ats_h_health(req));
	}

	/* A small, friendly entry point for a personal deployment. */
	if (req->method == HTTP_METHOD_GET &&
	    (klen == 0 || (klen == 1 && key[0] == '/'))) {
		kore_free(dec);
		ats_send_redirect(req, 302, "/dv");
		return (KORE_RESULT_OK);
	}

	is_post = (req->method == HTTP_METHOD_POST) ? 1 : 0;
	if (req->method != HTTP_METHOD_GET && !is_post) {
		kore_free(dec);
		ats_send_status(req, 404);
		return (KORE_RESULT_OK);
	}

	r = NULL;
	for (i = 0; i < (int)ARRAY_LEN(routes); i++) {
		if (routes[i].post != is_post)
			continue;
		if (route_match(&routes[i], key, klen)) {
			r = &routes[i];
			break;
		}
	}

	if (r == NULL) {
		kore_free(dec);
		ats_send_status(req, 404);
		return (KORE_RESULT_OK);
	}

	kore_free(dec);

	ip = kore_malloc(64);
	ats_client_ip(req, ip, 64);
	i = ats_throttle_allow("global", ip, 600, 60);
	kore_free(ip);

	if (!i) {
		ats_send_status(req, 429);
		return (KORE_RESULT_OK);
	}

	if (r->auth && ats_require_auth(req) == -1)
		return (KORE_RESULT_OK);

	return (r->handler(req));
}