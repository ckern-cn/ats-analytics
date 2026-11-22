/*
 * Request handlers.
 *
 * The tracking endpoints keep the original wire format (small JavaScript
 * snippets) so existing embeds keep working.  The admin side is a small,
 * self-hosted analytics UI: dashboard, app create/edit/delete, login/logout.
 */

#include <sys/socket.h>
#include <sys/types.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include <kore/kore.h>
#include <kore/http.h>

#include "ats.h"
#include "db.h"
#include "geoip.h"
#include "handlers.h"
#include "i18n.h"
#include "jwt.h"
#include "snowflake.h"
#include "throttle.h"
#include "ua.h"
#include "util.h"
#include "wru.h"
#include "views.h"

#define MAX_BODY	1048576
#define QV_LEN		8192
#define MAX_HOST	256
#define TOP_N		10
#define TREND_DAYS	14

#define GLOBAL_RATE_CAP		600	/* requests per minute per IP */
#define LOGIN_RATE_CAP		20	/* login attempts per 5 minutes */
#define LOGIN_RATE_PERIOD	300

static int	php_empty(const char *);
static int	php_space(unsigned char);
static char	*seg_dup(const char *, size_t);
static void	 send_typed(struct http_request *, const char *,
		    struct ats_sb *, int);
static void	 redirect_to(struct http_request *, int, const char *);
static char	*auth_token(struct http_request *);
static char	*read_body(struct http_request *, size_t *);
static int	 ct_contains(struct http_request *, const char *);
static int	 is_form_request(struct http_request *);
static char	*request_var(struct http_request *, const char *, size_t,
		    const char *, size_t);
static int	 json_scalar(cJSON *, const char *, char *, size_t, int);
static int	 uri_host(const char *, char *, size_t);
static int	 same_origin(struct http_request *);
static int	 dnt_enabled(struct http_request *);
static int	 normalize_host(const char *, char *, size_t);
static int	 normalize_hosts(const char *, char out[ATS_MAX_HOSTS][MAX_HOST],
		    int *n_out);
static int	 host_matches(const char *, const char *);
static int	 app_exists(const char *, int *, char *, size_t);
static int	 app_hosts(const char *, char out[ATS_MAX_HOSTS][MAX_HOST],
		    char *, size_t);
static int	 app_get(const char *, char *, size_t, char *, size_t,
		    char *, size_t, char *, size_t);
static int	 host_taken(const char *, const char *, char *, size_t);
static int	 canonical_app_id(const char *, const char *, size_t,
		    char *, size_t);
static void	 visit_time_local(const char *, char *, size_t);
static int	 load_top(const char *, const struct ats_val *, int,
		    struct ats_top_item *, char labels[][512], int);
static void	 send_error(struct http_request *, enum ats_locale, int,
		    const char *, const char *);
static void	 set_locale_header(struct http_request *, enum ats_locale);
static int	 constant_time_eq(const char *, const char *);

static int
php_empty(const char *s)
{
	return (s == NULL || *s == '\0' || !strcmp(s, "0"));
}

static int
php_space(unsigned char c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v');
}

static char *
seg_dup(const char *s, size_t n)
{
	char	*p;

	p = kore_malloc(n + 1);
	memcpy(p, s, n);
	p[n] = '\0';

	return (p);
}

static void
send_typed(struct http_request *req, const char *ctype, struct ats_sb *sb,
    int admin)
{
	http_response_header(req, "content-type", ctype);
	http_response_header(req, "cache-control", "no-store");
	http_response_header(req, "pragma", "no-cache");
	http_response_header(req, "x-content-type-options", "nosniff");

	if (admin) {
		http_response_header(req, "x-frame-options", "DENY");
		http_response_header(req, "referrer-policy", "no-referrer");
	}

	http_response(req, 200, sb->p, sb->len);
}

void
ats_send_html(struct http_request *req, struct ats_sb *sb)
{
	send_typed(req, "text/html; charset=UTF-8", sb, 1);
}

void
ats_send_js(struct http_request *req, struct ats_sb *sb)
{
	send_typed(req, "text/javascript; charset=UTF-8", sb, 0);
}

void
ats_send_status(struct http_request *req, int code)
{
	http_response_header(req, "content-type", "text/html; charset=UTF-8");
	http_response_header(req, "cache-control", "no-store");
	http_response_header(req, "x-content-type-options", "nosniff");
	http_response(req, code, "", 0);
}

void
ats_send_redirect(struct http_request *req, int status, const char *loc)
{
	/*
	 * Kore writes header values verbatim. A CR/LF in a redirect target would
	 * split the response and inject arbitrary headers, so reject it at the
	 * boundary even if a caller forgot to validate its input.
	 */
	if (loc == NULL || strpbrk(loc, "\r\n") != NULL) {
		kore_log(LOG_WARNING, "redirect: rejected control characters");
		http_response_header(req, "cache-control", "no-store");
		http_response(req, 400, NULL, 0);
		return;
	}

	http_response_header(req, "location", loc);
	http_response_header(req, "cache-control", "no-store");
	http_response(req, status, NULL, 0);
}

static void
redirect_to(struct http_request *req, int status, const char *rel)
{
	struct ats_sb	url;

	ats_sb_init(&url);
	ats_site_url(&url, rel);
	ats_send_redirect(req, status, ats_sb_cstr(&url));
	ats_sb_free(&url);
}


static char *
auth_token(struct http_request *req)
{
	char		*raw, *dec;
	size_t		 len;

	http_populate_cookies(req);
	if (http_request_cookie(req, "auth_jwt", &raw) != KORE_RESULT_OK)
		return (NULL);

	len = strlen(raw);
	dec = kore_malloc(len + 1);
	if (ats_url_decode(raw, len, dec, len + 1) == -1) {
		kore_free(dec);
		return (NULL);
	}

	return (dec);
}

int
ats_require_auth(struct http_request *req)
{
	struct ats_sb	rel, pth;
	char		*tok;
	int		 status;

	tok = auth_token(req);
	if (tok != NULL) {
		if (*tok != '\0' && ats_jwt_verify(tok) == 0) {
			kore_free(tok);
			return (0);
		}
		kore_free(tok);
	}

	ats_sb_init(&rel);
	ats_sb_adds(&rel, "login?ref=");
	ats_sb_init(&pth);
	ats_filter_path(&pth, req->path);
	ats_sb_add(&rel, pth.p, pth.len);
	ats_sb_free(&pth);

	status = (req->method == HTTP_METHOD_POST) ? 303 : 302;
	redirect_to(req, status, ats_sb_cstr(&rel));
	ats_sb_free(&rel);

	return (-1);
}

char *
ats_segment(struct http_request *req, int n)
{
	struct ats_sb	 sb;
	char		*path, *base, *p, *seg, *out;
	size_t		 len;
	int		 idx;

	if (n < 1)
		return (seg_dup("", 0));

	ats_sb_init(&sb);
	ats_filter_path(&sb, req->path);
	path = kore_strdup(ats_sb_cstr(&sb));
	ats_sb_free(&sb);

	base = path;
	while (*base == '/')
		base++;

	len = strlen(base);
	while (len > 0 && base[len - 1] == '/')
		base[--len] = '\0';

	if (len == 0) {
		kore_free(path);
		return (seg_dup("", 0));
	}

	idx = 1;
	seg = base;

	for (p = base;; p++) {
		if (*p != '/' && *p != '\0')
			continue;

		if (idx == n) {
			out = seg_dup(seg, (size_t)(p - seg));
			kore_free(path);
			return (out);
		}

		if (*p == '\0')
			break;

		idx++;
		seg = p + 1;
	}

	kore_free(path);

	return (seg_dup("", 0));
}

void
ats_client_ip(struct http_request *req, char *out, size_t out_len)
{
	const char	*xri;
	u_int8_t	 probe[16];

	if (out_len == 0)
		return;

	out[0] = '\0';

	/* nginx is the only client; it overwrites X-Real-IP on every request. */
	if (http_request_header(req, "x-real-ip", &xri) == KORE_RESULT_OK &&
	    xri != NULL &&
	    (inet_pton(AF_INET, xri, probe) == 1 ||
	     inet_pton(AF_INET6, xri, probe) == 1)) {
		(void)snprintf(out, out_len, "%s", xri);
		return;
	}

	if (req->owner != NULL) {
		if (req->owner->family == AF_INET)
			(void)inet_ntop(AF_INET,
			    &req->owner->addr.ipv4.sin_addr, out,
			    (socklen_t)out_len);
		else if (req->owner->family == AF_INET6)
			(void)inet_ntop(AF_INET6,
			    &req->owner->addr.ipv6.sin6_addr, out,
			    (socklen_t)out_len);
	}

	if (out[0] == '\0')
		(void)snprintf(out, out_len, "0.0.0.0");
}

