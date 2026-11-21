/*
 * Single-binary entry point.
 *
 * Kore calls kore_parent_configure() before kore_parse_config(), so the
 * environment variables referenced by conf/ats-c.conf ($ATS_CAPI_PID,
 * $ATS_CAPI_BODY_TMP) must be set here. Paths are derived from the location
 * of the executable instead of the working directory, because kore chroots
 * into worker_privsep.root after the config is parsed.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kore/kore.h>
#include <kore/hooks.h>
#include <kore/seccomp.h>

#include "ats.h"
#include "db.h"
#include "snowflake.h"
#include "throttle.h"
#include "ua.h"

/*
 * Kore's worker filter predates SQLite's use of positional I/O and WAL
 * syncing. Allow the minimum set the pager needs; everything else keeps the
 * default-deny policy.
 */
KORE_SECCOMP_FILTER("ats-sqlite",
#if defined(SYS_pread64)
	KORE_SYSCALL_ALLOW(pread64),
#endif
#if defined(SYS_pwrite64)
	KORE_SYSCALL_ALLOW(pwrite64),
#endif
#if defined(SYS_ftruncate)
	KORE_SYSCALL_ALLOW(ftruncate),
#endif
#if defined(SYS_fsync)
	KORE_SYSCALL_ALLOW(fsync),
#endif
#if defined(SYS_fdatasync)
	KORE_SYSCALL_ALLOW(fdatasync),
#endif
#if defined(SYS_newfstatat)
	KORE_SYSCALL_ALLOW(newfstatat),
#endif
#if defined(SYS_statx)
	KORE_SYSCALL_ALLOW(statx),
#endif
#if defined(SYS_mremap)
	KORE_SYSCALL_ALLOW(mremap),
#endif
)

static int
install_root(char *out, size_t out_len)
{
	char	 exe[PATH_MAX];
	ssize_t	 n;
	size_t	 len, i;
	int	 slashes;

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n == -1)
		return (-1);
	exe[n] = '\0';

	/* Strip "/bin/<prog>" so the root is the directory above bin/. */
	len = strlen(exe);
	slashes = 0;
	for (i = len; i > 0; i--) {
		if (exe[i - 1] != '/')
			continue;
		if (++slashes == 2) {
			exe[i - 1] = '\0';
			len = i - 1;
			break;
		}
	}
	if (slashes < 2 || len == 0)
		return (-1);

	if (len >= out_len)
		return (-1);

	memcpy(out, exe, len + 1);

	return (0);
}

static void
set_env_default(const char *root, const char *name, const char *rel)
{
	char	path[PATH_MAX];
	int	len;

	if (getenv(name) != NULL && *getenv(name) != '\0')
		return;

	len = snprintf(path, sizeof(path), "%s/%s", root, rel);
	if (len == -1 || (size_t)len >= sizeof(path))
		fatal("env path too long: %s", name);

	if (setenv(name, path, 1) == -1)
		fatal("setenv(%s): %s", name, errno_s);
}

static void
make_dir(const char *root, const char *rel, mode_t mode)
{
	char	path[PATH_MAX];
	int	len;

	len = snprintf(path, sizeof(path), "%s/%s", root, rel);
	if (len == -1 || (size_t)len >= sizeof(path))
		fatal("dir path too long: %s", rel);

	if (mkdir(path, mode) == -1 && errno != EEXIST)
		fatal("mkdir(%s): %s", path, errno_s);
}

void
kore_parent_configure(int argc, char **argv)
{
	char	root[PATH_MAX];

	kore_default_getopt(argc, argv);

	if (install_root(root, sizeof(root)) == -1)
		fatal("cannot determine install root from /proc/self/exe");

	set_env_default(root, "ATS_DATA_DIR", "data");
	set_env_default(root, "ATS_DB_PATH", "data/ats.db");
	set_env_default(root, "ATS_GEOIP_DB_PATH",
	    "data/GeoLite2-Country.mmdb");
	set_env_default(root, "ATS_CAPI_PID", "log/ats-capi.pid");
	set_env_default(root, "ATS_CAPI_BODY_TMP", "data/body");

	make_dir(root, "data", 0750);
	make_dir(root, "log", 0750);
	make_dir(root, "data/body", 0700);

	if (ats_config_init() == -1)
		fatal("config: invalid runtime configuration");

	{
		char err[256];

		if (ats_db_bootstrap(getenv("ATS_DB_PATH"), err,
		    sizeof(err)) == KORE_RESULT_ERROR)
			fatal("database: %s", err);
	}

	/*
	 * The shared tables are inherited by the workers, so they must exist
	 * before kore forks them.
	 */
	if (ats_snowflake_init() == -1)
		fatal("snowflake: init failed");
	if (ats_throttle_init() == -1)
		fatal("throttle: init failed");
	if (ats_ua_init() == -1)
		fatal("ua: init failed");
}