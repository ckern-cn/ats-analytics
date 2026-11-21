#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/random.h>
#endif

#include <kore/kore.h>
#include <kore/sha2.h>

#include "ats.h"
#include "hmac.h"
#include "util.h"

static void
sb_grow(struct ats_sb *sb, size_t need)
{
	size_t	cap;
	char	*p;

	if (sb->len + need + 1 <= sb->cap)
		return;

	cap = sb->cap == 0 ? 512 : sb->cap;
	while (cap < sb->len + need + 1)
		cap *= 2;

	if ((p = realloc(sb->p, cap)) == NULL)
		fatal("out of memory");
	sb->p = p;
	sb->cap = cap;
}

void
ats_sb_init(struct ats_sb *sb)
{
	memset(sb, 0, sizeof(*sb));
}

void
ats_sb_free(struct ats_sb *sb)
{
	free(sb->p);
	memset(sb, 0, sizeof(*sb));
}

void
ats_sb_add(struct ats_sb *sb, const void *data, size_t len)
{
	if (len == 0)
		return;

	sb_grow(sb, len);
	memcpy(sb->p + sb->len, data, len);
	sb->len += len;
	sb->p[sb->len] = '\0';
}

void
ats_sb_adds(struct ats_sb *sb, const char *s)
{
	if (s != NULL)
		ats_sb_add(sb, s, strlen(s));
}

void
ats_sb_addf(struct ats_sb *sb, const char *fmt, ...)
{
	va_list	ap;
	char	*tmp;
	int	n;

	va_start(ap, fmt);
	n = vasprintf(&tmp, fmt, ap);
	va_end(ap);

	if (n < 0)
		fatal("out of memory");

	ats_sb_add(sb, tmp, (size_t)n);
	free(tmp);
}

const char *
ats_sb_cstr(struct ats_sb *sb)
{
	if (sb->p == NULL)
		return ("");

	return (sb->p);
}

static void
sb_add_entity(struct ats_sb *sb, char c)
{
	switch (c) {
	case '&':
		ats_sb_adds(sb, "&amp;");
		break;
	case '<':
		ats_sb_adds(sb, "&lt;");
		break;
	case '>':
		ats_sb_adds(sb, "&gt;");
		break;
	case '"':
		ats_sb_adds(sb, "&quot;");
		break;
	case '\'':
		ats_sb_adds(sb, "&#039;");
		break;
	default:
		ats_sb_add(sb, &c, 1);
		break;
	}
}

/* htmlspecialchars(ENT_QUOTES|ENT_HTML401) for the five special characters. */
void
ats_sb_html(struct ats_sb *sb, const char *s)
{
	if (s == NULL)
		return;

	for (; *s != '\0'; s++)
		sb_add_entity(sb, *s);
}

/*
 * Escape for a single-quoted JavaScript string literal. The views carry
 * request input into inline <script> blocks; the PHP source did not escape
 * there (stored/reflected XSS), so this is a deliberate deviation.
 */
void
ats_sb_js(struct ats_sb *sb, const char *s)
{
	char	esc[7];

	if (s == NULL)
		return;

	for (; *s != '\0'; s++) {
		switch (*s) {
		case '\\':
			ats_sb_adds(sb, "\\\\");
			break;
		case '\'':
			ats_sb_adds(sb, "\\'");
			break;
		case '"':
			ats_sb_adds(sb, "\\\"");
			break;
		case '\n':
			ats_sb_adds(sb, "\\n");
			break;
		case '\r':
			ats_sb_adds(sb, "\\r");
			break;
		case '<':
			ats_sb_adds(sb, "\\x3C");
			break;
		case '>':
			ats_sb_adds(sb, "\\x3E");
			break;
		case '&':
			ats_sb_adds(sb, "\\x26");
			break;
		default:
			if ((unsigned char)*s < 0x20) {
				(void)snprintf(esc, sizeof(esc), "\\u%04x",
				    (unsigned char)*s);
				ats_sb_adds(sb, esc);
			} else {
				ats_sb_add(sb, s, 1);
			}
			break;
		}
	}
}

static int
trim_char(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v');
}