static char *
read_body(struct http_request *req, size_t *lenp)
{
	char		*buf;
	size_t		 want, off;
	ssize_t		 r;

	*lenp = 0;

	want = (size_t)req->http_body_length;
	if (want == 0)
		want = (size_t)req->content_length;
	if (want == 0 || want > MAX_BODY)
		return (NULL);

	if (http_body_rewind(req) != KORE_RESULT_OK)
		return (NULL);

	buf = kore_malloc(want + 1);

	off = 0;
	while (off < want) {
		r = http_body_read(req, buf + off, want - off);
		if (r <= 0)
			break;
		off += (size_t)r;
	}

	buf[off] = '\0';
	*lenp = off;

	if (off == 0) {
		kore_free(buf);
		return (NULL);
	}

	return (buf);
}

static int
ct_contains(struct http_request *req, const char *needle)
{
	const char	*ct;

	if (http_request_header(req, "content-type", &ct) != KORE_RESULT_OK ||
	    ct == NULL)
		return (0);

	return (strstr(ct, needle) != NULL);
}

static int
is_form_request(struct http_request *req)
{
	const char	*ct;

	if (http_request_header(req, "content-type", &ct) != KORE_RESULT_OK ||
	    ct == NULL)
		return (0);

	return (strncasecmp(ct, "application/x-www-form-urlencoded", 33) == 0);
}

static char *
request_var(struct http_request *req, const char *body, size_t body_len,
    const char *key, size_t out_len)
{
	char	*out, *post;
	size_t	 plen;

	out = kore_malloc(out_len);
	out[0] = '\0';

	(void)ats_query_get(req, key, out, out_len);

	if (req->method == HTTP_METHOD_POST && body != NULL &&
	    is_form_request(req)) {
		post = kore_malloc(QV_LEN);
		if (ats_form_get(body, body_len, key, post, QV_LEN)) {
			plen = strlen(post);
			if (plen >= out_len)
				plen = out_len - 1;
			memcpy(out, post, plen);
			out[plen] = '\0';
		}
		kore_free(post);
	}

	return (out);
}

static int
json_scalar(cJSON *root, const char *key, char *out, size_t out_len,
    int allow_null)
{
	cJSON	*item;

	item = cJSON_GetObjectItemCaseSensitive(root, key);

	if (item == NULL || cJSON_IsNull(item)) {
		if (allow_null && out_len > 0)
			out[0] = '\0';
		return (allow_null ? 0 : -1);
	}

	if (cJSON_IsString(item)) {
		if (strlen(item->valuestring) >= out_len)
			return (-1);
		(void)snprintf(out, out_len, "%s", item->valuestring);
		return (0);
	}

	if (cJSON_IsBool(item)) {
		(void)snprintf(out, out_len, "%s",
		    cJSON_IsTrue(item) ? "1" : "");
		return (0);
	}

	if (cJSON_IsNumber(item)) {
		if (item->valuedouble == (double)item->valueint)
			(void)snprintf(out, out_len, "%d", item->valueint);
		else
			(void)snprintf(out, out_len, "%.14G",
			    item->valuedouble);
		return (0);
	}

	return (-1);
}

static int
uri_host(const char *url, char *out, size_t out_len)
{
	const char	*p, *auth, *end, *at, *colon, *rb;
	size_t		 n;
	int		 numeric;

	if (out_len > 0)
		out[0] = '\0';
	if (url == NULL)
		return (-1);

	if (url[0] == '/' && url[1] == '/') {
		auth = url + 2;
	} else {
		p = strstr(url, "://");
		if (p == NULL)
			return (-1);
		auth = p + 3;
	}

	end = auth + strcspn(auth, "/?#");
	if (auth == end)
		return (-1);

	at = NULL;
	for (p = auth; p < end; p++) {
		if (*p == '@')
			at = p;
	}
	if (at != NULL)
		auth = at + 1;

	if (auth < end && *auth == '[') {
		rb = memchr(auth, ']', (size_t)(end - auth));
		if (rb == NULL)
			return (-1);
		n = (size_t)(rb - auth - 1);
		if (n >= out_len)
			return (-1);
		memcpy(out, auth + 1, n);
		out[n] = '\0';
		return (0);
	}

	colon = NULL;
	for (p = auth; p < end; p++) {
		if (*p == ':')
			colon = p;
	}
	if (colon != NULL) {
		numeric = 1;
		for (p = colon + 1; p < end; p++) {
			if (!isdigit((unsigned char)*p)) {
				numeric = 0;
				break;
			}
		}
		if (!numeric)
			return (-1);
		end = colon;
	}

	n = (size_t)(end - auth);
	if (n >= out_len)
		return (-1);
	memcpy(out, auth, n);
	out[n] = '\0';

	return (0);
}

/*
 * Accepts either a bare host ("example.com"), a URL
 * ("https://www.example.com/path") or "example.com:8080", and returns a
 * lowercase host without port, path or trailing dot.
 */
static int
normalize_host(const char *in, char *out, size_t out_len)
{
	char	 tmp[MAX_HOST * 4];
	char	 host[MAX_HOST];
	char	*start, *end, *p, *colon;
	size_t	 len, i;
	int	 bracketed = 0;

	if (out_len == 0)
		return (-1);
	out[0] = '\0';

	if (in == NULL)
		return (-1);

	if (strlen(in) >= sizeof(tmp))
		return (-1);
	(void)snprintf(tmp, sizeof(tmp), "%s", in);
	ats_trim(tmp);
	if (*tmp == '\0')
		return (-1);

	if (strstr(tmp, "://") != NULL) {
		if (uri_host(tmp, host, sizeof(host)) == -1)
			return (-1);
		start = host;
	} else {
		start = tmp;
		while (*start == ' ')
			start++;

		/* Strip userinfo. */
		if ((p = strrchr(start, '@')) != NULL)
			start = p + 1;

		/* Stop at path/query/fragment. */
		end = start + strcspn(start, "/?#");
		len = (size_t)(end - start);
		if (len == 0 || len >= sizeof(host))
			return (-1);
		memcpy(host, start, len);
		host[len] = '\0';
		start = host;
	}

	/* Bracketed IPv6. */
	if (*start == '[') {
		end = strchr(start, ']');
		if (end == NULL)
			return (-1);
		start++;
		*end = '\0';
		bracketed = 1;
	}

	/* Strip a numeric port (not from an IPv6 literal). */
	if (!bracketed && (colon = strrchr(start, ':')) != NULL) {
		if (colon[1] == '\0') {
			*colon = '\0';
		} else {
			for (p = colon + 1; *p != '\0'; p++) {
				if (!isdigit((unsigned char)*p))
					break;
			}
			if (*p == '\0')
				*colon = '\0';
		}
	}

	len = strlen(start);
	while (len > 0 && start[len - 1] == '.')
		start[--len] = '\0';

	if (len == 0 || len >= out_len)
		return (-1);

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)start[i];

		if (c >= 'A' && c <= 'Z')
			c = (unsigned char)(c - 'A' + 'a');

		if (!(isalnum(c) || c == '-' || c == '.' || c == ':')) {
			kore_log(LOG_WARNING, "rejecting host %s", start);
			return (-1);
		}
		out[i] = (char)c;
	}
	out[len] = '\0';

	return (0);
}

static int
normalize_hosts(const char *input, char out[ATS_MAX_HOSTS][MAX_HOST],
    int *n_out)
{
	char	 buf[2048];
	char	*p, *tok, *save;
	int	 n, i;

	n = 0;
	*n_out = 0;
	for (i = 0; i < ATS_MAX_HOSTS; i++)
		out[i][0] = '\0';

	if (input == NULL || strlen(input) >= sizeof(buf))
		return (-1);
	(void)snprintf(buf, sizeof(buf), "%s", input);

	for (p = buf; ; p = NULL) {
		tok = strtok_r(p, ", \t\r\n", &save);
		if (tok == NULL)
			break;

		/* Refuse a fourth host before indexing out[ATS_MAX_HOSTS]. */
		if (n >= ATS_MAX_HOSTS)
			return (-1);

		if (normalize_host(tok, out[n], MAX_HOST) == -1)
			return (-1);

		for (i = 0; i < n; i++) {
			if (host_matches(out[i], out[n]))
				break;
		}
		if (i == n)
			n++;
	}

	if (n == 0)
		return (-1);

	*n_out = n;

	return (0);
}

static int
host_matches(const char *a, const char *b)
{
	if (a == NULL || b == NULL)
		return (0);

	return (strcasecmp(a, b) == 0);
}

/*
 * Browsers send Origin on cross-origin POSTs. Same-origin form posts carry the
 * canonical host. Requests without Origin (curl, scripts) are allowed, so the
 * admin API stays usable; browsers get a second CSRF barrier besides
 * SameSite=Lax.
 */
