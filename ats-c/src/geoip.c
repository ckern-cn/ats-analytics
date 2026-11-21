#include <sys/stat.h>
#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <maxminddb.h>
#include <kore/kore.h>

#include "geoip.h"

static MMDB_s	 mmdb;
static int	 opened;
static time_t	 last_check;
static off_t	 last_size;
static time_t	 last_mtime;

static void
remember_stat(const struct stat *st)
{
	last_size = st->st_size;
	last_mtime = st->st_mtime;
	last_check = time(NULL);
}

static int
open_db(void)
{
	const char	*path;
	struct stat	 st;
	int		 status;

	if (opened)
		return (0);

	path = getenv("ATS_GEOIP_DB_PATH");
	if (path == NULL) {
		kore_log(LOG_ERR, "geoip: ATS_GEOIP_DB_PATH is not set");
		return (-1);
	}

	status = MMDB_open(path, MMDB_MODE_MMAP, &mmdb);
	if (status != MMDB_SUCCESS) {
		kore_log(LOG_ERR, "geoip: cannot open %s: %s", path,
		    MMDB_strerror(status));
		return (-1);
	}

	opened = 1;
	if (stat(path, &st) == 0)
		remember_stat(&st);
	else
		last_check = time(NULL);

	return (0);
}

/*
 * Pick up a GeoIP database that was replaced on disk without restarting the
 * service. Each worker checks at most once a minute. A failed reload keeps
 * the previous mapping in place.
 */
static void
maybe_reload(void)
{
	const char	*path;
	struct stat	 st;
	time_t		 now;

	if (!opened)
		return;

	now = time(NULL);
	if (now - last_check < 60)
		return;

	last_check = now;

	path = getenv("ATS_GEOIP_DB_PATH");
	if (path == NULL || stat(path, &st) == -1)
		return;

	if (st.st_size == last_size && st.st_mtime == last_mtime)
		return;

	MMDB_close(&mmdb);
	opened = 0;

	if (open_db() == 0)
		kore_log(LOG_INFO, "geoip: reloaded %s", path);
	else
		kore_log(LOG_ERR, "geoip: reload failed, keeping old data");
}

const char *
ats_geoip_country(const char *ip)
{
	static char		 buf[8];
	MMDB_lookup_result_s	 result;
	MMDB_entry_data_s	 entry;
	int			 gai_error, mmdb_error;

	buf[0] = '\0';

	if (ip == NULL || *ip == '\0')
		return (buf);

	if (open_db() == -1)
		return (buf);

	maybe_reload();

	if (!opened)
		return (buf);

	result = MMDB_lookup_string(&mmdb, ip, &gai_error, &mmdb_error);
	if (gai_error != 0 || mmdb_error != MMDB_SUCCESS || !result.found_entry)
		return (buf);

	if (MMDB_get_value(&result.entry, &entry, "country", "iso_code",
	    NULL) != MMDB_SUCCESS || !entry.has_data ||
	    entry.type != MMDB_DATA_TYPE_UTF8_STRING)
		return (buf);

	if (entry.data_size >= sizeof(buf))
		return (buf);

	for (unsigned int i = 0; i < entry.data_size; i++)
		buf[i] = (char)tolower((unsigned char)entry.utf8_string[i]);
	buf[entry.data_size] = '\0';

	return (buf);
}
