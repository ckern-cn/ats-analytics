/*
 * Runtime configuration.
 *
 * Every value comes from the environment so the systemd unit can keep secrets
 * out of the binary.  The only file fallback is the JWT signing key: if
 * ATS_JWT_KEY is not set, a random key is created once in data/jwt.key
 * (mode 0600) and reused, so sessions survive a restart.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <kore/kore.h>

#include "ats.h"
#include "util.h"

static char	base_url[512];
static char	base_host[256];
static char	cookie_domain[256];
static char	admin_user[128];
static char	admin_hash[65];
static char	jwt_key[512];
static int	tz_offset = ATS_TZ_OFFSET_DEFAULT;
static int	session_seconds = ATS_SESSION_SECONDS_DEFAULT;
static int	retention_days = ATS_RETENTION_DAYS_DEFAULT;
static int	anonymize_ip = 0;
static int	respect_dnt = 1;

static const char *
env_str(const char *name)
{
	const char	*v;

	v = getenv(name);
	if (v == NULL || *v == '\0')
		return (NULL);

	return (v);
}

static int
env_int(const char *name, int def, int min, int max)
{
	const char	*v;
	char		*end;
	long		 n;

	if ((v = env_str(name)) == NULL)
		return (def);

	errno = 0;
	n = strtol(v, &end, 10);
	if (errno != 0 || end == v || *end != '\0' || n < min || n > max) {
		kore_log(LOG_WARNING, "config: ignoring invalid %s=%s", name, v);
		return (def);
	}

	return ((int)n);
}

static int
env_bool(const char *name, int def)
{
	const char	*v;

	if ((v = env_str(name)) == NULL)
		return (def);

	if (!strcasecmp(v, "1") || !strcasecmp(v, "true") ||
	    !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
		return (1);
	if (!strcasecmp(v, "0") || !strcasecmp(v, "false") ||
	    !strcasecmp(v, "no") || !strcasecmp(v, "off"))
		return (0);

	kore_log(LOG_WARNING, "config: ignoring invalid %s=%s", name, v);
	return (def);
}

static int
copy_str(char *out, size_t out_len, const char *in)
{
	size_t	len;

	len = strlen(in);
	if (len >= out_len)
		return (-1);

	memcpy(out, in, len + 1);

	return (0);
}

static void
strip_trailing_slash(char *s)
{
	size_t	len;

	len = strlen(s);
	while (len > 0 && s[len - 1] == '/')
		s[--len] = '\0';
}

/* Extract the host from the base URL for same-origin checks. */
static void
parse_base_host(void)
{
	const char	*p, *start, *end;
	size_t		 n;

	base_host[0] = '\0';

	p = strstr(base_url, "://");
	start = (p == NULL) ? base_url : p + 3;
	end = start + strcspn(start, "/:?#");

	n = (size_t)(end - start);
	if (n == 0 || n >= sizeof(base_host))
		return;

	memcpy(base_host, start, n);
	base_host[n] = '\0';

	for (p = base_host; *p != '\0'; p++) {
		if (*p >= 'A' && *p <= 'Z')
			*((char *)p) = (char)(*p - 'A' + 'a');
	}
}

static int
is_hex64(const char *s)
{
	size_t	i;

	if (s == NULL || strlen(s) != 64)
		return (0);

	for (i = 0; i < 64; i++) {
		if (!((s[i] >= '0' && s[i] <= '9') ||
		    (s[i] >= 'a' && s[i] <= 'f') ||
		    (s[i] >= 'A' && s[i] <= 'F')))
			return (0);
	}

	return (1);
}

static int
hex_encode(const unsigned char *in, size_t len, char *out, size_t out_len)
{
	static const char hex[] = "0123456789abcdef";
	size_t		  i;

	if (out_len < len * 2 + 1)
		return (-1);

	for (i = 0; i < len; i++) {
		out[i * 2] = hex[in[i] >> 4];
		out[i * 2 + 1] = hex[in[i] & 0x0f];
	}
	out[len * 2] = '\0';

	return (0);
}

static int
read_jwt_key_file(const char *path)
{
	int	fd;
	char	buf[256];
	ssize_t	n;
	size_t	len;

	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd == -1)
		return (-1);

	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return (-1);

	buf[n] = '\0';
	len = strlen(buf);
	while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
	    buf[len - 1] == ' ' || buf[len - 1] == '\t'))
		buf[--len] = '\0';

	if (len < 16)
		return (-1);

	return (copy_str(jwt_key, sizeof(jwt_key), buf));
}

static int
create_jwt_key_file(const char *path)
{
	unsigned char	raw[32];
	char		hex[65];
	int		fd;
	ssize_t		n;
	size_t		off;

	if (ats_random_bytes(raw, sizeof(raw)) == -1)
		return (-1);
	if (hex_encode(raw, sizeof(raw), hex, sizeof(hex)) == -1)
		return (-1);

	fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
	if (fd == -1) {
		if (errno == EEXIST)
			return (read_jwt_key_file(path));
		return (-1);
	}

	off = 0;
	while (off < strlen(hex)) {
		n = write(fd, hex + off, strlen(hex) - off);
		if (n <= 0) {
			close(fd);
			return (-1);
		}
		off += (size_t)n;
	}
	if (write(fd, "\n", 1) != 1) {
		close(fd);
		return (-1);
	}
	close(fd);

	return (copy_str(jwt_key, sizeof(jwt_key), hex));
}