static int
same_origin(struct http_request *req)
{
	const char	*origin, *base, *site;
	char		 host[MAX_HOST];

	if (http_request_header(req, "origin", &origin) != KORE_RESULT_OK ||
	    origin == NULL || *origin == '\0')
		return (0);

	/*
	 * Some embedded browsers and sandboxed contexts send "Origin: null"
	 * for same-origin form posts. Fetch Metadata can still identify those
	 * as same-origin. A non-null Origin must pass the normal host check even
	 * if a client (or proxy) supplies a misleading Sec-Fetch-Site header.
	 */
	if (!strcasecmp(origin, "null")) {
		if (http_request_header(req, "sec-fetch-site", &site)
		    == KORE_RESULT_OK && site != NULL &&
		    !strcasecmp(site, "same-origin"))
			return (0);
		return (-1);
	}

	if (uri_host(origin, host, sizeof(host)) == -1)
		return (-1);

	base = ats_base_url_host();
	if (base == NULL || *base == '\0')
		return (0);

	return (host_matches(host, base) ? 0 : -1);
}

static int
dnt_enabled(struct http_request *req)
{
	const char	*dnt;

	if (http_request_header(req, "dnt", &dnt) != KORE_RESULT_OK ||
	    dnt == NULL)
		return (0);

	return (!strcmp(dnt, "1") || !strcasecmp(dnt, "yes"));
}

static int
app_exists(const char *id, int *exists, char *err, size_t errlen)
{
	sqlite3_stmt	*stmt;

	*exists = 0;

	if (ats_db_prepare("SELECT COUNT(*) FROM apps WHERE id = ?", &stmt,
	    err, errlen) == KORE_RESULT_ERROR)
		return (-1);

	(void)ats_bind_text(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int64(stmt, 0) > 0)
		*exists = 1;
	ats_db_finish(stmt);

	return (0);
}

static int
app_hosts(const char *id, char out[ATS_MAX_HOSTS][MAX_HOST], char *err,
    size_t errlen)
{
	sqlite3_stmt		*stmt;
	const unsigned char	*txt;
	int			 i;

	for (i = 0; i < ATS_MAX_HOSTS; i++)
		out[i][0] = '\0';

	if (ats_db_prepare("SELECT host_url, host_url_01, host_url_02 "
	    "FROM apps WHERE id = ? LIMIT 1", &stmt, err, errlen)
	    == KORE_RESULT_ERROR)
		return (-1);

	(void)ats_bind_text(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		for (i = 0; i < ATS_MAX_HOSTS; i++) {
			txt = sqlite3_column_text(stmt, i);
			if (txt != NULL) {
				(void)snprintf(out[i], MAX_HOST, "%s",
				    (const char *)txt);
			}
		}
	}
	ats_db_finish(stmt);

	return (0);
}

static int
app_get(const char *id, char *name, size_t name_len, char *hosts,
    size_t hosts_len, char *remark, size_t remark_len, char *err,
    size_t errlen)
{
	sqlite3_stmt		*stmt;
	const unsigned char	*txt;
	int			 i;

	name[0] = '\0';
	hosts[0] = '\0';
	remark[0] = '\0';

	if (ats_db_prepare("SELECT name, host_url, host_url_01, host_url_02, "
	    "remark FROM apps WHERE id = ? LIMIT 1", &stmt, err, errlen)
	    == KORE_RESULT_ERROR)
		return (-1);

	(void)ats_bind_text(stmt, 1, id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		txt = sqlite3_column_text(stmt, 0);
		if (txt != NULL)
			(void)snprintf(name, name_len, "%s", (const char *)txt);

		for (i = 1; i <= 3; i++) {
			txt = sqlite3_column_text(stmt, i);
			if (txt == NULL || *txt == '\0')
				continue;
			if (hosts[0] != '\0')
				strncat(hosts, ", ",
				    hosts_len - strlen(hosts) - 1);
			strncat(hosts, (const char *)txt,
			    hosts_len - strlen(hosts) - 1);
		}

		txt = sqlite3_column_text(stmt, 4);
		if (txt != NULL)
			(void)snprintf(remark, remark_len, "%s",
			    (const char *)txt);
	}
	ats_db_finish(stmt);

	return (0);
}

static int
host_taken(const char *id, const char *host, char *err, size_t errlen)
{
	sqlite3_stmt	*stmt;
	sqlite3_int64	 n;
	int		 r;

	if (ats_db_prepare("SELECT COUNT(*) FROM apps WHERE id <> ? AND "
	    "(host_url = ? COLLATE NOCASE OR host_url_01 = ? COLLATE NOCASE "
	    "OR host_url_02 = ? COLLATE NOCASE)", &stmt, err, errlen)
	    == KORE_RESULT_ERROR)
		return (-1);

	(void)ats_bind_text(stmt, 1, id);
	(void)ats_bind_text(stmt, 2, host);
	(void)ats_bind_text(stmt, 3, host);
	(void)ats_bind_text(stmt, 4, host);

	r = sqlite3_step(stmt);
	n = (r == SQLITE_ROW) ? sqlite3_column_int64(stmt, 0) : 0;
	ats_db_finish(stmt);

	return (n > 0);
}

static int
canonical_app_id(const char *id, const char *fallback, size_t fallback_len,
    char *out, size_t out_len)
{
	if (id == NULL)
		return (-1);
	if (*id == '\0' || !strcmp(id, "g"))
		return (fallback != NULL && fallback_len > 0 ?
		    (int)snprintf(out, out_len, "%s", fallback) :
		    (int)snprintf(out, out_len, "%s", ""));
	return (snprintf(out, out_len, "%s", id) >= (int)out_len ? -1 : 0);
}

static void
visit_time_local(const char *utc, char *out, size_t out_len)
{
	struct tm	 tm;
	time_t		 t;

	if (utc == NULL || *utc == '\0') {
		if (out_len > 0)
			out[0] = '\0';
		return;
	}

	memset(&tm, 0, sizeof(tm));
	if (strptime(utc, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
		(void)snprintf(out, out_len, "%s", utc);
		return;
	}

	t = timegm(&tm);
	ats_local_stamp(t, out, out_len);
}

static int
load_top(const char *sql, const struct ats_val *vals, int nvals,
    struct ats_top_item *items, char labels[][512], int max)
{
	sqlite3_stmt		*stmt;
	const unsigned char	*txt;
	char			 err[256];
	int			 n;

	if (ats_db_prepare(sql, &stmt, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "top query: %s", err);
		return (-1);
	}

	for (n = 0; n < nvals; n++) {
		if (vals[n].type == ATS_VAL_TEXT)
			(void)ats_bind_text(stmt, n + 1, vals[n].txt);
		else
			(void)ats_bind_int64(stmt, n + 1, vals[n].num);
	}

	n = 0;
	while (n < max && sqlite3_step(stmt) == SQLITE_ROW) {
		txt = sqlite3_column_text(stmt, 0);
		if (txt == NULL)
			labels[n][0] = '\0';
		else
			(void)snprintf(labels[n], 512, "%s", (const char *)txt);
		items[n].label = labels[n];
		items[n].count = sqlite3_column_int64(stmt, 1);
		n++;
	}
	ats_db_finish(stmt);

	return (n);
}

static void
set_locale_header(struct http_request *req, enum ats_locale locale)
{
	http_response_header(req, "content-language", ats_locale_code(locale));
}

static void
send_error(struct http_request *req, enum ats_locale locale, int status,
    const char *title, const char *message)
{
	struct ats_sb	sb;

	ats_sb_init(&sb);
	view_error(&sb, locale, status, title, message);
	 set_locale_header(req, locale);
	http_response_header(req, "content-type", "text/html; charset=UTF-8");
	http_response_header(req, "cache-control", "no-store");
	http_response_header(req, "x-content-type-options", "nosniff");
	http_response(req, status, sb.p, sb.len);
	ats_sb_free(&sb);
}

static int
constant_time_eq(const char *a, const char *b)
{
	volatile unsigned char	diff;
	size_t			alen, blen, i;

	if (a == NULL || b == NULL)
		return (0);

	alen = strlen(a);
	blen = strlen(b);
	if (alen != blen)
		return (0);

	diff = 0;
	for (i = 0; i < alen; i++)
		diff |= (unsigned char)a[i] ^ (unsigned char)b[i];

	return (diff == 0);
}

int
ats_h_health(struct http_request *req)
{
	char	err[256];

	if (ats_db_check(err, sizeof(err)) == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "health: %s", err);
		http_response_header(req, "content-type",
		    "text/plain; charset=UTF-8");
		http_response(req, 503, "db error\n", 9);
		return (KORE_RESULT_OK);
	}

	http_response_header(req, "content-type", "text/plain; charset=UTF-8");
	http_response(req, 200, "ok\n", 3);

	return (KORE_RESULT_OK);
}

