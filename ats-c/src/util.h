#ifndef ATS_UTIL_H_INCLUDED
#define ATS_UTIL_H_INCLUDED

#include <stddef.h>
#include <time.h>

#include <kore/kore.h>
#include <kore/http.h>

struct ats_sb {
	char	*p;
	size_t	len;
	size_t	cap;
};

void	 ats_sb_init(struct ats_sb *);
void	 ats_sb_free(struct ats_sb *);
void	 ats_sb_add(struct ats_sb *, const void *, size_t);
void	 ats_sb_adds(struct ats_sb *, const char *);
void	 ats_sb_addf(struct ats_sb *, const char *, ...)
	    __attribute__((format(printf, 2, 3)));
void	 ats_sb_html(struct ats_sb *, const char *);
void	 ats_sb_js(struct ats_sb *, const char *);
const char *ats_sb_cstr(struct ats_sb *);

const char *ats_base_url(void);
const char *ats_cookie_domain(void);

/* PHP trim() default charlist. */
void	 ats_trim(char *);

/*
 * Percent-decode into out; '+' becomes space. Malformed escapes are kept
 * verbatim, like PHP urldecode(). Returns 0, or -1 when out is too small.
 */
int	 ats_url_decode(const char *src, size_t len, char *out, size_t out_len);

/*
 * CI4 URI::filterPath(): urldecode, remove dot segments, then re-encode
 * everything outside the allowed set. This is what the request URI's path
 * and segments are built from, and what site_url() applies to its argument.
 */
void	 ats_filter_path(struct ats_sb *, const char *path);

/* Query-string lookup (PHP parse_str semantics: decoded, last value wins). */
int	 ats_query_get(struct http_request *, const char *key,
	    char *out, size_t out_len);

/* application/x-www-form-urlencoded body lookup, same decoding rules. */
int	 ats_form_get(const char *body, size_t body_len, const char *key,
	    char *out, size_t out_len);

/* PHP urlencode(): unreserved A-Za-z0-9-_. and space as '+'. */
void	 ats_php_urlencode(struct ats_sb *, const char *);

/*
 * CI4 site_url($rel): baseURL + route path + normalized query. Query values
 * are round-tripped through parse_str()/http_build_query(), so they come out
 * RFC1738-encoded ('/' -> %2F, space -> '+').
 */
void	 ats_site_url(struct ats_sb *, const char *rel);

void	 ats_now(char *utc, size_t utc_len);	/* "YYYY-MM-DD HH:MM:SS" */
long long ats_now_sec(void);
long long ats_now_ms(void);

/* Returned strings are "YYYY-MM-DD HH:MM:SS" in UTC. */
void	 ats_utc_stamp(time_t, char *out, size_t out_len);

/* Same format, shifted by the configured local offset (display only). */
void	 ats_local_stamp(time_t, char *out, size_t out_len);

/* UTC epoch of local 00:00 for today minus days_ago. */
time_t	 ats_day_start(int days_ago);

/* "Mon, 03-Nov-2026 08:00:00 GMT" (Cookie::EXPIRES_FORMAT). */
void	 ats_gmt_stamp(time_t, char *out, size_t out_len);

/* SHA-256 of a NUL-terminated string, lowercase hex. */
void	 ats_sha256_hex(const char *s, char out[65]);

/* Mask the host part of an IPv4/IPv6 address for storage. */
void	 ats_anonymize_ip(const char *in, char *out, size_t out_len);

/* Keyed visitor identifier (HMAC-SHA256), lowercase hex. */
void	 ats_visitor_hash(const char *agent, const char *ip, char out[65]);

/* Cryptographically strong random bytes. Returns 0 on success. */
int	 ats_random_bytes(void *out, size_t len);

int	 ats_ci_get_page(struct http_request *);

#endif