void
ats_trim(char *s)
{
	char	*start, *end;
	size_t	len;

	len = strlen(s);
	start = s;
	while (*start != '\0' && trim_char((unsigned char)*start))
		start++;

	end = s + len;
	while (end > start && trim_char((unsigned char)end[-1]))
		end--;

	memmove(s, start, (size_t)(end - start));
	s[end - start] = '\0';
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9')
		return (c - '0');
	if (c >= 'a' && c <= 'f')
		return (c - 'a' + 10);
	if (c >= 'A' && c <= 'F')
		return (c - 'A' + 10);

	return (-1);
}

int
ats_url_decode(const char *src, size_t len, char *out, size_t out_len)
{
	size_t	i, o = 0;
	int	hi, lo;

	for (i = 0; i < len; i++) {
		if (o + 1 >= out_len)
			return (-1);

		if (src[i] == '+') {
			out[o++] = ' ';
		} else if (src[i] == '%' && len - i >= 3 &&
		    (hi = hexval((unsigned char)src[i + 1])) != -1 &&
		    (lo = hexval((unsigned char)src[i + 2])) != -1) {
			out[o++] = (char)((hi << 4) | lo);
			i += 2;
		} else {
			/* PHP urldecode() keeps malformed escapes as-is. */
			out[o++] = src[i];
		}
	}

	out[o] = '\0';

	return (0);
}

/*
 * Scan a key=value&... string. Return 1 and fill out when the key is found;
 * later duplicates overwrite earlier ones, like PHP parse_str(). A key with
 * no '=' yields the empty string.
 */
static int
pairs_get(const char *qs, size_t qlen, const char *key,
    char *out, size_t out_len)
{
	const char	*p, *end, *amp, *eq;
	size_t		klen, kseg, vlen;
	int		found = 0;
	char		tmp[16384];

	if (qs == NULL || (klen = strlen(key)) == 0)
		return (0);

	end = qs + qlen;

	for (p = qs; p < end; ) {
		amp = memchr(p, '&', (size_t)(end - p));
		if (amp == NULL)
			amp = end;

		eq = memchr(p, '=', (size_t)(amp - p));
		kseg = (eq == NULL) ? (size_t)(amp - p) : (size_t)(eq - p);

		if (kseg == klen && !strncmp(p, key, klen)) {
			if (eq == NULL) {
				if (out_len > 0)
					out[0] = '\0';
				found = 1;
			} else {
				vlen = (size_t)(amp - eq - 1);
				if (vlen >= sizeof(tmp))
					vlen = sizeof(tmp) - 1;
				if (ats_url_decode(eq + 1, vlen, tmp,
				    sizeof(tmp)) == 0) {
					(void)snprintf(out, out_len, "%s", tmp);
					found = 1;
				}
			}
		}

		if (amp == end)
			break;
		p = amp + 1;
	}

	return (found);
}

int
ats_query_get(struct http_request *req, const char *key,
    char *out, size_t out_len)
{
	if (req->query_string == NULL) {
		if (out_len > 0)
			out[0] = '\0';
		return (0);
	}

	return (pairs_get(req->query_string, strlen(req->query_string),
	    key, out, out_len));
}

int
ats_form_get(const char *body, size_t body_len, const char *key,
    char *out, size_t out_len)
{
	if (body == NULL) {
		if (out_len > 0)
			out[0] = '\0';
		return (0);
	}

	return (pairs_get(body, body_len, key, out, out_len));
}

void
ats_php_urlencode(struct ats_sb *sb, const char *s)
{
	char	esc[4];

	for (; s != NULL && *s != '\0'; s++) {
		if (isalnum((unsigned char)*s) || *s == '-' || *s == '_' ||
		    *s == '.') {
			ats_sb_add(sb, s, 1);
		} else if (*s == ' ') {
			ats_sb_adds(sb, "+");
		} else {
			(void)snprintf(esc, sizeof(esc), "%%%02X",
			    (unsigned char)*s);
			ats_sb_adds(sb, esc);
		}
	}
}

/* PHP urldecode() leaves malformed percent escapes untouched. */
static size_t
url_decode_lenient(const char *src, size_t len, char *out)
{
	size_t	i, o = 0;
	int	hi, lo;

	for (i = 0; i < len; i++) {
		if (src[i] == '+') {
			out[o++] = ' ';
		} else if (src[i] == '%' && len - i >= 3 &&
		    (hi = hexval((unsigned char)src[i + 1])) != -1 &&
		    (lo = hexval((unsigned char)src[i + 2])) != -1) {
			out[o++] = (char)((hi << 4) | lo);
			i += 2;
		} else {
			out[o++] = src[i];
		}
	}
	out[o] = '\0';

	return (o);
}