int
ats_h_wru(struct http_request *req)
{
	char		 ref[QV_LEN], page[QV_LEN], ref_host[MAX_HOST], err[256];
	char		 token[64], msg[128];
	char		*rec_id;
	struct ats_val	 vals[4];
	struct ats_sb	 sb;
	int		 ok, changes;

	rec_id = ats_segment(req, 2);

	ref[0] = '\0';
	page[0] = '\0';
	ref_host[0] = '\0';
	token[0] = '\0';
	(void)ats_query_get(req, "ref_url", ref, sizeof(ref));
	(void)ats_query_get(req, "page_url", page, sizeof(page));
	(void)ats_query_get(req, "t", token, sizeof(token));

	/* The id is guessable; only update when the HMAC matches. */
	if (ats_wru_verify(rec_id, token) == -1) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "record update status: 0");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(rec_id);
		return (KORE_RESULT_OK);
	}

	if (normalize_host(ref, ref_host, sizeof(ref_host)) == -1)
		ref_host[0] = '\0';

	vals[0] = ATS_TXT(ref);
	vals[1] = ATS_TXT(page);
	vals[2] = ATS_TXT(ref_host);
	vals[3] = ATS_TXT(rec_id);

	ok = ats_db_run("UPDATE apps_traffic SET referring_url = ?, "
	    "page_url = ?, referring_host = ?, record_status = 1 "
	    "WHERE id = ? AND record_status = 0", vals, 4, err, sizeof(err));

	changes = ok == KORE_RESULT_OK ? ats_db_changes() : 0;
	if (ok == KORE_RESULT_ERROR)
		kore_log(LOG_ERR, "wru(%s): %s", rec_id, err);

	(void)snprintf(msg, sizeof(msg), "record update status: %d", changes);

	ats_sb_init(&sb);
	view_js_msg(&sb, msg);
	ats_send_js(req, &sb);
	ats_sb_free(&sb);

	kore_free(rec_id);

	return (KORE_RESULT_OK);
}

int
ats_h_rv(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char		 err[256], host[MAX_HOST], ip[64], stored_ip[64], visit[32];
	char		 unique[65], hosts[ATS_MAX_HOSTS][MAX_HOST];
	char		 idbuf[32], wtoken[33];
	char		*api, *app_id, *agent;
	const char	*country;
	const char	*a;
	struct ats_ua	 ua;
	struct ats_val	 vals[18];
	struct ats_sb	 sb;
	uint64_t	 id;
	size_t		 alen;
	int		 exists, is_self, is_error, i, saw_host;
	sqlite3_int64	 sid;

	api = ats_segment(req, 1);
	app_id = ats_segment(req, 2);

	is_error = 0;
	if (app_exists(app_id, &exists, err, sizeof(err)) == -1) {
		kore_log(LOG_ERR, "rv.js app lookup: %s", err);
		is_error = 1;
	}
	is_self = (req->referer == NULL);

	if (is_error != 0) {
		kore_free(api);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	if (is_self || strcmp(api, "rv.js") != 0 || exists == 0) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "create new record failed");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(api);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	if (normalize_host(req->referer, host, sizeof(host)) == -1 ||
	    php_empty(host)) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "empty host");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(api);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	if (app_hosts(app_id, hosts, err, sizeof(err)) == -1) {
		kore_log(LOG_ERR, "rv.js host lookup: %s", err);
		kore_free(api);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	saw_host = 0;
	for (i = 0; i < ATS_MAX_HOSTS; i++) {
		char stored[MAX_HOST];

		if (normalize_host(hosts[i], stored, sizeof(stored)) == -1)
			continue;
		if (host_matches(stored, host)) {
			saw_host = 1;
			break;
		}
	}

	if (!saw_host) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "app not exists");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(api);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	if (ats_respect_dnt() && dnt_enabled(req)) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "tracking disabled by Do Not Track");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(api);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	if (req->agent != NULL) {
		a = req->agent;
		while (php_space((unsigned char)*a))
			a++;
		alen = strlen(a);
		while (alen > 0 && php_space((unsigned char)a[alen - 1]))
			alen--;
		agent = seg_dup(a, alen);
		ats_ua_detect(agent, &ua);
	} else {
		agent = kore_strdup("");
		ats_ua_detect(NULL, &ua);
	}

	ats_client_ip(req, ip, sizeof(ip));

	country = ats_geoip_country(ip);
	if (!strcmp(country, "hk") || !strcmp(country, "tw") ||
	    !strcmp(country, "mo"))
		country = "cn";

	if (ats_anonymize_enabled())
		ats_anonymize_ip(ip, stored_ip, sizeof(stored_ip));
	else
		(void)snprintf(stored_ip, sizeof(stored_ip), "%s", ip);

	ats_visitor_hash(agent, stored_ip, unique);

	id = ats_next_id();
	sid = (sqlite3_int64)id;
	(void)snprintf(idbuf, sizeof(idbuf), "%llu",
	    (unsigned long long)id);
	ats_wru_sign(idbuf, wtoken);
	ats_now(visit, sizeof(visit));

	vals[0] = ATS_NUM(sid);
	vals[1] = ATS_TXT(app_id);
	vals[2] = ATS_TXT(req->referer);
	vals[3] = ATS_TXT(visit);
	vals[4] = ATS_TXT(stored_ip);
	vals[5] = ATS_TXT(agent);
	vals[6] = ATS_TXT("");
	vals[7] = ATS_TXT("");
	vals[8] = ATS_NUM(0);
	vals[9] = ATS_TXT(country);
	vals[10] = ATS_TXT("");
	vals[11] = ATS_TXT(ua.is_mobile ? "mobile" :
	    (ua.is_robot ? "robot" : "pc"));
	vals[12] = ATS_TXT(ua.browser);
	vals[13] = ATS_TXT(ua.platform);
	vals[14] = ATS_TXT("");
	vals[15] = ATS_NUM(0);
	vals[16] = ATS_TXT(unique);
	vals[17] = ATS_TXT("");

	if (ats_db_run("INSERT INTO apps_traffic (id, app_id, page_url, "
	    "visit_date, visitor_ip, user_agent, referring_url, "
	    "referring_host, visit_duration, country, city, device_type, "
	    "browser, operating_system, traffic_source, record_status, "
	    "unique_visitors, remark) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
	    "?, ?, ?, ?, ?, ?, ?, ?)", vals, 18, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "rv.js insert(%llu): %s",
		    (unsigned long long)id, err);
		ats_sb_init(&sb);
		view_js_msg(&sb, "create new record failed");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(agent);
		kore_free(api);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	ats_sb_init(&sb);
	view_js_api_wru(&sb, id, wtoken);
	ats_send_js(req, &sb);
	ats_sb_free(&sb);

	kore_free(agent);
	kore_free(api);
	kore_free(app_id);

	return (KORE_RESULT_OK);
}

