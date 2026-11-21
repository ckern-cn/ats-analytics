#ifndef ATS_H_INCLUDED
#define ATS_H_INCLUDED

#include <kore/kore.h>
#include <kore/http.h>

#include "cJSON.h"

#define ATS_BASE_URL_DEFAULT		"http://127.0.0.1:3001"
#define ATS_COOKIE_DOMAIN_DEFAULT	"127.0.0.1"

/* Rows per page in the dashboard. */
#define ATS_PER_PAGE			10

/* Maximum number of apps listed in the sidebar / accepted from the DB. */
#define ATS_MAX_APPS			512

/* An app may be tracked on up to three hostnames (apex, www, mirror). */
#define ATS_MAX_HOSTS			3

/* Defaults; every value can be overridden through the environment. */
#define ATS_TZ_OFFSET_DEFAULT		480	/* minutes east of UTC */
#define ATS_SESSION_SECONDS_DEFAULT	43200	/* 12 hours */
#define ATS_RETENTION_DAYS_DEFAULT	365

/* Runtime configuration (config.c). */
int		 ats_config_init(void);
const char	*ats_base_url(void);
const char	*ats_base_url_host(void);
const char	*ats_cookie_domain(void);
const char	*ats_admin_user(void);
const char	*ats_admin_hash(void);
const char	*ats_jwt_key(void);
int		 ats_tz_offset_min(void);
int		 ats_session_seconds(void);
int		 ats_retention_days(void);
int		 ats_anonymize_enabled(void);
int		 ats_respect_dnt(void);

#endif