/*
 * Port of CodeIgniter\HTTP\URI::removeDotSegments(). This is deliberately
 * not RFC 3986 section 5.2.4: CI4 drops empty segments and trims '/ ' from
 * both ends, and only preserves a leading/trailing slash from the input.
 */
static void
remove_dot_segments(const char *path, char *out, size_t out_len)
{
	const char	*p, *q, *seg;
	size_t		 len, n, i, nseg = 0, o = 0, start, stop;
	int		 leading, trailing;
	struct {
		const char	*p;
		size_t		 n;
	} *segs;

	len = strlen(path);

	if (len == 0 || (len == 1 && path[0] == '/')) {
		if (out_len < len + 1)
			return;
		memcpy(out, path, len + 1);
		return;
	}

	leading = (path[0] == '/');
	trailing = (path[len - 1] == '/');

	if ((segs = malloc((len + 1) * sizeof(*segs))) == NULL)
		fatal("out of memory");

	for (p = path; *p != '\0'; ) {
		if ((q = strchr(p, '/')) == NULL)
			q = path + len;

		seg = p;
		n = (size_t)(q - p);

		if (n == 2 && !strncmp(seg, "..", 2)) {
			if (nseg > 0)
				nseg--;
		} else if (!(n == 1 && seg[0] == '.') && n != 0) {
			segs[nseg].p = seg;
			segs[nseg].n = n;
			nseg++;
		}

		if (*q == '\0')
			break;
		p = q + 1;
	}

	for (i = 0; i < nseg; i++) {
		if (i > 0)
			out[o++] = '/';
		if (o + segs[i].n >= out_len) {
			free(segs);
			out[0] = '\0';
			return;
		}
		memcpy(out + o, segs[i].p, segs[i].n);
		o += segs[i].n;
	}
	out[o] = '\0';
	free(segs);

	start = 0;
	stop = o;
	while (start < stop && (out[start] == '/' || out[start] == ' '))
		start++;
	while (stop > start && (out[stop - 1] == '/' || out[stop - 1] == ' '))
		stop--;
	memmove(out, out + start, stop - start);
	o = stop - start;
	out[o] = '\0';

	if (leading) {
		if (o + 2 >= out_len)
			return;
		memmove(out + 1, out, o + 1);
		out[0] = '/';
		o++;
	}

	if (!(o == 1 && out[0] == '/') && trailing) {
		if (o + 2 >= out_len)
			return;
		out[o++] = '/';
		out[o] = '\0';
	}
}

static int
path_char_ok(unsigned char c)
{
	return (isalnum(c) || strchr("-._~:@&=+$,/;", c) != NULL);
}

static void
encode_path(struct ats_sb *sb, const char *s)
{
	char		esc[4];
	unsigned char	c;

	for (; *s != '\0'; s++) {
		c = (unsigned char)*s;

		if (c == '%' && isxdigit((unsigned char)s[1]) &&
		    isxdigit((unsigned char)s[2])) {
			ats_sb_add(sb, s, 3);
			s += 2;
		} else if (path_char_ok(c)) {
			ats_sb_add(sb, s, 1);
		} else {
			(void)snprintf(esc, sizeof(esc), "%%%02X", c);
			ats_sb_adds(sb, esc);
		}
	}
}

void
ats_filter_path(struct ats_sb *sb, const char *path)
{
	char	*dec, *clean;
	size_t	 len;

	if (path == NULL)
		path = "";

	len = strlen(path);
	dec = malloc(len + 1);
	clean = malloc(len + 4);
	if (dec == NULL || clean == NULL)
		fatal("out of memory");

	(void)ats_url_decode(path, len, dec, len + 1);
	remove_dot_segments(dec, clean, len + 4);

	/* CI4 force-prefixes a slash when the original path was relative. */
	if (!strncmp(path, "./", 2) || !strncmp(path, "../", 3)) {
		memmove(clean + 1, clean, strlen(clean) + 1);
		clean[0] = '/';
	}

	encode_path(sb, clean);

	free(dec);
	free(clean);
}

struct site_pair {
	char	*k;
	char	*v;
};