int
ats_h_pv(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char			err[256];
	char			*container, *num_s, *app_id;
	char			*countries[5];
	struct ats_country_count rows[5];
	struct ats_val		 v[2];
	struct ats_sb		 sb;
	sqlite3_stmt		*stmt;
	sqlite3_int64		 pv_total, sum;
	long long		 cnt;
	int			 exists, num, n, i;

	container = ats_segment(req, 4);
	num_s = ats_segment(req, 3);
	app_id = ats_segment(req, 2);

	if (php_empty(container)) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "container is required");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	num = php_empty(num_s) ? 0 : atoi(num_s);
	if (num < 3 || num > 5) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "the number of countries is out of range");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	exists = 0;
	if (!php_empty(app_id) &&
	    app_exists(app_id, &exists, err, sizeof(err)) == -1) {
		kore_log(LOG_ERR, "pv app lookup: %s", err);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	if (exists == 0) {
		ats_sb_init(&sb);
		view_js_msg(&sb, "app not exists");
		ats_send_js(req, &sb);
		ats_sb_free(&sb);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		return (KORE_RESULT_OK);
	}

	v[0] = ATS_TXT(app_id);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ?", v, 1, &pv_total, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "pv count: %s", err);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	n = 0;
	sum = 0;
	for (i = 0; i < 5; i++)
		countries[i] = NULL;

	if (ats_db_prepare("SELECT CASE WHEN country IS NULL OR country = '' "
	    "THEN 'unknown' ELSE country END AS c, COUNT(*) AS pv "
	    "FROM apps_traffic WHERE app_id = ? GROUP BY c "
	    "ORDER BY pv DESC LIMIT ?", &stmt, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "pv group: %s", err);
		kore_free(container);
		kore_free(num_s);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	(void)ats_bind_text(stmt, 1, app_id);
	(void)ats_bind_int64(stmt, 2, num);

	while (n < num && sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char	*txt;

		txt = sqlite3_column_text(stmt, 0);
		if (txt != NULL) {
			countries[n] = kore_strdup((const char *)txt);
			rows[n].country = countries[n];
		} else {
			rows[n].country = "unknown";
		}
		cnt = sqlite3_column_int64(stmt, 1);
		rows[n].count = cnt;
		sum += cnt;
		n++;
	}

	ats_sb_init(&sb);
	view_js_res_pv(&sb, locale, container, pv_total, rows, n,
	    pv_total > sum ? pv_total - sum : 0);
	ats_db_finish(stmt);
	ats_send_js(req, &sb);
	ats_sb_free(&sb);

	for (i = 0; i < 5; i++)
		kore_free(countries[i]);

	kore_free(container);
	kore_free(num_s);
	kore_free(app_id);

	return (KORE_RESULT_OK);
}

int
ats_h_dv(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char			 err[256] = "", tmp[32], app_id[32], first_id[32];
	char			 ts_today[32], ts_yesterday[32], ts_7[32];
	char			 ts_30[32], mod[32];
	char			 times[ATS_PER_PAGE][32];
	char			*cells[ATS_PER_PAGE][7];
	char			*names[ATS_MAX_APPS];
	char			*hosts[ATS_MAX_APPS];
	char			 page_labels[TOP_N][512];
	char			 ref_labels[TOP_N][512];
	char			 country_labels[TOP_N][512];
	char			 day_dates[TREND_DAYS][11];
	struct ats_app_row	 apps[ATS_MAX_APPS];
	struct ats_visit_row	 rows[ATS_PER_PAGE];
	struct ats_day_count	 days[TREND_DAYS];
	struct ats_top_item	 pages[TOP_N], refs[TOP_N], country_items[TOP_N];
	struct ats_dashboard	 dash;
	struct ats_summary	 s;
	struct ats_val		 v[3];
	struct ats_sb		 sb;
	sqlite3_stmt		*stmt;
	sqlite3_int64		 total, page_count, page, cur, offset;
	int			 n_apps, n_rows, n_pages, n_refs, n_countries;
	int			 n_days, i, k, bad;

	bad = 0;
	n_apps = 0;
	n_rows = 0;
	n_pages = 0;
	n_refs = 0;
	n_countries = 0;
	n_days = 0;
	first_id[0] = '\0';
	app_id[0] = '\0';

	memset(&s, 0, sizeof(s));
	memset(&dash, 0, sizeof(dash));

	for (i = 0; i < ATS_PER_PAGE; i++) {
		for (k = 0; k < 7; k++)
			cells[i][k] = NULL;
	}

	if (ats_db_prepare("SELECT id, name, host_url FROM apps "
	    "ORDER BY name COLLATE NOCASE, id", &stmt, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	while (n_apps < ATS_MAX_APPS && sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char	*txt;

		apps[n_apps].id = sqlite3_column_int64(stmt, 0);
		txt = sqlite3_column_text(stmt, 1);
		names[n_apps] = kore_strdup(txt == NULL ? "" : (const char *)txt);
		apps[n_apps].name = names[n_apps];
		txt = sqlite3_column_text(stmt, 2);
		hosts[n_apps] = kore_strdup(txt == NULL ? "" : (const char *)txt);
		apps[n_apps].host = hosts[n_apps];
		if (n_apps == 0)
			(void)snprintf(first_id, sizeof(first_id), "%lld",
			    apps[n_apps].id);
		n_apps++;
	}
	ats_db_finish(stmt);

	{
		char *seg = ats_segment(req, 2);

		if (canonical_app_id(seg, first_id, strlen(first_id), app_id,
		    sizeof(app_id)) == -1) {
			kore_free(seg);
			bad = 1;
			goto done;
		}
		kore_free(seg);
	}

	ats_utc_stamp(ats_day_start(0), ts_today, sizeof(ts_today));
	ats_utc_stamp(ats_day_start(1), ts_yesterday, sizeof(ts_yesterday));
	ats_utc_stamp(ats_day_start(6), ts_7, sizeof(ts_7));
	ats_utc_stamp(ats_day_start(29), ts_30, sizeof(ts_30));
	(void)snprintf(mod, sizeof(mod), "%+d minutes",
	    ats_tz_offset_min());

	v[0] = ATS_TXT(app_id);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ?", v, 1, &total, err, sizeof(err))
	    == KORE_RESULT_ERROR ||
	    ats_db_scalar("SELECT COUNT(*) FROM (SELECT 1 FROM apps_traffic "
	    "WHERE app_id = ? GROUP BY unique_visitors)", v, 1, &s.visitors,
	    err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	s.page_views = total;

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_today);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date >= ?", v, 2, &s.s_today,
	    err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_today);
	if (ats_db_scalar("SELECT COUNT(*) FROM (SELECT 1 FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date >= ? GROUP BY unique_visitors)",
	    v, 2, &s.uv_today, err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_today);
	v[2] = ATS_TXT(ts_yesterday);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date < ? AND visit_date >= ?",
	    v, 3, &s.s_yesterday, err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_7);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date >= ?", v, 2, &s.s_last_7_days,
	    err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_30);
	if (ats_db_scalar("SELECT COUNT(*) FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date >= ?", v, 2, &s.s_last_30_days,
	    err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	/* 14-day trend, zero-filled. */
	for (i = 0; i < TREND_DAYS; i++) {
		char stamp[32];

		ats_local_stamp(ats_day_start(TREND_DAYS - 1 - i), stamp,
		    sizeof(stamp));
		(void)snprintf(days[i].date, sizeof(days[i].date), "%.10s",
		    stamp);
		days[i].count = 0;
		(void)snprintf(day_dates[i], sizeof(day_dates[i]), "%.10s",
		    days[i].date);
	}
	n_days = TREND_DAYS;

	v[0] = ATS_TXT(mod);
	v[1] = ATS_TXT(app_id);
	ats_utc_stamp(ats_day_start(TREND_DAYS - 1), tmp, sizeof(tmp));
	v[2] = ATS_TXT(tmp);

	if (ats_db_prepare("SELECT date(visit_date, ?), COUNT(*) "
	    "FROM apps_traffic WHERE app_id = ? AND visit_date >= ? "
	    "GROUP BY 1 ORDER BY 1", &stmt, err, sizeof(err))
	    == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}
	(void)ats_bind_text(stmt, 1, mod);
	(void)ats_bind_text(stmt, 2, app_id);
	(void)ats_bind_text(stmt, 3, tmp);
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *d = sqlite3_column_text(stmt, 0);

		if (d == NULL)
			continue;
		for (i = 0; i < TREND_DAYS; i++) {
			if (!strcmp(days[i].date, (const char *)d)) {
				days[i].count = sqlite3_column_int64(stmt, 1);
				break;
			}
		}
	}
	ats_db_finish(stmt);

	v[0] = ATS_TXT(app_id);
	v[1] = ATS_TXT(ts_30);
	n_pages = load_top("SELECT page_url, COUNT(*) c FROM apps_traffic "
	    "WHERE app_id = ? AND visit_date >= ? AND page_url <> '' "
	    "GROUP BY page_url ORDER BY c DESC, page_url LIMIT ?",
	    (struct ats_val[]){ v[0], v[1], ATS_NUM(TOP_N) }, 3,
	    pages, page_labels, TOP_N);
	if (n_pages < 0)
		n_pages = 0;

	n_refs = load_top("SELECT CASE WHEN referring_host = '' "
	    "THEN '(direct)' ELSE referring_host END AS h, COUNT(*) c "
	    "FROM apps_traffic WHERE app_id = ? AND visit_date >= ? "
	    "GROUP BY h ORDER BY c DESC, h LIMIT ?",
	    (struct ats_val[]){ v[0], v[1], ATS_NUM(TOP_N) }, 3,
	    refs, ref_labels, TOP_N);
	if (n_refs < 0)
		n_refs = 0;

	n_countries = load_top("SELECT CASE WHEN country = '' "
	    "THEN 'unknown' ELSE country END AS c, COUNT(*) n "
	    "FROM apps_traffic WHERE app_id = ? AND visit_date >= ? "
	    "GROUP BY c ORDER BY n DESC, c LIMIT ?",
	    (struct ats_val[]){ v[0], v[1], ATS_NUM(TOP_N) }, 3,
	    country_items, country_labels, TOP_N);
	if (n_countries < 0)
		n_countries = 0;

	page_count = (total + ATS_PER_PAGE - 1) / ATS_PER_PAGE;
	page = ats_ci_get_page(req);
	cur = page > page_count ? page_count : page;
	offset = (cur > 0 ? cur - 1 : 0) * ATS_PER_PAGE;

	if (ats_db_prepare("SELECT page_url, referring_url, visitor_ip, "
	    "country, browser, operating_system, visit_date FROM apps_traffic "
	    "WHERE app_id = ? ORDER BY id DESC LIMIT ? OFFSET ?",
	    &stmt, err, sizeof(err)) == KORE_RESULT_ERROR) {
		bad = 1;
		goto done;
	}

	(void)ats_bind_text(stmt, 1, app_id);
	(void)ats_bind_int64(stmt, 2, ATS_PER_PAGE);
	(void)ats_bind_int64(stmt, 3, offset);

	while (n_rows < ATS_PER_PAGE && sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *txt;

		for (k = 0; k < 7; k++) {
			txt = sqlite3_column_text(stmt, k);
			cells[n_rows][k] = kore_strdup(txt == NULL ? "" :
			    (const char *)txt);
		}

		rows[n_rows].page_url = cells[n_rows][0];
		rows[n_rows].referring_url = cells[n_rows][1];
		rows[n_rows].visitor_ip = cells[n_rows][2];
		rows[n_rows].country = cells[n_rows][3];
		rows[n_rows].browser = cells[n_rows][4];
		rows[n_rows].operating_system = cells[n_rows][5];
		visit_time_local(cells[n_rows][6], times[n_rows],
		    sizeof(times[n_rows]));
		rows[n_rows].visit_time = times[n_rows];
		n_rows++;
	}
	ats_db_finish(stmt);

	dash.apps = apps;
	dash.n_apps = n_apps;
	dash.cur_app_id = app_id;
	dash.s = s;
	dash.days = days;
	dash.n_days = n_days;
	dash.pages = pages;
	dash.n_pages = n_pages;
	dash.refs = refs;
	dash.n_refs = n_refs;
	dash.countries = country_items;
	dash.n_countries = n_countries;
	dash.rows = rows;
	dash.n_rows = n_rows;
	dash.current_page = (int)cur;
	dash.page_count = (int)page_count;

	ats_sb_init(&sb);
	view_data_view(&sb, req, locale, &dash);
	ats_send_html(req, &sb);
	ats_sb_free(&sb);

done:
	if (bad != 0) {
		kore_log(LOG_ERR, "dv: %s", err);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
	}

	for (i = 0; i < n_apps; i++) {
		kore_free(names[i]);
		kore_free(hosts[i]);
	}
	for (i = 0; i < n_rows; i++) {
		for (k = 0; k < 7; k++)
			kore_free(cells[i][k]);
	}

	return (KORE_RESULT_OK);
}

int
ats_h_create(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	struct ats_sb	sb;

	ats_sb_init(&sb);
	view_app_form(&sb, req, locale, ats_i18n(locale, I18N_APP_NEW_TITLE), "/do_create", "", "", "", "", "");
	ats_send_html(req, &sb);
	ats_sb_free(&sb);

	return (KORE_RESULT_OK);
}

static int
parse_app_form(struct http_request *req, enum ats_locale locale,
    char *name, size_t name_len,
    char hosts[ATS_MAX_HOSTS][MAX_HOST], int *n_hosts, char *remark,
    size_t remark_len, char *host_input, size_t host_input_len,
    char *error, size_t error_len)
{
	char	*body;
	size_t	 blen;

	host_input[0] = '\0';
	name[0] = '\0';
	remark[0] = '\0';

	body = read_body(req, &blen);
	if (body == NULL || !is_form_request(req)) {
		kore_free(body);
		(void)snprintf(error, error_len, "%s",
		    ats_i18n(locale, I18N_ERR_FORM));
		return (-1);
	}

	(void)ats_form_get(body, blen, "app_name", name, name_len);
	(void)ats_form_get(body, blen, "app_hosts", host_input,
	    host_input_len);
	(void)ats_form_get(body, blen, "remark", remark, remark_len);
	kore_free(body);

	if (php_empty(name)) {
		(void)snprintf(error, error_len, "%s",
		    ats_i18n(locale, I18N_ERR_APP_NAME));
		return (-1);
	}

	if (normalize_hosts(host_input, hosts, n_hosts) == -1) {
		(void)snprintf(error, error_len, "%s",
		    ats_i18n(locale, I18N_ERR_HOSTS_EXAMPLE));
		return (-1);
	}

	return (0);
}

int
ats_h_do_create(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	if (same_origin(req) == -1) {
		send_error(req, locale, 403, ats_i18n(locale, I18N_ERR_TITLE_FORBIDDEN), ats_i18n(locale, I18N_ERR_MSG_ORIGIN));
		return (KORE_RESULT_OK);
	}

	char		 name[256], remark[512], error[256];
	char		 hosts[ATS_MAX_HOSTS][MAX_HOST];
	char		 host_input[2048];
	char		 now[32], err[256];
	uint64_t	 id;
	struct ats_val	 vals[7];
	struct ats_sb	 sb;
	int		 n_hosts, i, r;

	host_input[0] = '\0';
	name[0] = '\0';
	remark[0] = '\0';

	if (parse_app_form(req, locale, name, sizeof(name), hosts, &n_hosts, remark,
	    sizeof(remark), host_input, sizeof(host_input), error,
	    sizeof(error)) == -1) {
		ats_sb_init(&sb);
		view_app_form(&sb, req, locale, ats_i18n(locale, I18N_APP_NEW_TITLE), "/do_create", "", name, host_input,
		    remark, error);
		http_response_header(req, "content-type",
		    "text/html; charset=UTF-8");
		http_response_header(req, "cache-control", "no-store");
		http_response(req, 400, sb.p, sb.len);
		ats_sb_free(&sb);
		return (KORE_RESULT_OK);
	}

	for (i = 0; i < n_hosts; i++) {
		r = host_taken("0", hosts[i], err, sizeof(err));
		if (r == -1) {
			send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
			return (KORE_RESULT_OK);
		}
		if (r == 1) {
			ats_sb_init(&sb);
			view_app_form(&sb, req, locale, ats_i18n(locale, I18N_APP_NEW_TITLE), "/do_create", "", name,
			    "", remark, ats_i18n(locale, I18N_ERR_HOST_TAKEN));
			http_response_header(req, "content-type",
			    "text/html; charset=UTF-8");
			http_response_header(req, "cache-control", "no-store");
			http_response(req, 409, sb.p, sb.len);
			ats_sb_free(&sb);
			return (KORE_RESULT_OK);
		}
	}

	id = ats_next_id();
	ats_now(now, sizeof(now));

	vals[0] = ATS_NUM((sqlite3_int64)id);
	vals[1] = ATS_TXT(name);
	vals[2] = ATS_TXT(hosts[0]);
	vals[3] = ATS_TXT(hosts[1]);
	vals[4] = ATS_TXT(hosts[2]);
	vals[5] = ATS_TXT(remark);
	vals[6] = ATS_TXT(now);

	if (ats_db_run("INSERT INTO apps (id, name, host_url, host_url_01, "
	    "host_url_02, remark, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
	    vals, 7, err, sizeof(err)) == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "do_create insert(%llu): %s",
		    (unsigned long long)id, err);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_CREATE_APP));
		return (KORE_RESULT_OK);
	}

	ats_sb_init(&sb);
	view_create_success(&sb, req, locale, id, name, hosts[0]);
	ats_send_html(req, &sb);
	ats_sb_free(&sb);

	return (KORE_RESULT_OK);
}