static int
load_jwt_key(void)
{
	const char	*v, *dir;
	char		 path[PATH_MAX];
	int		 len;

	if ((v = env_str("ATS_JWT_KEY")) != NULL) {
		if (strlen(v) < 16) {
			kore_log(LOG_ERR, "config: ATS_JWT_KEY is too short");
			return (-1);
		}
		return (copy_str(jwt_key, sizeof(jwt_key), v));
	}

	dir = env_str("ATS_DATA_DIR");
	if (dir == NULL)
		dir = ".";

	len = snprintf(path, sizeof(path), "%s/jwt.key", dir);
	if (len < 0 || (size_t)len >= sizeof(path))
		return (-1);

	if (read_jwt_key_file(path) == 0)
		return (0);

	if (create_jwt_key_file(path) == 0) {
		kore_log(LOG_INFO, "config: generated %s", path);
		return (0);
	}

	kore_log(LOG_ERR, "config: cannot load or create %s: %s", path,
	    errno_s);

	return (-1);
}

int
ats_config_init(void)
{
	const char	*v;
	char		 hash[65];

	/* Base URL. */
	if ((v = env_str("ATS_BASE_URL")) == NULL)
		v = ATS_BASE_URL_DEFAULT;
	if (strncmp(v, "http://", 7) != 0 && strncmp(v, "https://", 8) != 0) {
		kore_log(LOG_ERR, "config: ATS_BASE_URL must be http(s)://...");
		return (-1);
	}
	if (copy_str(base_url, sizeof(base_url), v) == -1)
		return (-1);
	strip_trailing_slash(base_url);
	parse_base_host();

	/* Cookie domain. */
	if ((v = env_str("ATS_COOKIE_DOMAIN")) == NULL)
		v = ATS_COOKIE_DOMAIN_DEFAULT;
	if (copy_str(cookie_domain, sizeof(cookie_domain), v) == -1)
		return (-1);

	/* Admin user. */
	if ((v = env_str("ATS_ADMIN_USER")) == NULL)
		v = "admin";
	if (copy_str(admin_user, sizeof(admin_user), v) == -1)
		return (-1);

	/* Admin password: plaintext, precomputed digest, or disabled. */
	admin_hash[0] = '\0';
	if ((v = env_str("ATS_ADMIN_PASSWORD")) != NULL) {
		ats_sha256_hex(v, hash);
		memcpy(admin_hash, hash, sizeof(admin_hash));
	} else if ((v = env_str("ATS_ADMIN_PASSWORD_SHA256")) != NULL) {
		if (!is_hex64(v)) {
			kore_log(LOG_ERR,
			    "config: ATS_ADMIN_PASSWORD_SHA256 must be 64 hex");
			return (-1);
		}
		if (copy_str(admin_hash, sizeof(admin_hash), v) == -1)
			return (-1);
	}

	if (admin_hash[0] == '\0')
		kore_log(LOG_WARNING, "config: admin login is disabled "
		    "(set ATS_ADMIN_PASSWORD or ATS_ADMIN_PASSWORD_SHA256)");

	if (load_jwt_key() == -1)
		return (-1);

	tz_offset = env_int("ATS_TZ_OFFSET_MIN", ATS_TZ_OFFSET_DEFAULT,
	    -840, 840);
	session_seconds = env_int("ATS_SESSION_SECONDS",
	    ATS_SESSION_SECONDS_DEFAULT, 300, 2592000);
	retention_days = env_int("ATS_RETENTION_DAYS",
	    ATS_RETENTION_DAYS_DEFAULT, 7, 3650);
	anonymize_ip = env_bool("ATS_ANONYMIZE_IP", 0);
	respect_dnt = env_bool("ATS_RESPECT_DNT", 1);

	kore_log(LOG_INFO, "config: base_url=%s cookie_domain=%s tz=%+dmin "
	    "session=%ds retention=%dd admin=%s anonymize_ip=%d respect_dnt=%d",
	    base_url, cookie_domain, tz_offset, session_seconds,
	    retention_days, admin_user, anonymize_ip, respect_dnt);

	return (0);
}

const char *
ats_base_url(void)
{
	return (base_url[0] != '\0' ? base_url : ATS_BASE_URL_DEFAULT);
}

const char *
ats_base_url_host(void)
{
	return (base_host);
}

const char *
ats_cookie_domain(void)
{
	return (cookie_domain[0] != '\0' ? cookie_domain :
	    ATS_COOKIE_DOMAIN_DEFAULT);
}

const char *
ats_admin_user(void)
{
	return (admin_user);
}

const char *
ats_admin_hash(void)
{
	return (admin_hash);
}

const char *
ats_jwt_key(void)
{
	return (jwt_key);
}

int
ats_tz_offset_min(void)
{
	return (tz_offset);
}

int
ats_session_seconds(void)
{
	return (session_seconds);
}

int
ats_retention_days(void)
{
	return (retention_days);
}

int
ats_anonymize_enabled(void)
{
	return (anonymize_ip);
}

int
ats_respect_dnt(void)
{
	return (respect_dnt);
}