static void
site_url_query(struct ats_sb *sb, const char *q, size_t len)
{
	const char		*p, *end, *amp, *eq;
	struct site_pair	*ps = NULL;
	char			*key, *val;
	size_t			 i, n = 0, cap = 0, klen, vlen, o;

	if (len == 0)
		return;

	end = q + len;

	for (p = q; p < end; ) {
		amp = memchr(p, '&', (size_t)(end - p));
		if (amp == NULL)
			amp = end;
		if (amp == p) {
			p = amp + 1;
			continue;
		}

		eq = memchr(p, '=', (size_t)(amp - p));

		klen = (eq == NULL) ? (size_t)(amp - p) : (size_t)(eq - p);
		key = malloc(klen + 1);
		if (key == NULL)
			fatal("out of memory");
		key[url_decode_lenient(p, klen, key)] = '\0';

		vlen = (eq == NULL) ? 0 : (size_t)(amp - eq - 1);
		val = malloc(vlen + 1);
		if (val == NULL)
			fatal("out of memory");
		if (vlen > 0)
			val[url_decode_lenient(eq + 1, vlen, val)] = '\0';
		else
			val[0] = '\0';

		for (i = 0; i < n; i++) {
			if (!strcmp(ps[i].k, key))
				break;
		}
		if (i < n) {
			free(ps[i].v);
			ps[i].v = val;
			free(key);
		} else {
			if (n == cap) {
				cap = cap == 0 ? 8 : cap * 2;
				ps = realloc(ps, cap * sizeof(*ps));
				if (ps == NULL)
					fatal("out of memory");
			}
			ps[n].k = key;
			ps[n].v = val;
			n++;
		}

		if (amp == end)
			break;
		p = amp + 1;
	}

	for (o = 0; o < n; o++) {
		if (o > 0)
			ats_sb_adds(sb, "&");
		ats_php_urlencode(sb, ps[o].k);
		ats_sb_adds(sb, "=");
		ats_php_urlencode(sb, ps[o].v);
		free(ps[o].k);
		free(ps[o].v);
	}
	free(ps);
}

void
ats_site_url(struct ats_sb *sb, const char *rel)
{
	const char	*hash, *qmark, *base, *rp;
	char		*dec, *clean;
	struct ats_sb	 enc, q;
	size_t		 end, path_len, blen, rl;

	if (rel == NULL)
		rel = "";

	hash = strchr(rel, '#');
	end = (hash == NULL) ? strlen(rel) : (size_t)(hash - rel);
	qmark = memchr(rel, '?', end);
	path_len = (qmark == NULL) ? end : (size_t)(qmark - rel);

	dec = malloc(path_len + 1);
	clean = malloc(path_len + 4);
	if (dec == NULL || clean == NULL)
		fatal("out of memory");
	url_decode_lenient(rel, path_len, dec);
	remove_dot_segments(dec, clean, path_len + 4);
	ats_sb_init(&enc);
	encode_path(&enc, clean);
	free(dec);
	free(clean);

	base = ats_base_url();
	blen = strlen(base);
	while (blen > 0 && base[blen - 1] == '/')
		blen--;
	ats_sb_add(sb, base, blen);

	rp = ats_sb_cstr(&enc);
	rl = enc.len;
	while (rl > 0 && *rp == '/') {
		rp++;
		rl--;
	}
	ats_sb_adds(sb, "/");
	if (rl > 0)
		ats_sb_add(sb, rp, rl);
	ats_sb_free(&enc);

	if (qmark != NULL) {
		ats_sb_init(&q);
		site_url_query(&q, qmark + 1, end - (size_t)(qmark - rel) - 1);
		if (q.len > 0) {
			ats_sb_adds(sb, "?");
			ats_sb_add(sb, q.p, q.len);
		}
		ats_sb_free(&q);
	}

	if (hash != NULL && hash[1] != '\0') {
		ats_sb_adds(sb, "#");
		ats_sb_adds(sb, hash + 1);
	}
}

long long
ats_now_ms(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

long long
ats_now_sec(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);

	return ((long long)tv.tv_sec);
}

void
ats_utc_stamp(time_t t, char *out, size_t out_len)
{
	struct tm	tm;

	gmtime_r(&t, &tm);
	(void)strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &tm);
}

void
ats_now(char *utc, size_t utc_len)
{
	ats_utc_stamp(time(NULL), utc, utc_len);
}

time_t
ats_day_start(int days_ago)
{
	time_t	now_sec, local, day, offset;

	now_sec = time(NULL);
	offset = (time_t)ats_tz_offset_min() * 60;
	local = now_sec + offset;
	day = local - (local % 86400);
	day -= (time_t)days_ago * 86400;

	return (day - offset);
}