int
ats_h_edit(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char		*id, name[256], hosts[768], remark[512], err[256];
	struct ats_sb	 sb;

	id = ats_segment(req, 2);
	{
		int exists = 0;

		if (app_exists(id, &exists, err, sizeof(err)) == -1) {
			kore_free(id);
			send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
			return (KORE_RESULT_OK);
		}
		if (!exists) {
			kore_free(id);
			send_error(req, locale, 404, ats_i18n(locale, I18N_ERR_TITLE_NOT_FOUND), ats_i18n(locale, I18N_ERR_APP_NOT_FOUND));
			return (KORE_RESULT_OK);
		}
	}

	if (app_get(id, name, sizeof(name), hosts, sizeof(hosts), remark,
	    sizeof(remark), err, sizeof(err)) == -1) {
		kore_free(id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	ats_sb_init(&sb);
	view_app_form(&sb, req, locale, ats_i18n(locale, I18N_APP_EDIT_TITLE), "/do_update", id, name,
	    hosts, remark, "");
	ats_send_html(req, &sb);
	ats_sb_free(&sb);
	kore_free(id);

	return (KORE_RESULT_OK);
}

int
ats_h_do_update(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	if (same_origin(req) == -1) {
		send_error(req, locale, 403, ats_i18n(locale, I18N_ERR_TITLE_FORBIDDEN), ats_i18n(locale, I18N_ERR_MSG_ORIGIN));
		return (KORE_RESULT_OK);
	}

	char		 name[256], remark[512], error[512], err[256];
	char		 hosts[ATS_MAX_HOSTS][MAX_HOST];
	char		 id[32], host_input[2048];
	char		*body;
	size_t		 blen;
	struct ats_val	 vals[6];
	struct ats_sb	 sb;
	int		 n_hosts, i, r;

	error[0] = '\0';
	id[0] = '\0';
	host_input[0] = '\0';
	name[0] = '\0';
	remark[0] = '\0';

	body = read_body(req, &blen);
	if (body == NULL || !is_form_request(req)) {
		kore_free(body);
		send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_FORM));
		return (KORE_RESULT_OK);
	}

	(void)ats_form_get(body, blen, "id", id, sizeof(id));
	(void)ats_form_get(body, blen, "app_name", name, sizeof(name));
	(void)ats_form_get(body, blen, "app_hosts", host_input,
	    sizeof(host_input));
	(void)ats_form_get(body, blen, "remark", remark, sizeof(remark));
	kore_free(body);

	if (php_empty(id)) {
		send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_MISSING_ID));
		return (KORE_RESULT_OK);
	}

	if (php_empty(name)) {
		(void)snprintf(error, sizeof(error), "%s",
		    ats_i18n(locale, I18N_ERR_APP_NAME));
		goto invalid;
	}

	if (normalize_hosts(host_input, hosts, &n_hosts) == -1) {
		(void)snprintf(error, sizeof(error), "%s",
		    ats_i18n(locale, I18N_ERR_HOSTS));
		goto invalid;
	}

	for (i = 0; i < n_hosts; i++) {
		r = host_taken(id, hosts[i], err, sizeof(err));
		if (r == -1) {
			send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
			return (KORE_RESULT_OK);
		}
		if (r == 1) {
			(void)snprintf(error, sizeof(error),
			    ats_i18n(locale, I18N_ERR_HOST_TAKEN_FMT),
			    hosts[i]);
			goto invalid;
		}
	}

	vals[0] = ATS_TXT(name);
	vals[1] = ATS_TXT(hosts[0]);
	vals[2] = ATS_TXT(hosts[1]);
	vals[3] = ATS_TXT(hosts[2]);
	vals[4] = ATS_TXT(remark);
	vals[5] = ATS_TXT(id);

	if (ats_db_run("UPDATE apps SET name = ?, host_url = ?, "
	    "host_url_01 = ?, host_url_02 = ?, remark = ? WHERE id = ?",
	    vals, 6, err, sizeof(err)) == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "do_update(%s): %s", id, err);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_UPDATE_APP));
		return (KORE_RESULT_OK);
	}

	{
		struct ats_sb url;

		ats_sb_init(&url);
		ats_sb_adds(&url, "/dv/");
		ats_sb_adds(&url, id);
		redirect_to(req, 303, ats_sb_cstr(&url));
		ats_sb_free(&url);
	}

	return (KORE_RESULT_OK);

invalid:
	ats_sb_init(&sb);
	view_app_form(&sb, req, locale, ats_i18n(locale, I18N_APP_EDIT_TITLE), "/do_update", id, name, host_input,
	    remark, error);
	http_response_header(req, "content-type", "text/html; charset=UTF-8");
	http_response_header(req, "cache-control", "no-store");
	http_response(req, 400, sb.p, sb.len);
	ats_sb_free(&sb);

	return (KORE_RESULT_OK);
}

int
ats_h_do_delete(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	if (same_origin(req) == -1) {
		send_error(req, locale, 403, ats_i18n(locale, I18N_ERR_TITLE_FORBIDDEN), ats_i18n(locale, I18N_ERR_MSG_ORIGIN));
		return (KORE_RESULT_OK);
	}

	char		*body, *id, *confirm, err[256];
	size_t		 blen;
	struct ats_val	 vals[1];

	body = read_body(req, &blen);
	if (body == NULL || !is_form_request(req)) {
		kore_free(body);
		send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_FORM));
		return (KORE_RESULT_OK);
	}

	id = request_var(req, body, blen, "id", 32);
	confirm = request_var(req, body, blen, "confirm", 8);
	kore_free(body);

	if (strcmp(confirm, "yes") != 0 || php_empty(id)) {
		kore_free(id);
		kore_free(confirm);
		send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_CONFIRM));
		return (KORE_RESULT_OK);
	}

	vals[0] = ATS_TXT(id);
	(void)ats_db_run("DELETE FROM apps_traffic WHERE app_id = ?", vals, 1,
	    err, sizeof(err));
	if (ats_db_run("DELETE FROM apps WHERE id = ?", vals, 1, err,
	    sizeof(err)) == KORE_RESULT_ERROR)
		kore_log(LOG_ERR, "do_delete(%s): %s", id, err);
	else
		kore_log(LOG_INFO, "deleted app %s", id);

	kore_free(id);
	kore_free(confirm);

	redirect_to(req, 303, "/dv");

	return (KORE_RESULT_OK);
}

int
ats_h_login(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char		ref[QV_LEN];
	struct ats_sb	sb;

	ref[0] = '\0';
	(void)ats_query_get(req, "ref", ref, sizeof(ref));

	ats_sb_init(&sb);
	view_login(&sb, locale, ref, "");
	ats_send_html(req, &sb);
	ats_sb_free(&sb);

	return (KORE_RESULT_OK);
}

int
ats_h_do_login(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	if (same_origin(req) == -1) {
		send_error(req, locale, 403, ats_i18n(locale, I18N_ERR_TITLE_FORBIDDEN), ats_i18n(locale, I18N_ERR_MSG_ORIGIN));
		return (KORE_RESULT_OK);
	}

	char		 ip[64], err[256];
	char		*body, *user, *pwd, *ref, *token, *exp;
	char		 juser[4096], jpwd[4096], jref[QV_LEN];
	char		 cookie[8192];
	size_t		 blen;
	time_t		 now;
	const char	*hash;
	int		 is_json, ok;
	cJSON		*root;
	struct ats_sb	 sb;

	body = read_body(req, &blen);
	is_json = (body != NULL && ct_contains(req, "application/json"));

	root = NULL;
	user = NULL;
	pwd = NULL;
	ref = NULL;

	if (is_json) {
		root = cJSON_ParseWithLength(body, blen);
		if (root == NULL || !cJSON_IsObject(root)) {
			if (root != NULL)
				cJSON_Delete(root);
			kore_free(body);
			send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_MSG_BAD_JSON));
			return (KORE_RESULT_OK);
		}

		if (json_scalar(root, "username", juser, sizeof(juser), 0) == -1 ||
		    json_scalar(root, "password", jpwd, sizeof(jpwd), 0) == -1 ||
		    json_scalar(root, "ref", jref, sizeof(jref), 1) == -1) {
			cJSON_Delete(root);
			kore_free(body);
			send_error(req, locale, 400, ats_i18n(locale, I18N_ERR_TITLE_BAD_REQUEST), ats_i18n(locale, I18N_ERR_MSG_CREDENTIALS_REQUIRED));
			return (KORE_RESULT_OK);
		}
		cJSON_Delete(root);

		user = kore_strdup(juser);
		pwd = kore_strdup(jpwd);
		ref = kore_strdup(jref);
	} else {
		user = request_var(req, body, blen, "username", 4096);
		pwd = request_var(req, body, blen, "password", 4096);
		ref = request_var(req, body, blen, "ref", QV_LEN);
	}
	kore_free(body);

	/*
	 * The login form's ref is attacker-controlled. Keep it a same-site path;
	 * CR/LF would otherwise reach the Location header through a fragment.
	 */
	if (ref[0] != '/' || ref[1] == '/' || ref[1] == '\\' ||
	    strchr(ref, '\r') != NULL || strchr(ref, '\n') != NULL)
		ref[0] = '\0';

	hash = ats_admin_hash();
	if (hash == NULL || *hash == '\0') {
		kore_free(user);
		kore_free(pwd);
		kore_free(ref);
		send_error(req, locale, 503, ats_i18n(locale, I18N_ERR_LOGIN_DISABLED_TITLE), ats_i18n(locale, I18N_ERR_LOGIN_DISABLED_MSG));
		return (KORE_RESULT_OK);
	}

	ats_client_ip(req, ip, sizeof(ip));
	if (!ats_throttle_allow("login", ip, LOGIN_RATE_CAP,
	    LOGIN_RATE_PERIOD)) {
		kore_log(LOG_WARNING, "login throttled for %s", ip);
		ats_sb_init(&sb);
		view_login(&sb, locale, ref, ats_i18n(locale, I18N_ERR_TOO_MANY));
		http_response_header(req, "content-type",
		    "text/html; charset=UTF-8");
		http_response_header(req, "cache-control", "no-store");
		http_response(req, 429, sb.p, sb.len);
		ats_sb_free(&sb);
		kore_free(user);
		kore_free(pwd);
		kore_free(ref);
		return (KORE_RESULT_OK);
	}

	ok = (!php_empty(user) && constant_time_eq(user, ats_admin_user()) &&
	    constant_time_eq(pwd, hash));

	if (!ok) {
		kore_log(LOG_WARNING, "login failed for %s from %s", user, ip);
		ats_sb_init(&sb);
		view_login(&sb, locale, ref, ats_i18n(locale, I18N_ERR_BAD_CREDENTIALS));
		http_response_header(req, "content-type",
		    "text/html; charset=UTF-8");
		http_response_header(req, "cache-control", "no-store");
		http_response(req, 401, sb.p, sb.len);
		ats_sb_free(&sb);
		kore_free(user);
		kore_free(pwd);
		kore_free(ref);
		return (KORE_RESULT_OK);
	}

	now = time(NULL);
	token = ats_jwt_sign(ats_admin_user(), (long long)now,
	    (long long)now + ats_session_seconds());
	if (token == NULL) {
		kore_free(user);
		kore_free(pwd);
		kore_free(ref);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_SESSION));
		return (KORE_RESULT_OK);
	}

	exp = kore_malloc(64);
	ats_gmt_stamp(now + ats_session_seconds(), exp, 64);
	(void)snprintf(cookie, sizeof(cookie),
	    "auth_jwt=%s; Expires=%s; Max-Age=%d; Path=/; Domain=%s%s; "
	    "HttpOnly; SameSite=Lax", token, exp, ats_session_seconds(),
	    ats_cookie_domain(),
	    strncmp(ats_base_url(), "https://", 8) == 0 ? "; Secure" : "");
	http_response_header(req, "set-cookie", cookie);
	kore_free(exp);
	kore_free(token);

	if (php_empty(ref))
		ref[0] = '\0';

	{
		struct ats_sb url;

		ats_sb_init(&url);
		ats_sb_adds(&url, ref[0] != '\0' ? ref : "/dv");
		redirect_to(req, 303, ats_sb_cstr(&url));
		ats_sb_free(&url);
	}

	kore_log(LOG_INFO, "login ok for %s from %s", user, ip);

	kore_free(user);
	kore_free(pwd);
	kore_free(ref);

	(void)err;
	return (KORE_RESULT_OK);
}