void
ats_local_stamp(time_t t, char *out, size_t out_len)
{
	ats_utc_stamp(t + (time_t)ats_tz_offset_min() * 60, out, out_len);
}

void
ats_sha256_hex(const char *s, char out[65])
{
	static const char hex[] = "0123456789abcdef";
	SHA2_CTX	  ctx;
	unsigned char	  digest[SHA256_DIGEST_LENGTH];
	size_t		  i;

	SHA256Init(&ctx);
	SHA256Update(&ctx, (const u_int8_t *)s, strlen(s));
	SHA256Final(digest, &ctx);

	for (i = 0; i < sizeof(digest); i++) {
		out[i * 2] = hex[digest[i] >> 4];
		out[i * 2 + 1] = hex[digest[i] & 0x0f];
	}
	out[sizeof(digest) * 2] = '\0';
}

void
ats_anonymize_ip(const char *in, char *out, size_t out_len)
{
	struct in_addr	 v4;
	struct in6_addr	 v6;

	if (out_len == 0)
		return;
	if (in == NULL) {
		out[0] = '\0';
		return;
	}

	if (inet_pton(AF_INET, in, &v4) == 1) {
		v4.s_addr &= htonl(0xffffff00U);
		if (inet_ntop(AF_INET, &v4, out, (socklen_t)out_len) != NULL)
			return;
	}

	if (inet_pton(AF_INET6, in, &v6) == 1) {
		/* Keep the /48 prefix; clear the interface identifier. */
		memset(&v6.s6_addr[6], 0, 10);
		if (inet_ntop(AF_INET6, &v6, out, (socklen_t)out_len) != NULL)
			return;
	}

	(void)snprintf(out, out_len, "%s", in);
}

void
ats_visitor_hash(const char *agent, const char *ip, char out[65])
{
	static const char hex[] = "0123456789abcdef";
	struct ats_sb	 sb;
	unsigned char	 mac[SHA256_DIGEST_LENGTH];
	const char	*key;
	size_t		 i;

	key = ats_jwt_key();
	ats_sb_init(&sb);
	ats_sb_adds(&sb, agent != NULL ? agent : "");
	ats_sb_adds(&sb, ip != NULL ? ip : "");

	if (key != NULL && *key != '\0') {
		ats_hmac_sha256((const u_int8_t *)key, strlen(key),
		    (const u_int8_t *)(sb.p != NULL ? sb.p : ""), sb.len, mac);
	} else {
		SHA2_CTX	ctx;

		SHA256Init(&ctx);
		SHA256Update(&ctx, (const u_int8_t *)(sb.p != NULL ? sb.p : ""),
		    sb.len);
		SHA256Final(mac, &ctx);
	}

	for (i = 0; i < sizeof(mac); i++) {
		out[i * 2] = hex[mac[i] >> 4];
		out[i * 2 + 1] = hex[mac[i] & 0x0f];
	}
	out[sizeof(mac) * 2] = '\0';

	ats_sb_free(&sb);
}

int
ats_random_bytes(void *out, size_t len)
{
	int	 fd;
	size_t	 off;
	ssize_t	 n;

#if defined(__linux__)
	if (getrandom(out, len, 0) == (ssize_t)len)
		return (0);
#endif

	fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return (-1);

	off = 0;
	while (off < len) {
		n = read(fd, (char *)out + off, len - off);
		if (n <= 0) {
			close(fd);
			return (-1);
		}
		off += (size_t)n;
	}
	close(fd);

	return (0);
}

void
ats_gmt_stamp(time_t t, char *out, size_t out_len)
{
	static const char *days[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	static const char *mons[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	struct tm	tm;

	gmtime_r(&t, &tm);
	(void)snprintf(out, out_len, "%s, %02d-%s-%04d %02d:%02d:%02d GMT",
	    days[tm.tm_wday], tm.tm_mday, mons[tm.tm_mon], tm.tm_year + 1900,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
}

/*
 * CI4 Pager::calculateCurrentPage(): (int) $_GET['page'], clamped to 1.
 * PHP casts numeric prefixes, so "12abc" becomes 12 and "abc" becomes 0.
 */
int
ats_ci_get_page(struct http_request *req)
{
	char	v[64];
	int	page;

	if (ats_query_get(req, "page", v, sizeof(v)) == 0)
		return (1);

	page = atoi(v);

	return (page < 1 ? 1 : page);
}