static void
sb_csv_field(struct ats_sb *sb, const char *s)
{
	ats_sb_adds(sb, "\"");

	/*
	 * Spreadsheet formula injection: a quoted field that starts with one of
	 * these characters may still be evaluated by Excel/LibreOffice. Prefix it
	 * with a single quote so the exported data is always text.
	 */
	if (s != NULL && (*s == '=' || *s == '+' || *s == '-' || *s == '@' ||
	    *s == '\t' || *s == '\r'))
		ats_sb_adds(sb, "'");

	for (; s != NULL && *s != '\0'; s++) {
		if (*s == '"')
			ats_sb_adds(sb, "\"\"");
		else
			ats_sb_add(sb, s, 1);
	}
	ats_sb_adds(sb, "\"");
}

int
ats_h_export(struct http_request *req)
{
	enum ats_locale locale = ats_locale_for_request(req);

	set_locale_header(req, locale);

	char		 err[256], header[256];
	char		*app_id;
	struct ats_sb	 sb;
	sqlite3_stmt	*stmt;
	int		 exists, n;

	app_id = ats_segment(req, 2);
	if (app_exists(app_id, &exists, err, sizeof(err)) == -1 ||
	    !exists) {
		kore_free(app_id);
		send_error(req, locale, 404, ats_i18n(locale, I18N_ERR_TITLE_NOT_FOUND), ats_i18n(locale, I18N_ERR_APP_NOT_FOUND));
		return (KORE_RESULT_OK);
	}

	if (ats_db_prepare("SELECT visit_date, page_url, referring_url, "
	    "visitor_ip, country, device_type, browser, operating_system, "
	    "unique_visitors, record_status FROM apps_traffic "
	    "WHERE app_id = ? ORDER BY id DESC LIMIT 100000", &stmt, err,
	    sizeof(err)) == KORE_RESULT_ERROR) {
		kore_log(LOG_ERR, "export(%s): %s", app_id, err);
		kore_free(app_id);
		send_error(req, locale, 500, ats_i18n(locale, I18N_ERR_TITLE_SERVER), ats_i18n(locale, I18N_ERR_MSG_DB));
		return (KORE_RESULT_OK);
	}

	ats_sb_init(&sb);
	ats_sb_adds(&sb, "\xEF\xBB\xBF");
	ats_sb_adds(&sb, ats_i18n(locale, I18N_CSV_HEADER));
	ats_sb_adds(&sb, "\n");

	(void)ats_bind_text(stmt, 1, app_id);
	n = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *txt;
		int			i;

		for (i = 0; i < 10; i++) {
			const char *val;

			if (i > 0)
				ats_sb_adds(&sb, ",");
			txt = sqlite3_column_text(stmt, i);
			val = txt == NULL ? "" : (const char *)txt;

			if (i == 5) {
				if (!strcmp(val, "pc"))
					val = ats_i18n(locale, I18N_DEVICE_PC);
				else if (!strcmp(val, "mobile"))
					val = ats_i18n(locale, I18N_DEVICE_MOBILE);
				else if (!strcmp(val, "robot"))
					val = ats_i18n(locale, I18N_DEVICE_ROBOT);
			} else if ((i == 6 || i == 7) &&
			    (*val == '\0' || !strcmp(val, "Unknown Platform"))) {
				val = ats_i18n(locale, I18N_UNKNOWN);
			}

			sb_csv_field(&sb, val);
		}
		ats_sb_adds(&sb, "\n");
		n++;
	}
	ats_db_finish(stmt);

	(void)snprintf(header, sizeof(header),
	    "attachment; filename=\"ats-%s.csv\"", app_id);
	http_response_header(req, "content-type", "text/csv; charset=UTF-8");
	http_response_header(req, "content-disposition", header);
	http_response_header(req, "cache-control", "no-store");
	http_response_header(req, "x-content-type-options", "nosniff");
	http_response(req, 200, sb.p, sb.len);
	ats_sb_free(&sb);

	kore_log(LOG_INFO, "exported %d rows for app %s", n, app_id);
	kore_free(app_id);

	return (KORE_RESULT_OK);
}

int
ats_h_lang(struct http_request *req)
{
	enum ats_locale	cur, loc;
	char		*code;
	char		 ref[1024], cookie[256];
	int		 secure;

	cur = ats_locale_for_request(req);
	code = ats_segment(req, 2);
	loc = ats_locale_from_code(code);
	kore_free(code);

	if (loc < 0) {
		send_error(req, cur, 404,
		    ats_i18n(cur, I18N_ERR_TITLE_NOT_FOUND),
		    ats_i18n(cur, I18N_ERR_MSG_NOT_FOUND));
		return (KORE_RESULT_OK);
	}

	ref[0] = '\0';
	(void)ats_query_get(req, "ref", ref, sizeof(ref));
	if (ref[0] != '/' || ref[1] == '/' || ref[1] == '\\' ||
	    strchr(ref, '\r') != NULL || strchr(ref, '\n') != NULL)
		(void)snprintf(ref, sizeof(ref), "/dv");

	secure = strncmp(ats_base_url(), "https://", 8) == 0;
	(void)snprintf(cookie, sizeof(cookie),
	    "ats_lang=%s; Path=/; Max-Age=31536000; SameSite=Lax%s",
	    ats_locale_code(loc), secure ? "; Secure" : "");
	http_response_header(req, "set-cookie", cookie);
	ats_send_redirect(req, 303, ref);

	return (KORE_RESULT_OK);
}

int
ats_h_logout(struct http_request *req)
{
	enum ats_locale	locale = ats_locale_for_request(req);
	char		cookie[512];

	set_locale_header(req, locale);

	if (same_origin(req) == -1) {
		send_error(req, locale, 403,
		    ats_i18n(locale, I18N_ERR_TITLE_FORBIDDEN),
		    ats_i18n(locale, I18N_ERR_MSG_ORIGIN));
		return (KORE_RESULT_OK);
	}

	(void)snprintf(cookie, sizeof(cookie),
	    "auth_jwt=; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Max-Age=0; "
	    "Path=/; Domain=%s%s; HttpOnly; SameSite=Lax",
	    ats_cookie_domain(),
	    strncmp(ats_base_url(), "https://", 8) == 0 ? "; Secure" : "");
	http_response_header(req, "set-cookie", cookie);
	redirect_to(req, 303, "/login");

	return (KORE_RESULT_OK);
}
